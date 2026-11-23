"""Encode provider kernels in the Arda native kernel library format."""

import os
from pathlib import Path
import re
import struct
import tempfile


def _encode_string(value):
    if not isinstance(value, str) or not value or "\0" in value:
        raise ValueError("Kernel index strings must be nonempty and contain no NUL.")
    data = value.encode("utf-8")
    return struct.pack("<I", len(data)) + data


def _validate_library_name(library):
    if not isinstance(library, str) or not re.fullmatch(r"[A-Za-z0-9_.-]+", library) or library in (".", ".."):
        raise ValueError("Library must be a name containing only letters, digits, underscore, dot or hyphen.")


def write_library(output, library, records):
    """Write records (provider, name, signature, format, entry point, attributes, bytes)."""
    _validate_library_name(library)
    output = Path(output)
    if output.name != f"{library}.ardakernels":
        raise ValueError(f"Output filename must be {library}.ardakernels.")

    entries = []
    keys = set()
    for provider, name, signature, kernel_format, entry_point, attributes, payload in records:
        key = (provider, name, signature)
        index = b"".join(_encode_string(value) for value in (*key, kernel_format, entry_point))
        if key in keys:
            raise ValueError(f"Duplicate kernel key: {key}")
        keys.add(key)
        attributes = tuple(attributes)
        if any(type(value) is not int or not 0 <= value <= 0xFFFFFFFF for value in attributes):
            raise ValueError(f"Attributes must be uint32 values for {key}.")
        if not payload:
            raise ValueError(f"Empty kernel payload for {key}.")
        index += struct.pack(f"<{len(attributes) + 1}I", len(attributes), *attributes)
        entries.append((index, payload))
    if not entries:
        raise ValueError("A library needs at least one kernel.")

    encoded_library = _encode_string(library)
    data_start = 16 + len(encoded_library) + sum(len(index) + 8 for index, _ in entries)
    if data_start + sum(len(payload) for _, payload in entries) > 0x7FFFFFFF:
        raise ValueError("Native kernel libraries must be smaller than 2 GiB.")

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary_path = None
    try:
        with tempfile.NamedTemporaryFile(dir=output.parent, delete=False) as temporary:
            temporary_path = Path(temporary.name)
            temporary.write(b"ARDANK01" + struct.pack("<II", data_start, len(entries)) + encoded_library)
            offset = data_start
            for index, payload in entries:
                temporary.write(index + struct.pack("<II", offset, len(payload)))
                offset += len(payload)
            for _, payload in entries:
                temporary.write(payload)
        os.replace(temporary_path, output)
    finally:
        if temporary_path and temporary_path.exists():
            temporary_path.unlink()


def read_library(path):
    """Read and validate a library ID and its list of kernel records."""
    path = Path(path)
    if not 16 <= path.stat().st_size <= 0x7FFFFFFF:
        raise ValueError("Invalid native kernel library size.")
    data = path.read_bytes()
    if len(data) < 16 or data[:8] != b"ARDANK01":
        raise ValueError("Unrecognized native kernel library.")
    data_start, count = struct.unpack_from("<II", data, 8)
    if not 16 <= data_start <= len(data) or not 0 < count <= (data_start - 16) // 32:
        raise ValueError("Invalid native kernel index.")
    position = 16

    def read_words(count):
        nonlocal position
        byte_count = count * 4
        if byte_count > data_start - position:
            raise ValueError("Truncated native kernel index.")
        values = struct.unpack_from(f"<{count}I", data, position)
        position += byte_count
        return values

    def read_string():
        nonlocal position
        size, = read_words(1)
        if not 0 < size <= data_start - position:
            raise ValueError("Invalid native kernel index string size.")
        value = data[position:position + size].decode("utf-8")
        position += size
        if "\0" in value:
            raise ValueError("Native kernel index strings may not contain NUL.")
        return value

    library = read_string()
    _validate_library_name(library)
    records = []
    keys = set()
    next_offset = data_start
    for _ in range(count):
        provider, name, signature, kernel_format, entry_point = (read_string() for _ in range(5))
        key = (provider, name, signature)
        if key in keys:
            raise ValueError(f"Duplicate kernel key: {key}")
        keys.add(key)
        attribute_count, = read_words(1)
        attributes = read_words(attribute_count)
        offset, size = read_words(2)
        if offset != next_offset or not 0 < size <= len(data) - offset:
            raise ValueError(f"Invalid native kernel payload range for {key}.")
        next_offset = offset + size
        records.append((*key, kernel_format, entry_point, attributes, data[offset:next_offset]))
    if position != data_start or next_offset != len(data):
        raise ValueError("Unexpected native kernel library data.")
    return library, records
