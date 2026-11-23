"""Closed GPU package transport shared by the offline exporter and inspection tools."""

from __future__ import annotations

import hashlib
import copy
import json
import math
import os
import re
import struct
from pathlib import Path

from ArdaNativeKernelPackage import read_library, write_library


class ExportError(ValueError):
    """The input cannot be represented by the closed export contract."""


def check(condition, message):
    if not condition:
        raise ExportError(message)


MAX_ARCHIVE_BYTES = 512 * 1024 * 1024


def read_archive(path):
    with Path(path).open("rb") as stream:
        size = os.fstat(stream.fileno()).st_size
        check(size <= MAX_ARCHIVE_BYTES, "Archive exceeds the 512 MiB limit")
        data = stream.read(size)
        check(len(data) == size and not stream.read(1), "Archive changed while reading")
        return data


def decode_text(data, field):
    try:
        return data.decode("utf-8")
    except UnicodeDecodeError as error:
        raise ExportError(f"Invalid UTF-8 in {field}") from error


def contiguous_strides(shape):
    strides = []
    extent = 1
    for dim in reversed(shape):
        strides.append(extent)
        extent *= dim
    return list(reversed(strides))


def validate_package(package):
    """Check complete bindings and reject unresolved device-library references."""
    from ArdaDTypes import dtype_info
    check(package.get("version") in (1, 2, 3), "Unsupported package version")
    check(package["commands"], "Package has no executable work")
    check(isinstance(package["name"], str) and package["name"], "Model name must be nonempty")
    for items in (package["buffers"], package["tensors"]):
        names = [item["name"] for item in items]
        check(all(isinstance(name, str) and name for name in names) and len(names) == len(set(names)),
              "Buffer and tensor names must be nonempty and unique within their respective tables")
    for module in package["modules"]:
        check(module["format"] == 0, "Only audited PTX modules are currently exported")
        ptx = decode_text(module["image"], "module PTX")
        target = re.search(r"(?m)^\s*\.target\s+sm_(\d+)(?=\s|,|$)", ptx)
        check(target is not None and int(target[1]) == module["target_sm"],
              "PTX target does not match the declared package architecture")
        check(".address_size 64" in ptx, "Only 64-bit CUDA pointers are supported")
        check(all(".shared" in declaration for declaration in re.findall(r"\.extern\b[^;{]+", ptx)),
              "PTX contains an unresolved external function or global")
        check(not re.search(r"\b(?:vprintf|__assertfail|cudaLaunchDevice|cudaGetParameterBuffer)\b", ptx),
              "PTX requires a device runtime service outside the export contract")
    for buffer in package["buffers"]:
        check(0 < buffer["byte_size"] < 2**63, "Invalid buffer size")
        check(len(buffer["initializer"]) in (0, buffer["byte_size"]),
              "Buffer initializer must cover the complete buffer")
    for kernel in package["kernels"]:
        check(0 <= kernel["module"] < len(package["modules"]), "Invalid kernel module")
        ptx = decode_text(package["modules"][kernel["module"]]["image"], "module PTX")
        match = re.search(r"\.entry\s+" + re.escape(kernel["entry"]) + r"\s*\((.*?)\)", ptx, re.S)
        check(match is not None, "Kernel entry is absent from embedded PTX")
        parameters = re.findall(r"\.param\s+\.(?:u|b)(32|64)\b", match[1])
        check(len(parameters) == len(kernel["arguments"]) and
              [int(width) // 8 for width in parameters] == [arg["size"] for arg in kernel["arguments"]],
              "PTX parameter types disagree with exported launch ABI")
        end = 0
        for arg in kernel["arguments"]:
            check(arg["alignment"] in (1, 2, 4, 8, 16), "Unsupported argument alignment")
            check(arg["offset"] >= end and arg["offset"] % arg["alignment"] == 0,
                  "Invalid packed argument layout")
            check(arg["kind"] in (0, 1) and arg["access"] in (0, 1, 2), "Invalid argument kind/access")
            check(arg["size"] == (8 if arg["kind"] else 4), "Unsupported kernel argument ABI")
            end = arg["offset"] + arg["size"]
        check(end <= kernel["parameter_size"], "Kernel parameter storage is too small")
    slots = package.get("state_slots", [])
    immutable = {index for index, buffer in enumerate(package["buffers"]) if buffer["initializer"]}
    immutable.update(slot["buffer"] for slot in slots)
    for command in package["commands"]:
        kind = command["kind"]
        if kind == "dispatch":
            check(0 <= command["kernel"] < len(package["kernels"]), "Invalid dispatch kernel")
            kernel = package["kernels"][command["kernel"]]
            check(len(command["arguments"]) == len(kernel["arguments"]), "Dispatch argument count mismatch")
            check(len(command["grid"]) == len(command["block"]) == 3, "CUDA launches must have three dimensions")
            check(all(0 < x < 2**32 for x in command["grid"] + command["block"]), "Invalid launch dimensions")
            check(math.prod(command["block"]) <= 1024, "CUDA block is too large")
            for schema, arg in zip(kernel["arguments"], command["arguments"]):
                if schema["kind"] == 1:
                    validate_range(package, arg["buffer"], arg["offset"], arg["range"])
                    check(schema["access"] == 0 or arg["buffer"] not in immutable, "Commands cannot write state slots or initialized buffers")
                    check(not arg["value"], "Buffer arguments cannot contain scalar data")
                else:
                    check(arg["buffer"] == 0xFFFFFFFF and arg["offset"] == arg["range"] == 0 and
                          len(arg["value"]) == schema["size"],
                          "Invalid scalar argument")
        elif kind == "fill":
            validate_range(package, command["buffer"], command["offset"], command["size"])
            check(command["buffer"] not in immutable, "Commands cannot write state slots or initialized buffers")
            check(len(command["pattern"]) in (1, 2, 4), "Unsupported fill pattern")
            check(command["size"] % len(command["pattern"]) == 0, "Unaligned fill size")
            check(command["offset"] % len(command["pattern"]) == 0, "Unaligned fill offset")
        elif kind == "copy":
            validate_range(package, command["source"], command["source_offset"], command["size"])
            validate_range(package, command["destination"], command["destination_offset"], command["size"])
            check(command["destination"] not in immutable, "Commands cannot write state slots or initialized buffers")
            check(command["source"] != command["destination"] or
                  command["source_offset"] + command["size"] <= command["destination_offset"] or
                  command["destination_offset"] + command["size"] <= command["source_offset"],
                  "Overlapping copies are unsupported")
        else:
            raise ExportError(f"Unsupported command: {kind}")
    for tensor in package["tensors"]:
        check(tensor["role"] in (0, 1, 2, 3), "Invalid package tensor role")
        check(not tensor["role"] or tensor["buffer"] not in immutable,
              "Immutable storage cannot serve as a public tensor port")
        info = dtype_info(tensor["dtype"])
        check(tensor["shape"] and all(isinstance(x, int) and x > 0 for x in tensor["shape"]),
              "Only nonempty static tensors are supported")
        check(tensor["strides"] == contiguous_strides(tensor["shape"]), "Only dense tensor ports are supported")
        check(tensor["byte_size"] == math.prod(tensor["shape"]) * info.byte_size,
              "Tensor byte size disagrees with dtype and shape")
        check(tensor["offset"] % info.byte_size == 0, "Tensor offset is not aligned to its dtype")
        validate_range(package, tensor["buffer"], tensor["offset"], tensor["byte_size"])
    check(package["version"] >= 2 or not slots, "State slots require descriptor version 2")
    names, buffers = set(), set()
    for slot in slots:
        check(isinstance(slot["name"], str) and slot["name"] and slot["name"] not in names,
              "State slot names must be nonempty and unique")
        names.add(slot["name"])
        info = dtype_info(slot["dtype"])
        check(all(type(dim) is int and dim > 0 for dim in slot["shape"]), "State shape must be static")
        check(slot["strides"] == contiguous_strides(slot["shape"]), "State slots must be dense")
        check(slot["byte_size"] == math.prod(slot["shape"]) * info.byte_size,
              "State byte size disagrees with dtype and shape")
        validate_range(package, slot["buffer"], slot["offset"], slot["byte_size"])
        buffer = package["buffers"][slot["buffer"]]
        check(slot["buffer"] not in buffers and slot["offset"] == 0 and slot["byte_size"] == buffer["byte_size"],
              "State slots require independent whole buffers")
        check(not buffer["initializer"], "Model descriptors cannot embed state dict values")
        check(not any(tensor["buffer"] == slot["buffer"] for tensor in package["tensors"]),
              "State slots cannot alias public tensor ports; return a computed copy of state")
        buffers.add(slot["buffer"])


def validate_range(package, index, offset, size):
    check(0 <= index < len(package["buffers"]), "Invalid buffer index")
    check(0 <= offset and 0 < size and offset + size <= package["buffers"][index]["byte_size"],
          f"Buffer {index} range [{offset}, {offset + size}) is out of bounds")


def _archive_string(value):
    value = value.encode("utf-8")
    return struct.pack("<Q", len(value)) + value


def _pack_model_kernels(package, output):
    """Give each kernel an ABI signature and put its implementation in an immutable sidecar."""
    records, schemas = [], []
    digest = hashlib.sha256()
    for kernel in package["kernels"]:
        schema = bytearray(struct.pack("<IIQ", kernel["parameter_size"], kernel["parameter_alignment"],
                                       len(kernel["arguments"])))
        for arg in kernel["arguments"]:
            schema.extend(_archive_string(arg["name"]))
            schema.extend(struct.pack("<BBIII", arg["access"], arg["kind"], arg["offset"],
                                      arg["size"], arg["alignment"]))
        signature = hashlib.sha256(schema).hexdigest()
        module = package["modules"][kernel["module"]]
        record = ("cuda", kernel["name"], signature, "ptx", kernel["entry"],
                  (module["target_sm"] << 1,), module["image"])
        for value in record[:5]:
            digest.update(_archive_string(value))
        digest.update(struct.pack("<I", record[5][0]))
        digest.update(hashlib.sha256(record[6]).digest())
        records.append(record)
        schemas.append(schema)
    library = "model-" + digest.hexdigest()
    if records:
        write_library(output.parent / (library + ".ardakernels"), library, records)
    definitions = []
    for record, schema in zip(records, schemas):
        provider, name, signature = record[:3]
        definitions.append(struct.pack("<I", 2) + _archive_string(name) + struct.pack("<I", 1)
                           + b"".join(map(_archive_string, (library, provider, name, signature))) + schema)
    return definitions, [library + ".ardakernels"] if records else []


def write_package(package, output):
    """Write fixed-width little-endian .ardagpu and an inspectable JSON companion."""
    validate_package(package)
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    data = bytearray(b"ARDAGPU\0")

    def number(fmt, value):
        data.extend(struct.pack("<" + fmt, value))

    def blob(value):
        number("Q", len(value))
        data.extend(value)

    def string(value):
        blob(value.encode("utf-8"))

    definitions, libraries = _pack_model_kernels(package, output)
    number("I", 3)
    string(package["name"])
    number("Q", len(package["buffers"]))
    for buffer in package["buffers"]:
        string(buffer["name"])
        number("Q", buffer["byte_size"])
        blob(buffer["initializer"])
    number("Q", len(package["tensors"]))
    for tensor in package["tensors"]:
        string(tensor["name"])
        number("I", tensor["buffer"])
        number("B", tensor["role"])
        number("H", tensor["dtype"])
        number("Q", len(tensor["shape"]))
        for dim in tensor["shape"]:
            number("Q", dim)
        number("Q", len(tensor["strides"]))
        for stride in tensor["strides"]:
            number("q", stride)
        number("Q", tensor["offset"])
    number("Q", len(definitions))
    for definition in definitions:
        blob(definition)
    number("Q", len(package["commands"]))
    for command in package["commands"]:
        kind = command["kind"]
        number("B", {"dispatch": 0, "fill": 1, "copy": 2}[kind])
        if kind == "dispatch":
            number("I", command["kernel"])
            for dim in command["grid"] + command["block"]:
                number("I", dim)
            number("I", command["shared_memory"])
            number("Q", len(command["arguments"]))
            for arg in command["arguments"]:
                number("I", arg["buffer"])
                number("Q", arg["offset"])
                number("Q", arg["range"])
                blob(arg["value"])
        elif kind == "fill":
            number("I", command["buffer"])
            number("Q", command["offset"])
            number("Q", command["size"])
            blob(command["pattern"])
        else:
            number("I", command["source"])
            number("Q", command["source_offset"])
            number("I", command["destination"])
            number("Q", command["destination_offset"])
            number("Q", command["size"])
    number("Q", len(package.get("state_slots", [])))
    for slot in package.get("state_slots", []):
        string(slot["name"])
        number("I", slot["buffer"])
        number("H", slot["dtype"])
        number("Q", len(slot["shape"]))
        for dim in slot["shape"]:
            number("Q", dim)
        number("Q", len(slot["strides"]))
        for stride in slot["strides"]:
            number("q", stride)
        number("Q", slot["offset"])
    temporary = output.with_suffix(output.suffix + ".tmp")
    check(len(data) <= MAX_ARCHIVE_BYTES, "Package exceeds Arda graph archive's current 512 MiB limit")
    temporary.write_bytes(data)
    temporary.replace(output)

    def inspect(value):
        if isinstance(value, bytes):
            return {"byte_size": len(value), "sha256": hashlib.sha256(value).hexdigest(),
                    **({"hex": value.hex()} if len(value) <= 32 else {})}
        if isinstance(value, dict):
            return {key: inspect(item) for key, item in value.items()}
        if isinstance(value, list):
            return [inspect(item) for item in value]
        return value

    output.with_suffix(".json").write_text(json.dumps(dict(inspect(package), version=3, kernel_libraries=libraries), indent=2) + "\n", encoding="utf-8")


class _ArchiveReader:
    def __init__(self, data):
        self.data = data
        self.cursor = 0

    def number(self, fmt):
        size = struct.calcsize("<" + fmt)
        check(self.cursor + size <= len(self.data), "Truncated package")
        result = struct.unpack_from("<" + fmt, self.data, self.cursor)[0]
        self.cursor += size
        return result

    def count(self):
        result = self.number("Q")
        check(result <= len(self.data) - self.cursor, "Invalid package length")
        return result

    def blob(self):
        size = self.count()
        result = self.data[self.cursor:self.cursor + size]
        self.cursor += size
        return result

    def string(self):
        return decode_text(self.blob(), "package string")


def _read_model_kernel(definition, directory, libraries, modules, module_indices):
    archive = _ArchiveReader(definition)
    version = archive.number("I")
    check(version in (2, 3, 4), "Invalid portable kernel definition")
    name = archive.string()
    check(archive.number("I") == 1, "Unsupported model kernel schema")
    library, provider, key, signature = (archive.string() for _ in range(4))
    check(re.fullmatch(r"[A-Za-z0-9_.-]+", library) and library not in (".", ".."), "Invalid kernel library name")
    kernel = {"name": name, "parameter_size": archive.number("I"), "parameter_alignment": archive.number("I")}
    if version >= 4:
        check(archive.number("B") == 0, "This descriptor needs its application-defined resource converters for replay")
    kernel["arguments"] = []
    for _ in range(archive.count()):
        kernel["arguments"].append({"name": archive.string(), "access": archive.number("B"), "kind": archive.number("B"),
                                    "offset": archive.number("I"), "size": archive.number("I"),
                                    "alignment": archive.number("I")})
        if version >= 3:
            archive.number("I")  # RHI handoff state, only used when recording on a graphics queue.
        if version >= 4:
            graphics_binding, texture_format = archive.number("B"), archive.number("B")
            check(graphics_binding in (0, 1) and texture_format == 0, "Offline CUDA replay requires plain buffer bindings")
    check(archive.cursor == len(definition), "Trailing kernel definition bytes")
    if library not in libraries:
        try:
            declared, records = read_library(directory / (library + ".ardakernels"))
        except (OSError, ValueError) as error:
            raise ExportError(f"Cannot load kernel library {library}: {error}") from error
        check(declared == library, "Native kernel library identity mismatch")
        libraries[library] = {row[:3]: row[3:] for row in records}
    record = libraries[library].get((provider, key, signature))
    check(record is not None, "Missing matching provider/kernel/signature")
    fmt, kernel["entry"], attributes, image = record
    check(provider == "cuda" and fmt == "ptx" and len(attributes) == 1 and attributes[0] % 2 == 0,
          "Offline CUDA replay requires a PTX kernel with one minimum architecture")
    identity = (attributes[0], image)
    if identity not in module_indices:
        module_indices[identity] = len(modules)
        modules.append({"format": 0, "target_sm": attributes[0] >> 1, "image": image, "build_identity": library})
    kernel["module"] = module_indices[identity]
    return kernel


def read_package(path):
    """Read the independent binary artifact with bounded lengths and no compiler import."""
    from ArdaDTypes import dtype_info
    archive = _ArchiveReader(read_archive(path))
    data = archive.data
    number, count, blob, string = archive.number, archive.count, archive.blob, archive.string

    check(data[:8] == b"ARDAGPU\0", "Invalid package magic")
    archive.cursor = 8
    package = {"version": number("I"), "name": string()}
    check(package["version"] in (1, 2, 3), "Unsupported package version")
    package["modules"] = [] if package["version"] == 3 else [{"format": number("B"), "target_sm": number("I"),
                            "build_identity": string(), "image": blob()} for _ in range(count())]
    package["buffers"] = [{"name": string(), "byte_size": number("Q"), "initializer": blob()}
                           for _ in range(count())]
    package["tensors"] = []
    for _ in range(count()):
        tensor = {"name": string(), "buffer": number("I"), "role": number("B"), "dtype": number("H")}
        tensor["shape"] = [number("Q") for _ in range(count())]
        tensor["strides"] = [number("q") for _ in range(count())]
        tensor["offset"] = number("Q")
        tensor["byte_size"] = math.prod(tensor["shape"]) * dtype_info(tensor["dtype"]).byte_size
        package["tensors"].append(tensor)
    package["kernels"] = []
    libraries, module_indices = {}, {}
    for _ in range(count()):
        if package["version"] == 3:
            package["kernels"].append(_read_model_kernel(blob(), Path(path).parent, libraries,
                                                        package["modules"], module_indices))
            continue
        kernel = {"name": string(), "module": number("I"), "entry": string(),
                  "parameter_size": number("I"), "parameter_alignment": number("I")}
        kernel["arguments"] = [{"name": string(), "kind": number("B"), "access": number("B"),
            "offset": number("I"), "size": number("I"), "alignment": number("I")} for _ in range(count())]
        package["kernels"].append(kernel)
    package["commands"] = []
    for _ in range(count()):
        opcode = number("B")
        if opcode == 0:
            command = {"kind": "dispatch", "kernel": number("I"), "grid": [number("I") for _ in range(3)],
                       "block": [number("I") for _ in range(3)], "shared_memory": number("I")}
            command["arguments"] = [{"buffer": number("I"), "offset": number("Q"), "range": number("Q"),
                                     "value": blob()} for _ in range(count())]
        elif opcode == 1:
            command = {"kind": "fill", "buffer": number("I"), "offset": number("Q"),
                       "size": number("Q"), "pattern": blob()}
        elif opcode == 2:
            command = {"kind": "copy", "source": number("I"), "source_offset": number("Q"),
                       "destination": number("I"), "destination_offset": number("Q"), "size": number("Q")}
        else:
            raise ExportError("Unknown package command opcode")
        package["commands"].append(command)
    if package["version"] >= 2:
        package["state_slots"] = []
        for _ in range(count()):
            slot = {"name": string(), "buffer": number("I"), "dtype": number("H")}
            slot["shape"] = [number("Q") for _ in range(count())]
            slot["strides"] = [number("q") for _ in range(count())]
            slot["offset"] = number("Q")
            slot["byte_size"] = math.prod(slot["shape"]) * dtype_info(slot["dtype"]).byte_size
            package["state_slots"].append(slot)
    check(archive.cursor == len(data), "Trailing package bytes")
    validate_package(package)
    return package


def validate_state_dict(state, package=None):
    """Validate a named checkpoint independently, or against a model descriptor."""
    from ArdaDTypes import dtype_info
    check(state.get("version") == 1, "Unsupported state dict version")
    entries = {}
    for entry in state["entries"]:
        name = entry["name"]
        check(isinstance(name, str) and name and name not in entries,
              "State dict names must be nonempty and unique")
        info = dtype_info(entry["dtype"])
        check(all(type(dim) is int and dim > 0 for dim in entry["shape"]), "State shape must be static")
        check(isinstance(entry["data"], bytes) and
              len(entry["data"]) == math.prod(entry["shape"]) * info.byte_size,
              f"State dict byte size disagrees with dtype and shape: {name}")
        entries[name] = entry
    if package is not None:
        validate_package(package)
        check(package["version"] >= 2, "Loading named state requires model descriptor version 2")
        slots = {slot["name"]: slot for slot in package["state_slots"]}
        check(entries.keys() == slots.keys(),
              f"State dict keys mismatch; missing={sorted(slots.keys() - entries.keys())}, "
              f"unexpected={sorted(entries.keys() - slots.keys())}")
        for name, slot in slots.items():
            check(entries[name]["dtype"] == slot["dtype"], f"State dict dtype mismatch: {name}")
            check(entries[name]["shape"] == slot["shape"], f"State dict shape mismatch: {name}")


def write_state_dict(state, output, package=None):
    """Write a portable named checkpoint, containing no kernels or graph commands."""
    validate_state_dict(state, package)
    data = bytearray(b"ARDASTAT" + struct.pack("<IQ", 1, len(state["entries"])))

    def blob(value):
        data.extend(struct.pack("<Q", len(value)))
        data.extend(value)

    for entry in state["entries"]:
        blob(entry["name"].encode("utf-8"))
        data.extend(struct.pack("<HQ", entry["dtype"], len(entry["shape"])))
        for dim in entry["shape"]:
            data.extend(struct.pack("<Q", dim))
        blob(entry["data"])
    check(len(data) <= MAX_ARCHIVE_BYTES, "State dict exceeds the current 512 MiB archive limit")
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    temporary.write_bytes(data)
    temporary.replace(output)


def read_state_dict(path):
    """Read a bounded checkpoint without importing a framework or compiler."""
    data, cursor = read_archive(path), 8
    check(data[:8] == b"ARDASTAT", "Invalid state dict magic")

    def number(fmt):
        nonlocal cursor
        size = struct.calcsize("<" + fmt)
        check(cursor + size <= len(data), "Truncated state dict")
        result = struct.unpack_from("<" + fmt, data, cursor)[0]
        cursor += size
        return result

    def count():
        value = number("Q")
        check(value <= len(data) - cursor, "Invalid state dict length")
        return value

    def blob():
        nonlocal cursor
        size = count()
        result = data[cursor:cursor + size]
        cursor += size
        return result

    state = {"version": number("I"), "entries": []}
    check(state["version"] == 1, "Unsupported state dict version")
    for _ in range(count()):
        try:
            name = blob().decode("utf-8")
        except UnicodeDecodeError as error:
            raise ExportError("Invalid state dict name encoding") from error
        entry = {"name": name, "dtype": number("H"), "shape": [number("Q") for _ in range(count())]}
        entry["data"] = blob()
        state["entries"].append(entry)
    check(cursor == len(data), "Trailing state dict bytes")
    validate_state_dict(state)
    return state


def load_state_dict(package, state):
    """Build independent initialized replay storage; leave the descriptor untouched."""
    if isinstance(state, (str, Path)):
        state = read_state_dict(state)
    validate_state_dict(state, package)
    result = copy.deepcopy(package)
    entries = {entry["name"]: entry for entry in state["entries"]}
    for slot in result["state_slots"]:
        result["buffers"][slot["buffer"]]["initializer"] = entries[slot["name"]]["data"]
    # An initialized in-memory replay package is deliberately a v1 execution
    # bundle; serializing it cannot masquerade as a reusable v2 descriptor.
    result["version"] = 1
    del result["state_slots"]
    return result
