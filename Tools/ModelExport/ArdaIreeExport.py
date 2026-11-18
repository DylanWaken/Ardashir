"""Compile ONNX/MLIR offline and lower a bounded static HAL program to Arda.

IREE and NumPy are build-time dependencies. No IREE bytecode or runtime is shipped.
Unknown operations, data-dependent host values and external device symbols fail export.
"""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import importlib.metadata
import json
import math
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from collections import Counter
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path

import numpy as np
from iree.compiler import ir

from ArdaModelPackage import (ExportError, check, contiguous_strides, validate_state_dict,
                              write_package, write_state_dict)
from ArdaDTypes import validate_target_dtype


DTYPES = {"i1": (1, 1), "i8": (2, 1), "ui8": (3, 1), "i16": (4, 2),
          "ui16": (5, 2), "i32": (6, 4), "ui32": (7, 4), "i64": (8, 8),
          "ui64": (9, 8), "f16": (10, 2), "bf16": (11, 2), "f32": (12, 4), "f64": (13, 8),
          "f8E4M3FN": (14, 1), "f8E5M2": (15, 1)}

# IREE 3.11 KnownTargets.cpp has profiles through Ampere only. LLVM in that
# same wheel supports these newer real architectures. Supply a conservative
# GPU attribute explicitly; do not compile for an older SM and relabel it.
# See compiler/plugins/target/CUDA/CUDATarget.cpp and GPU/IR/IREEGPUAttrs.td
# at https://github.com/iree-org/iree/tree/v3.11.0/compiler .
_BUILTIN_TARGETS = (60, 61, 62, 70, 72, 75, 80, 86, 87)
_EXPLICIT_TARGETS = {89: 84, 90: 84, 120: 87}
SUPPORTED_TARGETS = _BUILTIN_TARGETS + tuple(_EXPLICIT_TARGETS)


def target_options(target):
    check(type(target) is int and target in SUPPORTED_TARGETS,
          f"Unsupported audited CUDA target sm_{target}; supported: {SUPPORTED_TARGETS}")
    if target not in _EXPLICIT_TARGETS:
        return ["--iree-hal-target-device=cuda", f"--iree-cuda-target=sm_{target}"]
    ptx = _EXPLICIT_TARGETS[target]
    # Portable CUDA limits and SIMT scheduling only: no unverified MMA, shared
    # memory opt-in, architecture-specific subgroup, or cluster capabilities.
    wgp = ("wgp = <compute = fp64|fp32|fp16|int64|int32|int16|int8, "
           "storage = b64|b32|b16|b8, subgroup = shuffle|arithmetic, dot = dp4xi8toi32, "
           "subgroup_size_choices = [32], max_workgroup_sizes = [1024, 1024, 1024], "
           "max_thread_count_per_workgroup = 1024, max_workgroup_memory_bytes = 49152, "
           "max_workgroup_counts = [2147483647, 65535, 65535]>")
    gpu = f'#iree_gpu.target<arch = "sm_{target}", features = "+ptx{ptx}", {wgp}>'
    device = ('#hal.device.target<"cuda", [#hal.executable.target<"cuda", "cuda-nvptx-fb", '
              '{iree_codegen.target_info = ' + gpu + '}>]>')
    return [f"--iree-hal-target-device={device}", f"--iree-cuda-target=sm_{target}",
            f"--iree-cuda-target-features=+ptx{ptx}"]


def validate_onnx_precision(model, target):
    """Check internal typed values as well as public ports, before compilation."""
    import onnx
    low_types = {onnx.TensorProto.FLOAT16: 10, onnx.TensorProto.BFLOAT16: 11,
                 onnx.TensorProto.FLOAT8E4M3FN: 14, onnx.TensorProto.FLOAT8E5M2: 15,
                 onnx.TensorProto.INT8: 2}

    def dtype(value):
        if value in low_types:
            validate_target_dtype(low_types[value], target)

    def visit(message):
        if isinstance(message, onnx.TensorProto):
            dtype(message.data_type)
        elif isinstance(message, onnx.TypeProto.Tensor):
            dtype(message.elem_type)
        elif isinstance(message, onnx.NodeProto):
            for attr in message.attribute:
                if attr.type == onnx.AttributeProto.INT and attr.name in ("to", "dtype", "output_dtype"):
                    dtype(attr.i)
        for field, value in message.ListFields():
            if field.type == field.TYPE_MESSAGE:
                if field.is_repeated:
                    for child in value:
                        visit(child)
                else:
                    visit(value)
    visit(model)


def validate_mlir_precision(module, target):
    """Inspect actual IR types; quoted strings and raw storage are not dtypes."""
    pattern = re.compile(r"(?<![A-Za-z0-9_])(f8E4M3FN|f8E5M2|bf16|f16|i8)(?![A-Za-z0-9_])")
    found = set()

    def dtype(value):
        if isinstance(value, (ir.RankedTensorType, ir.UnrankedTensorType, ir.MemRefType, ir.VectorType)):
            dtype(ir.ShapedType(value).element_type)
            return
        if isinstance(value, ir.FunctionType):
            function = ir.FunctionType(value)
            for child in (*function.inputs, *function.results):
                dtype(child)
            return
        for match in pattern.finditer(str(value)):
            found.add(match[1])
            validate_target_dtype(DTYPES[match[1]][0], target)

    def attribute(attr):
        if isinstance(attr, ir.TypeAttr):
            dtype(attr.value)
        elif isinstance(attr, ir.DenseElementsAttr):
            dtype(attr.type)
        elif isinstance(attr, ir.ArrayAttr):
            for child in attr:
                attribute(child)
        elif isinstance(attr, ir.DictAttr):
            for child in attr:
                attribute(child.attr)

    def visit(op):
        for value in (*op.operands, *op.results):
            dtype(value.type)
        for named in op.attributes:
            attribute(op.attributes[named] if isinstance(named, str) else named.attr)
        for region in op.regions:
            for block in region.blocks:
                for arg in block.arguments:
                    dtype(arg.type)
                for child in block.operations:
                    visit(child)
    visit(module.operation)
    return found


def lower_onnx_fp8_casts(module):
    """Fill the pinned ONNX Cast mapping gap, preserving Cast-19 saturation.

    ATen scalar enums match the wheel's extras/fx_importer.py: E5M2=23,
    E4M3FN=24. ONNX default saturate=1 clamps to finite extrema before rounding;
    saturate=0 retains the native overflow-to-NaN/Inf behavior.
    """
    casts = []

    def visit(op):
        if op.operation.name == "torch.operator" and "torch.onnx.to" in op.attributes:
            to = ir.IntegerAttr(op.attributes["torch.onnx.to"]).value
            if attr_string(op.attributes["name"]) == "onnx.Cast" and to in (17, 19):
                casts.append((op, to))
        for region in op.regions:
            for block in region.blocks:
                for child in list(block.operations):
                    visit(child)
    visit(module.operation)
    for op, to in casts:
        check(len(op.operands) == len(op.results) == 1, "Invalid FP8 Cast operands")
        keys = [named if isinstance(named, str) else named.name for named in op.attributes]
        check(all(not key.startswith("torch.onnx.") or key in ("torch.onnx.to", "torch.onnx.saturate") for key in keys),
              "Unsupported FP8 Cast semantic attribute; only round-to-nearest-even Cast-19 saturation is audited")
        expected = "f8E4M3FN" if to == 17 else "f8E5M2"
        check(str(op.results[0].type).endswith("," + expected + ">"), "FP8 Cast type disagrees with its ONNX attribute")
        saturation = ir.IntegerAttr(op.attributes["torch.onnx.saturate"]).value if "torch.onnx.saturate" in op.attributes else 1
        check(saturation in (0, 1), "FP8 Cast saturate must be zero or one")
        with ir.InsertionPoint(op), op.location:
            def constant(name, type_name, value=None):
                attrs = {} if value is None else {"value": value}
                return ir.Operation.create(name, results=[ir.Type.parse(type_name)], attributes=attrs).results[0]
            value = op.operands[0]
            if saturation:
                limit = 448.0 if to == 17 else 57344.0
                low = constant("torch.constant.float", "!torch.float", ir.FloatAttr.get(ir.F64Type.get(), -limit))
                high = constant("torch.constant.float", "!torch.float", ir.FloatAttr.get(ir.F64Type.get(), limit))
                clipped = ir.Operation.create("torch.aten.clamp", results=[value.type], operands=[value, low, high]).results[0]
                # The pinned clamp lowering uses min/max-number behavior for
                # NaNs. ONNX Cast saturation preserves NaNs explicitly.
                bool_type = ir.Type.parse(str(value.type).rsplit(",", 1)[0] + ",i1>")
                isnan = ir.Operation.create("torch.aten.isnan", results=[bool_type], operands=[value]).results[0]
                value = ir.Operation.create("torch.aten.where.self", results=[value.type], operands=[isnan, value, clipped]).results[0]
            scalar = constant("torch.constant.int", "!torch.int", ir.IntegerAttr.get(ir.IntegerType.get_signless(64), 24 if to == 17 else 23))
            false = constant("torch.constant.bool", "!torch.bool", ir.BoolAttr.get(False))
            none = constant("torch.constant.none", "!torch.none")
            converted = ir.Operation.create("torch.aten.to.dtype", results=[op.results[0].type],
                                            operands=[value, scalar, false, false, none])
            op.results[0].replace_all_uses_with(converted.results[0])
            op.operation.erase()
    return bool(casts)


def _walk_operations(op):
    yield op.operation
    for region in op.regions:
        for block in region.blocks:
            for child in block.operations:
                yield from _walk_operations(child)


def fp8_conversion_counts(module):
    counts = [0, 0]
    for op in _walk_operations(module.operation):
        if op.name == "arith.extf" and str(op.operands[0].type) == "f8E4M3FN":
            counts[0] += 1
        elif op.name == "arith.truncf" and str(op.results[0].type) in ("f8E4M3FN", "f8E5M2"):
            counts[1] += 1
    return counts


def repair_pinned_fp8_conversion(module, expected):
    """Correct two audited IREE 3.11 scalar conversion defects, fail closed.

    ConvertUnsupportedFloatArithPass.cpp compares an FN NaN against the signed
    encoding and masks away the carry when a subnormal rounds to minimum normal.
    Match the exact operand structure emitted by that pinned pass; the number
    of repairs must agree with the original casts. Never patch arbitrary PTX.
    """
    counts = [0, 0]

    def owner(value, name):
        candidate = value.owner
        if isinstance(candidate, ir.OpView):
            candidate = candidate.operation
        return candidate if isinstance(candidate, ir.Operation) and candidate.name == name else None

    def integer(value):
        op = owner(value, "arith.constant")
        return ir.IntegerAttr(op.attributes["value"]).value if op is not None and isinstance(op.attributes["value"], ir.IntegerAttr) else None

    for op in list(_walk_operations(module.operation)):
        if op.name == "arith.cmpi" and ir.IntegerAttr(op.attributes["predicate"]).value == 0 and integer(op.operands[1]) == 127:
            extend = owner(op.operands[0], "arith.extui")
            bits = owner(extend.operands[0], "arith.bitcast") if extend is not None else None
            if bits is not None and str(bits.operands[0].type) == "f8E4M3FN":
                with ir.InsertionPoint(op), op.location:
                    masked = ir.Operation.create("arith.andi", results=[op.operands[0].type],
                                                 operands=[op.operands[0], op.operands[1]])
                    op.operands[0] = masked.results[0]
                counts[0] += 1
        elif op.name == "arith.andi" and integer(op.operands[1]) in (3, 7):
            shift = owner(op.operands[0], "arith.shrui")
            distance = owner(shift.operands[1], "arith.subi") if shift is not None else None
            rounded = owner(shift.operands[0], "arith.addi") if shift is not None else None
            mantissa = owner(rounded.operands[0], "arith.ori") if rounded is not None else None
            if distance is not None and mantissa is not None and integer(mantissa.operands[1]) == 0x800000:
                # The shifted rounded mantissa is already bounded. Keeping its
                # carry bit produces exponent1/mantissa0 at the normal boundary.
                op.results[0].replace_all_uses_with(op.operands[0])
                op.erase()
                counts[1] += 1
    check(counts == expected,
          f"Pinned FP8 conversion structure changed: expected repairs {expected}, found {counts}")
    return counts


class _MlirAttribute(ctypes.Structure):
    _fields_ = [("ptr", ctypes.c_void_p)]


@lru_cache(maxsize=1)
def _dense_raw_reader():
    # MLIR's documented Python capsule bridge exposes the C attribute handle.
    # The C API returns stored bits, unlike conversion through a Python float:
    # NaN payloads, signed zero and all FP8 encodings survive unchanged.
    # https://mlir.llvm.org/docs/Bindings/Python/#interoperability-with-the-c-api
    import iree.compiler._mlir_libs as libs
    filename = "IREECompiler.dll" if sys.platform == "win32" else (
        "libIREECompiler.dylib" if sys.platform == "darwin" else "libIREECompiler.so")
    library = ctypes.CDLL(str(Path(libs.__file__).parent / filename))
    reader = library.mlirDenseElementsAttrGetRawData
    reader.argtypes = [_MlirAttribute]
    reader.restype = ctypes.c_void_p
    return library, reader


class FlatBuffer:
    """The small, pinned CDA1 executable schema; every read is bounds checked."""

    def __init__(self, data):
        self.data = data
        if data[:4] == b"CDA1":
            check(len(data) >= 72 and data[68:72] == b"CDA1", "Invalid CDA1 wrapper")
            self.data = data[64:]
        check(self.data[4:8] == b"CDA1", "Unsupported CUDA executable schema")
        self.root = self.read("I", 0)

    def read(self, fmt, offset):
        check(0 <= offset <= len(self.data) - struct.calcsize(fmt), "Truncated executable metadata")
        return struct.unpack_from("<" + fmt, self.data, offset)[0]

    def field(self, table, ordinal):
        vtable = table - self.read("i", table)
        slot = 4 + ordinal * 2
        if slot >= self.read("H", vtable):
            return 0
        relative = self.read("H", vtable + slot)
        return table + relative if relative else 0

    def integer(self, table, ordinal, default=0):
        field = self.field(table, ordinal)
        return self.read("I", field) if field else default

    def vector(self, table, ordinal, stride=4, tables=True):
        field = self.field(table, ordinal)
        if not field:
            return []
        start = field + self.read("I", field)
        count = self.read("I", start)
        check(count <= len(self.data) // stride, "Invalid executable vector length")
        slots = [start + 4 + i * stride for i in range(count)]
        return [slot + self.read("I", slot) for slot in slots] if tables else slots

    def string(self, table, ordinal):
        field = self.field(table, ordinal)
        check(field != 0, "Missing executable string")
        start = field + self.read("I", field)
        length = self.read("I", start)
        check(start + 4 + length <= len(self.data), "Truncated executable string")
        return self.data[start + 4:start + 4 + length]


@dataclass(frozen=True)
class Buffer:
    index: int
    offset: int = 0
    length: int | None = None


@dataclass
class Tensor:
    buffer: Buffer
    shape: list
    dtype: str
    name: str = ""


def attr_string(attr):
    if isinstance(attr, ir.StringAttr):
        return ir.StringAttr(attr).value
    if isinstance(attr, ir.FlatSymbolRefAttr):
        return ir.FlatSymbolRefAttr(attr).value
    return str(attr)


def constant_bytes(attr):
    if isinstance(attr, ir.DenseElementsAttr):
        dense = ir.DenseElementsAttr(attr)
        shaped = ir.ShapedType(dense.type)
        dtype = str(shaped.element_type)
        if dtype in ("bf16", "f8E4M3FN", "f8E5M2"):
            check(sys.byteorder == "little", "Low-precision constant export requires a little-endian host")
            count, width = math.prod(shaped.shape), DTYPES[dtype][1]
            if not count:
                return b""
            get_pointer = ctypes.pythonapi.PyCapsule_GetPointer
            get_pointer.argtypes = [ctypes.py_object, ctypes.c_char_p]
            get_pointer.restype = ctypes.c_void_p
            handle = _MlirAttribute(get_pointer(dense._CAPIPtr, b"iree.compiler.ir.Attribute._CAPIPtr"))
            _library, reader = _dense_raw_reader()
            data = ctypes.string_at(reader(handle), width if dense.is_splat else count * width)
            return data * count if dense.is_splat else data
        return np.asarray(ir.DenseElementsAttr(attr)).tobytes()
    # CompositeAttr has no Python accessor in IREE 3.11. Parse only its constant
    # child-attribute syntax, then delegate each child back to MLIR's parser.
    text = str(attr)
    match = re.fullmatch(r"#util\.composite<(\d+)xi8,\s*\[(.*)\]>", text, re.S)
    check(match is not None, f"Unsupported constant serialization: {text[:100]}")
    parts, start, depth, quoted, escaped = [], 0, 0, False, False
    body = match[2]
    for index, char in enumerate(body):
        if quoted:
            if char == '"' and not escaped:
                quoted = False
            escaped = char == "\\" and not escaped
        elif char == '"':
            quoted = True
        elif char in "<[({":
            depth += 1
        elif char in ">])}":
            depth -= 1
        elif char == "," and depth == 0:
            if body[start:index].strip():
                parts.append(body[start:index].strip())
            start = index + 1
    if body[start:].strip():
        parts.append(body[start:].strip())
    result = b"".join(constant_bytes(ir.Attribute.parse(part)) for part in parts)
    check(len(result) == int(match[1]), "Composite constant byte size mismatch")
    return result


def kernel_identity(package_name, ptx, entry):
    return package_name + "::" + hashlib.sha256(ptx).hexdigest() + "::" + entry


class StaticHalExporter:
    def __init__(self, module, name, target, input_names=None, output_names=None, state_names=None):
        self.module = module
        self.target = target
        self.globals = {}
        self.executables = {}
        self.functions = {}
        self.steps = 0
        self.operation_counts = Counter()
        self.input_names = input_names or []
        self.output_names = output_names or []
        self.state_names = set(state_names or [])
        self.package = {"version": 2, "name": name, "modules": [], "buffers": [],
                        "tensors": [], "kernels": [], "commands": [], "state_slots": [],
                        "producer": {"name": "ArdaIreeExport", "iree": importlib.metadata.version("iree-base-compiler"),
                                     "contract": "static-hal-v2", "runtime_dependencies": ["CUDA driver"]}}
        self.build_identity = "iree-base-compiler=" + self.package["producer"]["iree"]

    def allocate(self, size, initializer=b"", name=None, initialize_zero=True):
        check(isinstance(size, int) and 0 < size < 2**40, "Unsupported static allocation size")
        index = len(self.package["buffers"])
        self.package["buffers"].append({"name": name or f"buffer_{index}", "byte_size": size,
                                        "initializer": initializer})
        # CUDA binding metadata distinguishes read-only from mutable, not exact
        # output footprints. Define the whole mutable arena before its first use
        # so conservative read/write graph hazards never consume unproduced bytes.
        # Caller-owned tensor inputs are initialized by the enclosing graph.
        if initialize_zero and not initializer:
            self.package["commands"].append({"kind": "fill", "buffer": index,
                                             "offset": 0, "size": size, "pattern": b"\0"})
        return Buffer(index, 0, size)

    def executable(self, op):
        name = attr_string(op.attributes["sym_name"])
        binaries = [child.operation for block in op.regions[0].blocks for child in block.operations
                    if child.operation.name == "hal.executable.binary"]
        check(len(binaries) == 1, "Exactly one CUDA variant is required")
        binary = binaries[0]
        check(attr_string(binary.attributes["format"]) == "cuda-nvptx-fb", "Non-CUDA executable")
        flat = FlatBuffer(constant_bytes(binary.attributes["data"]))
        module_indices = []
        for item in flat.vector(flat.root, 1):
            image = flat.string(item, 0)
            check(image.startswith(b"//") or b".version" in image[:200], "Expected PTX, not an opaque binary")
            actual_target = re.search(rb"(?m)^\s*\.target\s+sm_(\d+)(?=[,\s]|$)", image)
            check(actual_target is not None and int(actual_target[1]) == self.target,
                  "Compiler emitted a different CUDA target; refusing to relabel the artifact")
            module_indices.append(len(self.package["modules"]))
            self.package["modules"].append({"format": 0, "target_sm": self.target,
                                             "build_identity": self.build_identity, "image": image})
        exports = []
        for item in flat.vector(flat.root, 0):
            entry = flat.string(item, 1).decode("utf-8")
            block_field = flat.field(item, 2)
            check(block_field != 0, "Missing block dimensions")
            block = [flat.read("I", block_field + axis * 4) for axis in range(3)]
            shared = flat.integer(item, 3)
            constants = flat.integer(item, 4)
            bindings = [flat.read("Q", slot) for slot in flat.vector(item, 5, stride=8, tables=False)]
            check(all(flags & ~3 == 0 for flags in bindings), "Unknown CUDA binding flags")
            arguments, offset = [], 0
            for index, flags in enumerate(bindings):
                arguments.append({"name": f"buffer_{index}", "kind": 1, "access": 0 if flags & 1 else 2,
                                  "offset": offset, "size": 8, "alignment": 8})
                offset += 8
            for index in range(constants):
                arguments.append({"name": f"constant_{index}", "kind": 0, "access": 0,
                                  "offset": offset, "size": 4, "alignment": 4})
                offset += 4
            index = len(self.package["kernels"])
            module_index = flat.integer(item, 0)
            check(module_index < len(module_indices), "Invalid executable module ordinal")
            kernel_name = kernel_identity(self.package["name"],
                self.package["modules"][module_indices[module_index]]["image"], entry)
            self.package["kernels"].append({"name": kernel_name, "entry": entry,
                "module": module_indices[module_index], "parameter_size": offset,
                "parameter_alignment": 8 if bindings else 4, "arguments": arguments})
            exports.append({"index": index, "block": block, "shared": shared,
                            "bindings": len(bindings), "constants": constants})
        self.executables[name + "::" + attr_string(binary.attributes["sym_name"])] = exports

    def export(self, entry):
        initializers = []
        for view in self.module.body.operations:
            op = view.operation
            if op.name == "hal.executable":
                self.executable(op)
            elif op.name == "util.func":
                self.functions[attr_string(op.attributes["sym_name"])] = op
            elif op.name == "util.global":
                check("initial_value" not in op.attributes, "Global initial values need explicit lowering")
                self.globals[attr_string(op.attributes["sym_name"])] = None
            elif op.name == "util.initializer":
                initializers.append(op)
            else:
                raise ExportError("Unsupported module operation: " + op.name)
        for op in initializers:
            self.execute(op, [])
        check(entry in self.functions, f"Entry function {entry!r} was not found")
        function = self.functions[entry]
        tensors = []
        for index, arg in enumerate(function.regions[0].blocks[0].arguments):
            check(str(arg.type) == "!hal.buffer_view", "Entry arguments must be tensor buffer views")
            name = self.input_names[index] if index < len(self.input_names) else f"input{index}"
            tensors.append(Tensor(Buffer(-1), [], "", name))
        result = self.execute(function, tensors)
        for tensor in tensors:
            if tensor.name in self.state_names:
                check(tensor.dtype in DTYPES and tensor.buffer.index >= 0, "Unresolved state tensor type or shape")
                dtype, width = DTYPES[tensor.dtype]
                self.package["state_slots"].append({"name": tensor.name, "dtype": dtype,
                    "buffer": tensor.buffer.index, "offset": tensor.buffer.offset,
                    "shape": tensor.shape, "strides": contiguous_strides(tensor.shape),
                    "byte_size": math.prod(tensor.shape) * width})
            else:
                self.port(tensor, tensor.name, 1)
        for index, tensor in enumerate(result):
            check(isinstance(tensor, Tensor), "Entry results must be tensor buffer views")
            name = self.output_names[index] if index < len(self.output_names) else f"output{index}"
            self.port(tensor, name, 2)
        self.package["producer"]["evaluated_host_operations"] = dict(sorted(self.operation_counts.items()))
        return self.package

    def port(self, tensor, name, role):
        check(tensor.dtype in DTYPES and tensor.shape, "Unresolved tensor type or shape")
        dtype, width = DTYPES[tensor.dtype]
        self.package["tensors"].append({"name": name, "role": role, "dtype": dtype,
            "buffer": tensor.buffer.index, "offset": tensor.buffer.offset,
            "shape": tensor.shape, "strides": contiguous_strides(tensor.shape),
            "byte_size": math.prod(tensor.shape) * width})

    def execute(self, function, arguments, depth=0):
        check(depth < 32, "Recursive host function calls are unsupported")
        block = function.regions[0].blocks[0]
        values = dict(zip(block.arguments, arguments))
        while True:
            next_block = None
            for view in block.operations:
                op = view.operation
                self.steps += 1
                check(self.steps < 1_000_000, "Host program exceeded static evaluation bound")
                args = [values[value] for value in op.operands]
                name = op.name
                self.operation_counts[name] += 1
                if name == "util.return":
                    return args
                if name in ("cf.br", "cf.cond_br"):
                    if name == "cf.br":
                        next_block, branch_args = op.successors[0], args
                    else:
                        sizes = list(ir.DenseI32ArrayAttr(op.attributes["operandSegmentSizes"]))
                        select = 0 if args[0] else 1
                        branch_args = args[1:1 + sizes[1]] if select == 0 else args[1 + sizes[1]:]
                        next_block = op.successors[select]
                    check(len(branch_args) == len(next_block.arguments), "Invalid branch operands")
                    values.update(zip(next_block.arguments, branch_args))
                    break
                result = self.evaluate(op, args, depth)
                if len(op.results) == 1:
                    result = [result]
                elif not op.results:
                    result = []
                check(len(result) == len(op.results), "Host result count mismatch: " + name)
                for value, output in zip(result, op.results):
                    if isinstance(value, int) and str(output.type) != "i1" and name.startswith("arith."):
                        width = 64 if str(output.type) == "index" else ir.IntegerType(output.type).width
                        check(-(1 << (width - 1)) <= value < (1 << (width - 1)),
                              "Host integer overflow or narrowing changes semantics: " + name)
                values.update(zip(op.results, result))
            if next_block is None:
                raise ExportError("Host block does not terminate")
            block = next_block

    def evaluate(self, op, args, depth):
        name, attrs = op.name, op.attributes
        if name.startswith("hal.") and "flags" in attrs:
            check(ir.IntegerAttr(attrs["flags"]).value == 0, "Unsupported HAL flags: " + name)
        if name.startswith("hal.device.queue."):
            check(args[0] == "cuda-device" and args[1] == -1, "Unsupported queue device or affinity")
        if name == "arith.constant":
            value = attrs["value"]
            if isinstance(value, ir.BoolAttr):
                return bool(value.value)
            check(isinstance(value, ir.IntegerAttr), "Non-integer host arithmetic is unsupported")
            return ir.IntegerAttr(value).value
        if name == "util.null":
            return None
        if name == "util.cmp.eq":
            return args[0] == args[1]
        if name == "arith.cmpi":
            pred = ir.IntegerAttr(attrs["predicate"]).value
            check(pred < 6, "Unsigned host comparisons need explicit lowering")
            a, b = args
            return [a == b, a != b, a < b, a <= b, a > b, a >= b][pred]
        if name == "arith.select":
            return args[1] if args[0] else args[2]
        if name in ("arith.addi", "arith.subi", "arith.muli", "arith.andi", "arith.ori", "arith.divui", "arith.ceildivui"):
            a, b = args
            if name == "arith.addi": return a + b
            if name == "arith.subi": return a - b
            if name == "arith.muli": return a * b
            if name == "arith.andi": return a & b
            if name == "arith.ori": return a | b
            check(a >= 0 and b > 0, "Invalid unsigned host division")
            return a // b if name == "arith.divui" else (a + b - 1) // b
        if name in ("arith.index_cast", "arith.index_castui", "arith.extui", "arith.extsi"):
            if name in ("arith.index_castui", "arith.extui"):
                check(args[0] >= 0, "Unsigned host extension of a negative value needs explicit lowering")
            return args[0]
        if name == "util.global.load":
            return self.globals[attr_string(attrs["global"])]
        if name == "util.global.store":
            self.globals[attr_string(attrs["global"])] = args[0]
            return
        if name == "util.call":
            callee = attr_string(attrs["callee"])
            check(callee in self.functions, f"External host call rejected: {callee}")
            result = self.execute(self.functions[callee], args, depth + 1)
            return result[0] if len(op.results) == 1 else result
        if name == "util.status.check_ok":
            check(args[0] == 0, attr_string(attrs["message"]))
            return
        if name == "hal.devices.count": return 1
        if name == "hal.devices.get":
            check(args[0] == 0, "Only a single CUDA device is supported")
            return "cuda-device"
        if name == "hal.device.query":
            category, key = attr_string(attrs["category"]), attr_string(attrs["key"])
            check((category, key) in (("hal.device.id", "cuda"), ("hal.executable.format", "cuda-nvptx-fb")),
                  f"Unsupported device query: {category}/{key}")
            return [True, True]
        if name == "hal.executable.create":
            target = str(attrs["executable_target"]).replace("@", "")
            check(target in self.executables, "Missing embedded CUDA executable: " + target)
            return self.executables[target]
        if name in ("hal.buffer_usage", "hal.memory_type"):
            return ir.IntegerAttr(attrs["usage" if name.endswith("usage") else "type"]).value
        if name == "hal.element_type":
            dtype = str(attrs["type"])
            check(dtype in DTYPES, "Unsupported tensor dtype: " + dtype)
            return dtype
        if name == "hal.encoding_type":
            check("encoding" not in attrs, "Only default dense row-major tensor encoding is supported")
            return "dense"
        if name == "hal.device.allocator": return "cuda-allocator"
        if name in ("hal.fence.create", "hal.fence.join"): return "fence"
        if name == "hal.fence.await": return 0
        if name == "hal.fence.signal": return
        if name == "util.buffer.constant": return constant_bytes(attrs["value"])
        if name == "hal.allocator.import":
            raw, offset, size = args[4:7]
            check(isinstance(raw, bytes) and offset >= 0 and size > 0 and offset + size <= len(raw), "Invalid weight import")
            return [True, self.allocate(size, raw[offset:offset + size], "weights_" + str(len(self.package["buffers"])))]
        if name in ("hal.allocator.allocate", "hal.device.queue.alloca"):
            return self.allocate(args[-1])
        if name == "hal.device.queue.dealloca": return
        if name == "hal.buffer_view.assert":
            tensor, dtype, encoding, *shape = args
            check(isinstance(tensor, Tensor) and encoding == "dense", "Unsupported tensor view")
            check(all(isinstance(x, int) and x > 0 for x in shape), "Dynamic/empty tensor dimensions")
            if tensor.buffer.index >= 0:
                check(tensor.shape == shape and tensor.dtype == dtype, "Tensor assertion mismatch")
            else:
                tensor.shape, tensor.dtype = shape, dtype
                tensor.buffer = self.allocate(math.prod(shape) * DTYPES[dtype][1], name=tensor.name, initialize_zero=False)
            return
        if name == "hal.buffer_view.buffer": return args[0].buffer
        if name == "hal.buffer.assert":
            buffer, allocator, minimum = args
            extent = buffer.length if buffer.length is not None else self.package["buffers"][buffer.index]["byte_size"] - buffer.offset
            check(buffer.index >= 0 and 0 <= minimum <= extent,
                  "Buffer assertion mismatch")
            return
        if name == "hal.buffer_view.create":
            buffer, offset, length, dtype, encoding, *shape = args
            extent = buffer.length if buffer.length is not None else self.package["buffers"][buffer.index]["byte_size"] - buffer.offset
            check(offset >= 0 and length > 0 and offset + length <= extent, "Output view exceeds parent buffer")
            check(encoding == "dense" and math.prod(shape) * DTYPES[dtype][1] == length,
                  "Output tensor size mismatch")
            return Tensor(Buffer(buffer.index, buffer.offset + offset, length), shape, dtype)
        if name == "hal.buffer.subspan":
            buffer, offset, length = args
            extent = buffer.length if buffer.length is not None else self.package["buffers"][buffer.index]["byte_size"] - buffer.offset
            check(offset >= 0 and length >= 0 and offset + length <= extent,
                  "Buffer subspan out of range")
            return Buffer(buffer.index, buffer.offset + offset, length)
        if name == "hal.command_buffer.create":
            check(ir.IntegerAttr(attrs["modes"]).value == 1 and
                  ir.IntegerAttr(attrs["command_categories"]).value & ~3 == 0 and
                  args == ["cuda-device", -1], "Unsupported command buffer mode, categories or affinity")
            return []
        if name in ("hal.command_buffer.execution_barrier", "hal.command_buffer.finalize"):
            if name.endswith("execution_barrier"):
                check(all(ir.IntegerAttr(attrs[key]).value & ~31 == 0
                          for key in ("source_stage_mask", "target_stage_mask")), "Unsupported execution barrier stage")
            return
        if name == "hal.command_buffer.fill_buffer":
            commands, buffer, offset, size, pattern = args
            bits = ir.IntegerType(op.operands[-1].type).width
            check(bits in (8, 16, 32), "Unsupported fill pattern type")
            commands.append({"kind": "fill", "buffer": buffer.index, "offset": buffer.offset + offset,
                             "size": size, "pattern": (pattern % (1 << bits)).to_bytes(bits // 8, "little")})
            return
        if name == "hal.command_buffer.copy_buffer":
            commands, source, source_offset, destination, destination_offset, size = args
            commands.append({"kind": "copy", "source": source.index, "source_offset": source.offset + source_offset,
                             "destination": destination.index, "destination_offset": destination.offset + destination_offset,
                             "size": size})
            return
        if name == "hal.command_buffer.dispatch":
            check(ir.IntegerAttr(attrs["flags"]).value == 0, "Unsupported dispatch flags")
            sizes = list(ir.DenseI32ArrayAttr(attrs["operandSegmentSizes"]))
            check(len(sizes) == 10 and sizes[:6] == [1] * 6, "Unsupported dispatch operand ABI")
            commands, executable, ordinal, x, y, z = args[:6]
            check(0 <= ordinal < len(executable), "Invalid export ordinal")
            kernel = executable[ordinal]
            check(sizes[6] == kernel["constants"] and sizes[7:] == [kernel["bindings"]] * 3,
                  "Kernel bindings disagree with compiler dispatch")
            scalars = args[6:6 + sizes[6]]
            start, count = 6 + sizes[6], sizes[7]
            buffers, offsets, lengths = args[start:start + count], args[start + count:start + 2 * count], args[start + 2 * count:]
            bindings = [{"buffer": buf.index, "offset": buf.offset + off, "range": length, "value": b""}
                        for buf, off, length in zip(buffers, offsets, lengths)]
            bindings += [{"buffer": 0xFFFFFFFF, "offset": 0, "range": 0, "value": struct.pack("<I", val % 2**32)} for val in scalars]
            commands.append({"kind": "dispatch", "kernel": kernel["index"], "grid": [x, y, z],
                             "block": kernel["block"], "shared_memory": kernel["shared"], "arguments": bindings})
            return
        if name == "hal.device.queue.execute":
            self.package["commands"].extend(args[4:][0])
            check(len(args) == 5, "Multiple submitted command buffers need explicit lowering")
            return
        raise ExportError(f"Unsupported host operation {name}: {op.get_asm(use_local_scope=True)[:200]}")


def externalize_onnx_state(model, state_names=None):
    """Lift selected initializers to runtime arguments before any compiler folding.

    Exporter-produced ONNX identifies persistent state in arda.state_dict metadata.
    For generic ONNX every initializer is state by default; callers may explicitly
    retain structural constants via state_names when their values define the graph.
    The returned checkpoint retains the initializer's exact storage representation.
    """
    import onnx
    from onnx import numpy_helper
    check(not model.graph.sparse_initializer, "Sparse ONNX state needs explicit dense lowering")
    initializers = {item.name: item for item in model.graph.initializer}
    check(len(initializers) == len(model.graph.initializer), "Duplicate ONNX initializer names")
    if state_names is None:
        metadata = [item.value for item in model.metadata_props if item.key == "arda.state_dict"]
        check(len(metadata) <= 1, "Duplicate arda.state_dict ONNX metadata")
        try:
            state_names = json.loads(metadata[0]) if metadata else list(initializers)
        except (TypeError, ValueError) as error:
            raise ExportError("Invalid arda.state_dict ONNX metadata") from error
    check(isinstance(state_names, (list, tuple)) and all(isinstance(name, str) and name for name in state_names),
          "State names must be a list of nonempty ONNX initializer names")
    check(len(set(state_names)) == len(state_names), "Duplicate state names")
    check(set(state_names) <= initializers.keys(),
          f"State names missing from ONNX initializers: {sorted(set(state_names) - initializers.keys())}")
    # Stable ONNX enum values mapped to EArdaRHIDataType. Never infer BF16/FP8
    # types from their integer storage dtype or convert through Python floats.
    dtypes = {1: 12, 2: 3, 3: 2, 4: 5, 5: 4, 6: 6, 7: 8, 9: 1,
              10: 10, 11: 13, 12: 7, 13: 9, 14: 16, 15: 17, 16: 11, 17: 14, 19: 15}
    state = {"version": 1, "entries": []}
    inputs = [item for item in model.graph.input if item.name not in initializers]
    for name in state_names:
        tensor = initializers[name]
        check(tensor.data_type in dtypes, f"Unsupported ONNX state dtype for {name}: {tensor.data_type}")
        state["entries"].append({"name": name, "dtype": dtypes[tensor.data_type],
            "shape": list(tensor.dims), "data": numpy_helper.to_array(tensor).tobytes()})
        inputs.append(onnx.helper.make_tensor_value_info(name, tensor.data_type, list(tensor.dims)))
    selected = set(state_names)
    retained = [item for item in model.graph.initializer if item.name not in selected]
    del model.graph.initializer[:]
    model.graph.initializer.extend(retained)
    del model.graph.input[:]
    model.graph.input.extend(inputs)
    validate_state_dict(state)
    return state


def _publish_model(package, state, output, state_output):
    """Stage both artifacts before publication and restore prior files on failure."""
    output.parent.mkdir(parents=True, exist_ok=True)
    state_output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".arda-export-", dir=output.parent) as model_directory, \
            tempfile.TemporaryDirectory(prefix=".arda-state-", dir=state_output.parent) as state_directory:
        staged_model = Path(model_directory) / "model.ardagpu"
        staged_state = Path(state_directory) / "model.ardastate"
        write_package(package, staged_model)
        write_state_dict(state, staged_state, package)
        files = [(staged_model.with_suffix(".json"), output.with_suffix(".json")),
                 (staged_state, state_output), (staged_model, output)]
        backups, committed = {}, []
        # Back up every destination before changing any of them. The descriptor
        # is published last, once its checkpoint and inspection metadata exist.
        for index, (staged, destination) in enumerate(files):
            if destination.exists():
                backup = staged.parent / f"previous-{index}"
                shutil.copyfile(destination, backup)
                backups[destination] = backup
        try:
            for staged, destination in files:
                staged.replace(destination)
                committed.append(destination)
        except OSError:
            for destination in reversed(committed):
                if destination in backups:
                    backups[destination].replace(destination)
                else:
                    destination.unlink()
            raise


def compile_model(source, output, target, entry=None, keep_ir=False, *, state_names=None, state_output=None):
    version = importlib.metadata.version("iree-base-compiler")
    check(version == "3.11.0", f"Exporter requires audited IREE 3.11.0, found {version}")
    cuda_options = target_options(target)
    source, output = Path(source).resolve(), Path(output).resolve()
    state_output = (Path(state_output) if state_output is not None else output.with_suffix(".ardastate")).resolve()
    destinations = [output, output.with_suffix(".json"), state_output]
    check(len(set(destinations)) == len(destinations),
          "Descriptor, metadata JSON and state dict must use different paths")
    check(source not in destinations, "Export outputs cannot overwrite the model source")
    work = output.parent / (output.stem + "-compiler")
    reserved = {work / name for name in ("model.onnx", "input.mlir", "fp8-input.mlir", "hal.mlir",
                                        "global.mlir", "fp8-converted.mlir")}
    check(source not in reserved and not any(path in reserved for path in destinations),
          "Export paths cannot alias compiler intermediate files")
    work.mkdir(parents=True, exist_ok=True)
    scripts = Path(sys.executable).parent
    suffix = ".exe" if sys.platform == "win32" else ""
    input_names, output_names = [], []
    state = {"version": 1, "entries": []}
    temporary_files = []
    if source.suffix.lower() == ".onnx":
        import onnx
        model = onnx.load(source, load_external_data=True)
        validate_onnx_precision(model, target)
        entry = entry or model.graph.name or "main_graph"
        state = externalize_onnx_state(model, state_names)
        input_names = [item.name for item in model.graph.input]
        output_names = [item.name for item in model.graph.output]
        structural = work / "model.onnx"
        onnx.save_model(model, structural, save_as_external_data=False)
        temporary_files.append(structural)
        imported = work / "input.mlir"
        run([str(scripts / ("iree-import-onnx" + suffix)), str(structural), "-o", str(imported)])
        temporary_files.append(imported)
    else:
        check(not state_names, "MLIR state names require explicit tensor argument metadata; use ONNX for named state")
        imported = source
        entry = entry or "main"
    with ir.Context() as context:
        context.allow_unregistered_dialects = True
        module = ir.Module.parse(imported.read_bytes())
        precision_types = validate_mlir_precision(module, target)
        if lower_onnx_fp8_casts(module):
            imported = work / "fp8-input.mlir"
            imported.write_text(str(module), encoding="utf-8")
            temporary_files.append(imported)
    lowered = work / "hal.mlir"
    has_fp8 = bool(precision_types & {"f8E4M3FN", "f8E5M2"})
    resume_options = []
    if has_fp8:
        # 3.11 runs this pass in its ROCm pipeline but omits it in NVVM.
        # Its existing bit-arithmetic lowering retains FP8 tensor/storage
        # types and precise conversion semantics; no inference runtime needed.
        optimized, converted = work / "global.mlir", work / "fp8-converted.mlir"
        run([str(scripts / ("iree-compile" + suffix)), str(imported), *cuda_options,
             "--compile-to=global-optimization", "-o", str(optimized)])
        with ir.Context():
            expected_repairs = fp8_conversion_counts(ir.Module.parse(optimized.read_bytes()))
        import iree.compiler._mlir_libs as libs
        optimizer = Path(libs.__file__).parent / ("iree-opt" + suffix)
        run([str(optimizer), str(optimized),
             "--pass-pipeline=builtin.module(util.func(iree-convert-unsupported-float-arith{enable-ext-trunc-emulation=true}))",
             "-o", str(converted)])
        with ir.Context():
            converted_module = ir.Module.parse(converted.read_bytes())
            repair_pinned_fp8_conversion(converted_module, expected_repairs)
            converted.write_text(str(converted_module), encoding="utf-8")
        temporary_files.extend((optimized, converted))
        imported = converted
        resume_options = ["--compile-from=global-optimization"]
    run([str(scripts / ("iree-compile" + suffix)), str(imported), *cuda_options,
         *resume_options,
         "--compile-to=hal", "--iree-hal-memoization=false",
         "--iree-hal-indirect-command-buffers=false", "--iree-execution-model=async-internal", "-o", str(lowered)])
    with ir.Context():
        module = ir.Module.parse(lowered.read_bytes())
        package = StaticHalExporter(module, output.stem, target, input_names, output_names,
                                    [entry["name"] for entry in state["entries"]]).export(entry)
    package["producer"]["source_sha256"] = hashlib.sha256(source.read_bytes()).hexdigest()
    package["producer"]["lowered_hal_sha256"] = hashlib.sha256(lowered.read_bytes()).hexdigest()
    package["producer"]["entry"] = entry
    package["producer"]["compile_target"] = f"sm_{target}"
    if target in _EXPLICIT_TARGETS:
        package["producer"]["target_profile"] = "arda-conservative-simt-v1"
        package["producer"]["ptx_version"] = _EXPLICIT_TARGETS[target]
    if has_fp8:
        package["producer"]["fp8_conversion"] = "iree-bit-arithmetic-corrected-v1"
    validate_state_dict(state, package)
    _publish_model(package, state, output, state_output)
    if not keep_ir:
        # Only remove the exact files written by this invocation.
        lowered.unlink()
        for temporary in temporary_files:
            temporary.unlink()
        if not any(work.iterdir()):
            work.rmdir()
    return package


def run(command):
    result = subprocess.run(command, capture_output=True, text=True)
    check(result.returncode == 0, "Offline compiler failed:\n" + result.stderr[-16000:])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--target-sm", type=int, default=80, help="Exact audited CUDA compilation target; no architecture substitution")
    parser.add_argument("--entry", help="Defaults to the ONNX graph name, or main for MLIR")
    parser.add_argument("--keep-ir", action="store_true")
    parser.add_argument("--state-output", type=Path, help="Checkpoint output; defaults to the descriptor stem with .ardastate")
    parser.add_argument("--state-name", action="append", dest="state_names",
                        help="ONNX initializer to keep replaceable; repeat to select state explicitly")
    args = parser.parse_args()
    try:
        package = compile_model(args.source, args.output, args.target_sm, args.entry, args.keep_ir,
                                state_names=args.state_names, state_output=args.state_output)
        print(f"Exported {args.output}: {len(package['kernels'])} kernels, {len(package['commands'])} commands, {len(package['buffers'])} buffers")
    except (ExportError, ir.MLIRError) as error:
        parser.exit(2, f"Export rejected: {error}\n")


if __name__ == "__main__":
    main()
