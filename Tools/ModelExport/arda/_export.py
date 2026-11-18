"""Strict functional model capture, closed compilation and transactional publication."""

from __future__ import annotations

from collections.abc import Mapping, Sequence
from contextlib import contextmanager
import copy
import hashlib
import inspect
import json
import math
import os
from pathlib import Path
import random
import re
import shutil
import subprocess
import sys
import tempfile
import threading

import numpy as np
import onnx
import torch

from ArdaIreeExport import compile_model
from ArdaModelPackage import ExportError, check, read_package, validate_state_dict, write_state_dict
from ArdaDTypes import EXPORT_DTYPES, canonical_dtype, dtype_info, validate_target_dtype
from ._spec import ExportArtifact, ExportResult, SUPPORTED_SMS, supported_architectures


_CAPTURE_LOCK = threading.RLock()


def _architectures(values):
    check(isinstance(values, Sequence) and not isinstance(values, (str, bytes)) and len(values) > 0,
          "target_archs must be a nonempty sequence of integer SMs or 'sm_80' names")
    result = []
    for value in values:
        if type(value) is int:
            target = value
        elif isinstance(value, str) and re.fullmatch(r"sm_[0-9]+", value):
            target = int(value[3:])
        else:
            raise ExportError(f"Invalid CUDA architecture: {value!r}")
        check(target in SUPPORTED_SMS,
              f"Unsupported CUDA target sm_{target} for IREE 3.11.0; choose {', '.join(supported_architectures())}. No target substitution is performed.")
        check(target not in result, f"Duplicate architecture sm_{target}")
        result.append(target)
    return tuple(result)


def _names(values, count, label):
    check(isinstance(values, Sequence) and not isinstance(values, (str, bytes)) and len(values) == count,
          f"{label} must contain exactly {count} names")
    result = list(values)
    check(all(isinstance(name, str) and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_.-]*", name) for name in result),
          f"{label} must contain nonempty portable names")
    check(len(set(result)) == count, f"{label} must be unique")
    return result


def _shape(value):
    check(isinstance(value, Sequence) and not isinstance(value, (str, bytes)) and len(value) > 0,
          "Each tensor shape must be a nonempty sequence of static dimensions")
    check(all(type(dim) is int and dim > 0 for dim in value), "Tensor dimensions must be positive static integers")
    check(math.prod(value) * 4 < 2**40, "Tensor profile exceeds the static allocation limit")
    return tuple(value)


def _port_dtype(value):
    name = canonical_dtype(value)
    check(name in EXPORT_DTYPES, f"Unsupported exported tensor dtype {name}; choose {', '.join(EXPORT_DTYPES)}")
    return name


def _validate_dtypes(dtypes, targets, opset_version):
    names = {canonical_dtype(value) for value in dtypes}
    for name in sorted(names):
        for target in targets:
            validate_target_dtype(name, target)
        check(not name.startswith("float8_") or opset_version >= 19,
              "FP8 capture requires opset_version >= 19; no ONNX opset is silently substituted")
    return names


def _tensor_dtypes(value):
    if isinstance(value, torch.Tensor):
        return {canonical_dtype(value.dtype)}
    if isinstance(value, Mapping):
        return set().union(*(_tensor_dtypes(item) for item in value.values())) if value else set()
    if isinstance(value, (tuple, list)):
        return set().union(*(_tensor_dtypes(item) for item in value)) if value else set()
    return set()


def _reference_array(value):
    """Retain native BF16/FP8 bits without depending on NumPy custom float dtypes."""
    name = canonical_dtype(value.dtype)
    if name == "bfloat16":
        return value.view(torch.uint16).numpy()
    if name.startswith("float8_"):
        return value.view(torch.uint8).numpy()
    return value.numpy()


def _validate_model_state(model):
    storage = {}
    values = list(model.named_parameters(remove_duplicate=False)) + list(model.named_buffers(remove_duplicate=False))
    for name, value in values:
        check(value.layout == torch.strided, f"Model state {name!r} must have dense tensor storage")
        if value.numel() == 0:
            continue
        identity = (value.device, value.untyped_storage().data_ptr())
        check(identity not in storage,
              f"Model state {name!r} shares storage with {storage.get(identity)!r}; "
              "tied parameters and buffers require independent cloned storage before export")
        storage[identity] = name


def _torch_float8_saturation(enum):
    dtype = torch.float8_e4m3fn if enum == 17 else torch.float8_e5m2
    maximum = torch.finfo(dtype).max
    converted = torch.tensor([maximum * 2, float("inf")], dtype=torch.float32).to(dtype).float()
    if bool((converted == maximum).all()):
        return 1
    if bool(torch.isnan(converted).all()) or bool(torch.isinf(converted).all()):
        return 0
    raise ExportError("Cannot establish the installed PyTorch FP8 overflow semantics")


def _normalize_torch_float8_casts(model):
    """Preserve identified PyTorch .to conversions, including dtype-specific overflow.

    Pinned Torch 2.14.1 saturates E4M3FN but permits E5M2 infinity. Probe the
    installed CPU capture behavior rather than assuming both types agree.
    """
    def nodes(sequence):
        for node in sequence:
            for attribute in node.attribute:
                if attribute.type == onnx.AttributeProto.GRAPH:
                    nodes(attribute.g.node)
                elif attribute.type == onnx.AttributeProto.GRAPHS:
                    for graph in attribute.graphs:
                        nodes(graph.node)
            attributes = {attribute.name: attribute for attribute in node.attribute}
            target = attributes.get("to")
            if node.domain not in ("", "ai.onnx") or node.op_type != "Cast" or target is None or target.i not in (17, 19):
                continue
            metadata = {item.key: item.value for item in node.metadata_props}
            origin = metadata.get("pkg.torch.onnx.fx_node", "")
            check(any(f"target=torch.ops.{name}]" in origin for name in
                      ("aten._to_copy.default", "aten.to.dtype", "aten.to.dtype_layout", "prims.convert_element_type.default")),
                  "Cannot establish PyTorch conversion semantics for an exported FP8 Cast")
            preserved = [attribute for attribute in node.attribute if attribute.name != "saturate"]
            del node.attribute[:]
            node.attribute.extend(preserved)
            node.attribute.append(onnx.helper.make_attribute("saturate", _torch_float8_saturation(target.i)))
    nodes(model.graph.node)
    for function in model.functions:
        nodes(function.node)


def _input_leaves(value, path=(), ancestors=frozenset()):
    if isinstance(value, torch.Tensor):
        _port_dtype(value.dtype)
        _shape(tuple(value.shape))
        check(value.is_contiguous(), "Input tensors must be dense contiguous; call contiguous() explicitly")
        check(bool(torch.isfinite(value.float()).all()), "Input tensors must contain finite values")
        return [(path, value)]
    if isinstance(value, Mapping):
        check(id(value) not in ancestors, "Cyclic input containers are unsupported")
        check(all(isinstance(key, str) for key in value), "Input dictionaries require string keys")
        return [leaf for key, child in value.items() for leaf in _input_leaves(child, path + (key,), ancestors | {id(value)})]
    if isinstance(value, (tuple, list)):
        check(id(value) not in ancestors, "Cyclic input containers are unsupported")
        return [leaf for index, child in enumerate(value) for leaf in _input_leaves(child, path + (index,), ancestors | {id(value)})]
    check(type(value) in (type(None), bool, int, float, str),
          "Input leaves must be tensors or frozen int/float/bool/string/None values")
    check(not isinstance(value, float) or math.isfinite(value), "Frozen scalar inputs must be finite")
    return []


def _clone_call(value, memo=None):
    memo = {} if memo is None else memo
    if id(value) in memo:
        return memo[id(value)]
    if isinstance(value, torch.Tensor):
        result = value.detach().cpu().clone()
        memo[id(value)] = result
        return result
    if isinstance(value, Mapping):
        result = {key: _clone_call(child, memo) for key, child in value.items()}
        memo[id(value)] = result
        return result
    if isinstance(value, tuple):
        result = tuple(_clone_call(child, memo) for child in value)
        memo[id(value)] = result
        return result
    if isinstance(value, list):
        result = [_clone_call(child, memo) for child in value]
        memo[id(value)] = result
        return result
    return value


def _argument_manifest(value, tensor_names):
    if isinstance(value, torch.Tensor):
        return {"tensor": next(tensor_names)}
    if isinstance(value, Mapping):
        return {key: _argument_manifest(child, tensor_names) for key, child in value.items()}
    if isinstance(value, (tuple, list)):
        return [_argument_manifest(child, tensor_names) for child in value]
    return value


def _inputs(model, args, kwargs, input_names, targets, opset_version):
    args = (args,) if isinstance(args, torch.Tensor) else args
    check(isinstance(args, tuple), "args must be a tuple of model arguments, or one tensor")
    check(kwargs is None or isinstance(kwargs, Mapping), "kwargs must be a mapping of forward keyword arguments")
    kwargs = dict(kwargs or {})
    check(all(isinstance(key, str) for key in kwargs), "kwargs keys must be strings")
    try:
        bound = inspect.signature(model.forward).bind(*args, **kwargs)
        bound.apply_defaults()
    except (TypeError, ValueError) as error:
        raise ExportError(f"Cannot bind args/kwargs to model.forward: {error}") from error
    # BoundArguments canonicalizes keyword order to the forward declaration. This
    # matches the flattened strict-capture ABI while retaining nested input paths.
    leaves = _input_leaves(bound.arguments)
    check(leaves, "At least one tensor input is required")
    storage = set()
    for location, value in leaves:
        identity = (value.device, value.untyped_storage().data_ptr())
        check(identity not in storage,
              f"Tensor input {location} shares storage with another input; exported tensor ports must be independent")
        storage.add(identity)
    _validate_dtypes((value.dtype for _, value in leaves), targets, opset_version)
    defaults = [f"input{index}" for index in range(len(leaves))]
    names = _names(input_names if input_names is not None else defaults, len(leaves), "input_names")
    arguments = _argument_manifest(bound.arguments, iter(names))
    return bound.args, bound.kwargs, leaves, names, arguments


def _outputs(value, path=()):
    if isinstance(value, torch.Tensor):
        _port_dtype(value.dtype)
        _shape(tuple(value.shape))
        check(bool(torch.isfinite(value.float()).all()), "CPU reference produced non-finite output")
        return [(path, value.detach().cpu().contiguous().clone())]
    if isinstance(value, Mapping):
        check(value and all(isinstance(key, str) for key in value), "Output mappings require nonempty string-keyed tensors")
        return [leaf for key, child in value.items() for leaf in _outputs(child, path + (key,))]
    if isinstance(value, (tuple, list)):
        check(value, "Empty output containers are unsupported")
        return [leaf for index, child in enumerate(value) for leaf in _outputs(child, path + (index,))]
    raise ExportError("Outputs must be tensors or nested tuples/lists/string-keyed mappings of tensors")


@contextmanager
def _preserve_rng():
    python_state, numpy_state = random.getstate(), np.random.get_state()
    devices = list(range(torch.cuda.device_count())) if torch.cuda.is_initialized() else []
    try:
        with torch.random.fork_rng(devices=devices):
            yield
    finally:
        random.setstate(python_state)
        np.random.set_state(numpy_state)


def _capture(model, args, kwargs, input_names, output_names, path, opset_version, targets):
    try:
        working = copy.deepcopy(model)
        check(working is not model, "The module's deepcopy must produce an independent module")
        working = working.cpu().eval()
        pristine = copy.deepcopy(working)
        args, kwargs = _clone_call((args, kwargs))
        values = [value for _, value in _input_leaves((args, kwargs))]
        # Strict Dynamo capture rejects tensor-dependent Python branches rather than
        # silently tracing whichever path happened to run for these examples.
        program = torch.export.export(working, args, kwargs=kwargs, strict=True)
        # The initial signature can hide aten.add_ mutations. Functionalization
        # makes state/input writes explicit in the output signature.
        program = program.run_decompositions({})
        observed_dtypes = _tensor_dtypes(program.state_dict) | _tensor_dtypes(program.constants)
        for item in program.graph_signature.output_specs:
            check(item.kind.name == "USER_OUTPUT", "Stateful forward mutation is unsupported; expose state as tensor inputs/outputs")
        for node in program.graph.nodes:
            observed_dtypes |= _tensor_dtypes(node.meta.get("val"))
            check(torch.Tag.nondeterministic_seeded not in getattr(node.target, "tags", ()),
                  "Random operations require explicit RNG/state tensor inputs")
        _validate_dtypes(observed_dtypes, targets, opset_version)
        with torch.no_grad():
            result = _outputs(pristine(*copy.deepcopy(args), **copy.deepcopy(kwargs)))
            captured = _outputs(program.module()(*copy.deepcopy(args), **copy.deepcopy(kwargs)))
        check([location for location, _ in result] == [location for location, _ in captured],
              "Strict capture changed the model's output structure")
        for (_, expected), (_, actual) in zip(result, captured):
            check(actual.dtype == expected.dtype, "Strict capture changed an output dtype")
            # PyTorch CPU has no native isclose for FP8. Compare numerical values
            # after confirming the captured output retained the exact dtype.
            is_fp8 = canonical_dtype(actual.dtype).startswith("float8_")
            torch.testing.assert_close(actual.float() if is_fp8 else actual, expected.float() if is_fp8 else expected,
                msg="Strict capture disagrees with the pristine eager model")
        defaults = ["_".join(str(part) for part in location) if location and isinstance(location[0], str)
                    else f"output{index}" for index, (location, _) in enumerate(result)]
        output_names = _names(output_names if output_names is not None else defaults, len(result), "output_names")
        check(not set(output_names) & set(input_names), "Input and output port names must be distinct")
        # Optimizing initializer values here would freeze this checkpoint into
        # the graph and destroy the names needed for subsequent state loading.
        torch.onnx.export(program, f=str(path), dynamo=True, opset_version=opset_version,
            input_names=input_names, output_names=output_names, external_data=False, optimize=False, verbose=False)
        encoded = onnx.load(path)
        initializers = {value.name: value for value in encoded.graph.initializer}
        used_inputs = {node.name for node in program.graph.nodes if node.op == "placeholder" and node.users}
        used_state = {item.target for item in program.graph_signature.input_specs
                      if item.target in program.state_dict and item.arg.name in used_inputs}
        check(used_state <= initializers.keys(),
              f"ONNX capture removed or renamed model state {sorted(used_state - initializers.keys())}; "
              "choose distinct port names and export without parameter folding")
        state_schema = []
        for name, value in program.state_dict.items():
            check(isinstance(value, torch.Tensor), f"State dict entry {name!r} must be a tensor")
            state_schema.append({"name": name, "dtype": canonical_dtype(value.dtype),
                                 "shape": list(value.shape), "exported": name in initializers})
        state_names = [entry["name"] for entry in state_schema if entry["exported"]]
        # Literal tensors and nonpersistent buffers are immutable model constants.
        encoded.metadata_props.add(key="arda.state_dict", value=json.dumps(state_names))
        _normalize_torch_float8_casts(encoded)
        onnx.checker.check_model(encoded, full_check=True)
        onnx.save(encoded, path)
        check([item.name for item in encoded.graph.input] == input_names,
              "Capture removed or renamed an input; all declared ports must participate in the model")
        check([item.name for item in encoded.graph.output] == output_names, "ONNX output port names/order changed")
        references = dict(zip(input_names, [_reference_array(value) for value in values]))
        references.update((name, _reference_array(value)) for name, (_, value) in zip(output_names, result))
        port_dtypes = dict(zip(input_names, [canonical_dtype(value.dtype) for value in values]))
        port_dtypes.update((name, canonical_dtype(value.dtype)) for name, (_, value) in zip(output_names, result))
        _validate_dtypes(port_dtypes.values(), targets, opset_version)
        return (references, output_names, [list(location) for location, _ in result], port_dtypes,
                sorted(observed_dtypes), state_schema)
    except ExportError:
        raise
    except Exception as error:
        raise ExportError(f"Strict PyTorch/ONNX capture failed ({type(error).__name__}): {str(error)[-6000:]}") from error


def _verify(package, checkpoint, reference, output, rtol, atol):
    import ArdaCudaReplay
    # A clean process ensures PyTorch/IREE are absent from the GPU replay and does
    # not replace the caller's current CUDA context.
    process = subprocess.run([sys.executable, str(Path(ArdaCudaReplay.__file__).resolve()),
        str(package), str(reference), "--state-dict", str(checkpoint), "--output", str(output),
        "--rtol", str(rtol), "--atol", str(atol)],
        capture_output=True, text=True)
    check(process.returncode == 0, "CUDA-driver numerical verification failed:\n" + (process.stderr + process.stdout)[-6000:])
    return json.loads(output.with_suffix(".json").read_text())


def _publish(stage, output, manifest_name):
    """Replace only this call's files, rolling back on an ordinary publication error."""
    output.mkdir(parents=True, exist_ok=True)
    lock_path = output / (manifest_name + ".lock")
    try:
        descriptor = os.open(lock_path, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    except FileExistsError as error:
        raise ExportError("Another export is publishing this manifest") from error
    os.close(descriptor)
    backup = stage / "_previous"
    backup.mkdir()
    published, replaced = [], []
    entries = sorted((item for item in stage.iterdir() if item != backup), key=lambda item: item.name == manifest_name)
    try:
        for item in entries:
            destination = output / item.name
            if destination.exists():
                destination.replace(backup / item.name)
                replaced.append(item.name)
            item.replace(destination)
            published.append(item.name)
    except BaseException:
        for name in reversed(published):
            (output / name).replace(stage / name)
        for name in reversed(replaced):
            (backup / name).replace(output / name)
        raise
    finally:
        lock_path.unlink()


def _staging_parent(output):
    """Use the destination filesystem without creating the final output directory."""
    # An existing destination can itself be a mount point or directory junction.
    parent = output if output.is_dir() else output.parent
    while not parent.exists():
        parent = parent.parent
    check(parent.is_dir(), "The output directory's existing parent must be a directory")
    return parent


def _validate_ports(package, path, input_names, output_names, references, port_dtypes):
    check(path.stat().st_size <= 512 * 1024 * 1024,
          "Compiled package exceeds the native reader's 512 MiB limit")
    ports = [tensor for tensor in package["tensors"] if tensor["role"] != 0]
    check(len(ports) == len(input_names) + len(output_names), "Compiler changed the number of public tensor ports")
    check(len({tensor["name"] for tensor in ports}) == len(ports), "Compiler emitted duplicate tensor ports")
    actual = {tensor["name"]: tensor for tensor in ports}
    check(set(actual) == set(input_names + output_names), "Compiler removed or renamed a public tensor port")
    for name in input_names + output_names:
        tensor = actual[name]
        check(tensor["role"] == (1 if name in input_names else 2), "Compiler changed a tensor port's role")
        check(tensor["dtype"] == dtype_info(port_dtypes[name]).dtype_id and tuple(tensor["shape"]) == references[name].shape,
              "Compiler changed a public tensor port's dtype or shape")


def _result(name, directory, targets, files, owner=None):
    return ExportResult(name, directory / f"{name}.manifest.json", directory / f"{name}.onnx",
        directory / f"{name}.reference.npz",
        tuple(ExportArtifact(target, directory / f"{name}.sm_{target}.ardagpu",
                             directory / f"{name}.sm_{target}.json",
                             directory / f"{name}.sm_{target}.ardastate") for target in targets),
        tuple(files), owner)


def _save_result(result, directory):
    output = Path(directory).expanduser().resolve()
    source = result.manifest_path.parent.resolve()
    check(result.manifest_path.is_file(), "The exported bundle was closed or removed")
    check(result._owner is None or not output.is_relative_to(source),
          "Save a temporary bundle outside its owned temporary directory")
    if output == source:
        return result
    with tempfile.TemporaryDirectory(prefix=".arda-save-", dir=_staging_parent(output)) as temporary:
        stage = Path(temporary)
        for name in result._files:
            entry = source / name
            check(entry.exists(), f"Exported bundle member is missing: {name}")
            if entry.is_dir():
                shutil.copytree(entry, stage / name)
            else:
                shutil.copy2(entry, stage / name)
        _publish(stage, output, result.manifest_path.name)
    return _result(result.name, output, [artifact.target_sm for artifact in result.artifacts], result._files)


def export_state_dict(state_dict, model, f):
    """Write replacement weights for an existing descriptor without compiling.

    state_dict is a torch.nn.Module or a mapping of names to tensors. model is
    an ExportResult, ExportArtifact, or .ardagpu descriptor path. An ExportResult
    accepts the complete original module state, including recorded unused keys;
    a descriptor alone requires exactly its exported state slots. Shapes and
    dtypes must match and tensor values are stored without conversion. f names
    the .ardastate file; the existing descriptor is never changed.
    """
    source_schema = None
    if isinstance(model, ExportResult):
        check(model.manifest_path.is_file(), "The exported bundle was closed or removed")
        manifest = json.loads(model.manifest_path.read_text(encoding="utf-8"))
        source_schema = manifest.get("capture", {}).get("state_dict")
        descriptors = list(model.descriptors.values())
    elif isinstance(model, ExportArtifact):
        descriptors = [model.descriptor_path]
    else:
        descriptors = [Path(model).expanduser().resolve()]
    check(descriptors, "The export contains no model descriptors")
    packages = [read_package(path) for path in descriptors]
    check(all(package["version"] == 2 for package in packages),
          "Replacement checkpoints require a version 2 model descriptor; re-export the model")
    values = state_dict.state_dict() if isinstance(state_dict, torch.nn.Module) else state_dict
    check(isinstance(values, Mapping) and all(isinstance(name, str) for name in values),
          "state_dict must be a module or a mapping of string names to tensors")
    slots = packages[0]["state_slots"]
    expected = source_schema if source_schema is not None else slots
    expected_names = {entry["name"] for entry in expected}
    actual_names = set(values)
    check(actual_names == expected_names,
          f"State dict keys differ: missing={sorted(expected_names - actual_names)}, "
          f"unexpected={sorted(actual_names - expected_names)}")
    for entry in expected:
        value = values[entry["name"]]
        check(isinstance(value, torch.Tensor), f"State dict entry {entry['name']!r} must be a tensor")
        check(value.layout == torch.strided, f"State dict entry {entry['name']!r} must have dense tensor storage")
        check(dtype_info(value.dtype).dtype_id == dtype_info(entry["dtype"]).dtype_id,
              f"State dict dtype mismatch for {entry['name']!r}")
        check(list(value.shape) == entry["shape"], f"State dict shape mismatch for {entry['name']!r}")
    entries = []
    for slot in slots:
        value = values[slot["name"]].detach().cpu().contiguous()
        entries.append({"name": slot["name"], "dtype": slot["dtype"], "shape": slot["shape"],
                        "data": _reference_array(value).tobytes()})
    state = {"version": 1, "entries": entries}
    for package in packages:
        validate_state_dict(state, package)
    output = Path(f).expanduser().resolve()
    check(output.suffix == ".ardastate", "Checkpoint output must use the .ardastate extension")
    with tempfile.TemporaryDirectory(prefix=".arda-state-", dir=_staging_parent(output.parent)) as temporary:
        stage = Path(temporary)
        write_state_dict(state, stage / output.name)
        _publish(stage, output.parent, output.name)
    return output


def export(model, args=(), f=None, *, kwargs=None, target_archs=("sm_80",), input_names=None,
           output_names=None, name=None, opset_version=22, keep_ir=False, verify=False, rtol=1e-4, atol=1e-5):
    """Export a PyTorch call into separate model descriptors and checkpoints.

    Like torch.onnx.export, args/kwargs are examples of the actual forward call.
    Tensor leaves define typed ports; scalar leaves are frozen configuration.
    Nested tuple/list/string-keyed dictionaries are flattened deterministically.
    f is a bundle DIRECTORY. When omitted, the returned ExportResult owns its
    temporary files; result.save(directory) publishes a copy with persistent paths.
    No input, output or model weight is implicitly cast, calibrated or quantized.
    Each .ardagpu file describes a statically shaped CUDA subgraph; its sibling
    .ardastate supplies named weights. export_state_dict writes other checkpoints
    for the same descriptors without capturing or compiling the model again.
    Structural/capture/closure checks are mandatory; verify=True also performs
    numerical comparison in isolated CUDA-driver-only processes on the local GPU.
    """
    targets = _architectures(target_archs)
    check(isinstance(model, torch.nn.Module), "model must be a torch.nn.Module")
    check(type(opset_version) is int and opset_version >= 18, "Dynamo ONNX export requires opset_version >= 18")
    check(type(keep_ir) is bool and type(verify) is bool, "keep_ir and verify must be booleans")
    check(all(isinstance(value, (int, float)) and math.isfinite(value) and value >= 0 for value in (rtol, atol)),
          "Numerical tolerances must be finite nonnegative numbers")
    name = name if name is not None else re.sub(r"[^A-Za-z0-9_-]+", "_", type(model).__name__).lower()
    check(isinstance(name, str) and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_-]*", name), "name must be a portable identifier, not a path")
    output_dir = None if f is None else Path(f).expanduser().resolve()
    with _CAPTURE_LOCK, _preserve_rng():
        _validate_model_state(model)
        _validate_dtypes((value.dtype for value in list(model.parameters()) + list(model.buffers())), targets, opset_version)
        args, kwargs, inputs, input_names, arguments = _inputs(model, args, kwargs, input_names, targets, opset_version)
        owner = tempfile.TemporaryDirectory(prefix=".arda-export-",
            dir=None if output_dir is None else _staging_parent(output_dir))
        try:
            stage = Path(owner.name)
            onnx_path = stage / (name + ".onnx")
            reference_path = stage / (name + ".reference.npz")
            references, output_names, output_structure, port_dtypes, observed_dtypes, state_schema = _capture(model, args, kwargs,
                input_names, output_names, onnx_path, opset_version, targets)
            np.savez(reference_path, **references)
            variants = []
            for target in targets:
                package_path = stage / f"{name}.sm_{target}.ardagpu"
                checkpoint_path = package_path.with_suffix(".ardastate")
                package = compile_model(onnx_path, package_path, target, keep_ir=keep_ir)
                _validate_ports(package, package_path, input_names, output_names, references, port_dtypes)
                verification = {"status": "not_run"}
                if verify:
                    report = _verify(package_path, checkpoint_path, reference_path,
                                     stage / f"{name}.sm_{target}.actual.npz", rtol, atol)
                    verification = {"status": "passed", "rtol": rtol, "atol": atol, "outputs": report,
                                    "scope": "local CUDA device; not every deployment device"}
                variants.append({"architecture": f"sm_{target}", "target_sm": target,
                    "descriptor": package_path.name, "package": package_path.name,
                    "state_dict": checkpoint_path.name, "metadata": package_path.with_suffix(".json").name,
                    "sha256": hashlib.sha256(package_path.read_bytes()).hexdigest(),
                    "state_dict_sha256": hashlib.sha256(checkpoint_path.read_bytes()).hexdigest(),
                    "state_slots": [{key: slot[key] for key in ("name", "dtype", "shape", "byte_size")}
                                    for slot in package["state_slots"]],
                    "kernels": len(package["kernels"]), "commands": len(package["commands"]),
                    "verification": verification})
            manifest_name = name + ".manifest.json"
            manifest = {"format": "arda.export", "version": 2, "name": name,
                "requested_architectures": [f"sm_{target}" for target in targets],
                "capture": {"torch": torch.__version__, "mode": "strict torch.export, eval, CPU",
                    "opset_version": opset_version, "arguments": arguments, "tensor_dtypes": observed_dtypes,
                    "state_dict": state_schema,
                    "inputs": [{"name": port, "shape": list(value.shape), "dtype": canonical_dtype(value.dtype),
                                "reference_storage": dtype_info(value.dtype).storage_dtype, "source_path": list(location)}
                               for port, (location, value) in zip(input_names, inputs)],
                    "outputs": [{"name": port, "shape": list(references[port].shape), "dtype": port_dtypes[port],
                                  "reference_storage": dtype_info(port_dtypes[port]).storage_dtype,
                                  "source_path": location} for port, location in zip(output_names, output_structure)]},
                "audit": {"onnx": onnx_path.name, "reference": reference_path.name,
                          "onnx_sha256": hashlib.sha256(onnx_path.read_bytes()).hexdigest()},
                "runtime_dependencies": ["Arda", "CUDA driver"],
                "runtime_files": [variant[key] for variant in variants for key in ("descriptor", "state_dict")],
                "artifacts": variants}
            (stage / manifest_name).write_text(json.dumps(manifest, indent=2, allow_nan=False) + "\n", encoding="utf-8")
            files = tuple(item.name for item in stage.iterdir())
            if output_dir is None:
                return _result(name, stage, targets, files, owner)
            _publish(stage, output_dir, manifest_name)
            owner.cleanup()
            return _result(name, output_dir, targets, files)
        except BaseException:
            owner.cleanup()
            raise
