"""Strict reader for the ArdaTrace little-endian capture format."""

from __future__ import annotations

import io
import math
import struct
from pathlib import Path
from typing import BinaryIO, Union


MAGIC = b"ARDATRC1"
VERSION = 1
ENDIAN_MARKER = 0x01020304
MAX_STRING_BYTES = 1024 * 1024


class TraceFormatError(ValueError):
    """Raised when a trace capture is malformed."""


class _Reader:
    def __init__(self, stream: BinaryIO) -> None:
        self.stream = stream
        self.offset = 0

    def read_exact(self, size: int, description: str) -> bytes:
        data = self.stream.read(size)
        if len(data) != size:
            raise TraceFormatError(
                f"Truncated {description} at byte {self.offset}: "
                f"expected {size} bytes, found {len(data)}"
            )
        self.offset += size
        return data

    def unpack(self, fmt: str, description: str):
        size = struct.calcsize(fmt)
        return struct.unpack(fmt, self.read_exact(size, description))

    def read_string(self, description: str) -> str:
        (length,) = self.unpack("<I", f"{description} length")
        if length > MAX_STRING_BYTES:
            raise TraceFormatError(
                f"{description} is {length} bytes; maximum is {MAX_STRING_BYTES}"
            )
        raw = self.read_exact(length, description)
        try:
            return raw.decode("utf-8")
        except UnicodeDecodeError as exc:
            raise TraceFormatError(f"{description} is not valid UTF-8") from exc


def _validate_references(session: dict) -> None:
    names = session["names"]
    threads = session["threads"]
    scopes = session["scopes"]
    scope_ids = {scope["scope_id"] for scope in scopes}

    for kind in ("scopes", "counters", "markers"):
        for record in session[kind]:
            if record["thread_id"] not in threads:
                raise TraceFormatError(
                    f"{kind[:-1].title()} references unknown thread "
                    f"{record['thread_id']}"
                )
            if record["name_id"] not in names:
                raise TraceFormatError(
                    f"{kind[:-1].title()} references unknown name "
                    f"{record['name_id']}"
                )

    for scope in scopes:
        parent_id = scope["parent_scope_id"]
        if parent_id and parent_id not in scope_ids:
            raise TraceFormatError(
                f"Scope {scope['scope_id']} references unknown parent {parent_id}"
            )

    scopes_by_id = {scope["scope_id"]: scope for scope in scopes}
    for scope in scopes:
        seen = {scope["scope_id"]}
        current = scope
        while current["parent_scope_id"]:
            parent = scopes_by_id[current["parent_scope_id"]]
            if parent["scope_id"] in seen:
                raise TraceFormatError(f"Scope parent cycle includes {parent['scope_id']}")
            if parent["thread_id"] != scope["thread_id"]:
                raise TraceFormatError(
                    f"Scope {scope['scope_id']} has a parent on another thread"
                )
            if (
                scope["start_ns"] < parent["start_ns"]
                or scope["end_ns"] > parent["end_ns"]
            ):
                raise TraceFormatError(
                    f"Scope {scope['scope_id']} extends beyond its parent"
                )
            seen.add(parent["scope_id"])
            current = parent


def _normalize(session: dict) -> dict:
    timestamps = []
    for scope in session["scopes"]:
        timestamps.extend((scope["start_ns"], scope["end_ns"]))
    for counter in session["counters"]:
        timestamps.append(counter["timestamp_ns"])
    for marker in session["markers"]:
        timestamps.append(marker["timestamp_ns"])

    origin = session["origin_ns"]
    start_ns = min(timestamps, default=origin)
    end_ns = max(timestamps, default=origin)
    session["start_ns"] = start_ns
    session["end_ns"] = end_ns
    session["duration_ns"] = max(0, end_ns - start_ns)
    session["names"] = [
        {"id": identifier, "label": label}
        for identifier, label in sorted(session["names"].items())
    ]
    session["threads"] = [
        {"id": identifier, "label": label}
        for identifier, label in sorted(session["threads"].items())
    ]
    session["summary"] = {
        "thread_count": len(session["threads"]),
        "name_count": len(session["names"]),
        "scope_count": len(session["scopes"]),
        "counter_count": len(session["counters"]),
        "marker_count": len(session["markers"]),
    }
    return session


def parse_stream(stream: BinaryIO) -> dict:
    """Parse a binary stream and return JSON-serializable normalized trace data."""
    reader = _Reader(stream)
    magic = reader.read_exact(8, "header magic")
    if magic != MAGIC:
        raise TraceFormatError("Invalid trace magic; expected ARDATRC1")

    version, endian_marker, origin_ns = reader.unpack("<IIQ", "header")
    if version != VERSION:
        raise TraceFormatError(f"Unsupported trace version {version}")
    if endian_marker != ENDIAN_MARKER:
        raise TraceFormatError("Invalid endian marker")

    session = {
        "version": version,
        "origin_ns": origin_ns,
        "names": {},
        "threads": {},
        "scopes": [],
        "counters": [],
        "markers": [],
    }
    scope_ids = set()

    while True:
        type_data = stream.read(1)
        if not type_data:
            raise TraceFormatError("Capture is missing the CaptureEnd record")
        reader.offset += 1
        record_type = type_data[0]

        if record_type == 255:
            if stream.read(1):
                raise TraceFormatError("Unexpected data after CaptureEnd record")
            break
        if record_type == 1:
            (identifier,) = reader.unpack("<I", "name id")
            if identifier in session["names"]:
                raise TraceFormatError(f"Duplicate name id {identifier}")
            session["names"][identifier] = reader.read_string("name")
        elif record_type == 2:
            (identifier,) = reader.unpack("<I", "thread id")
            session["threads"][identifier] = reader.read_string("thread name")
        elif record_type == 3:
            values = reader.unpack("<IIQQQQ", "scope payload")
            thread_id, name_id, scope_id, parent_id, start_ns, end_ns = values
            if scope_id == 0:
                raise TraceFormatError("Scope id zero is reserved")
            if scope_id in scope_ids:
                raise TraceFormatError(f"Duplicate scope id {scope_id}")
            if end_ns < start_ns:
                raise TraceFormatError(f"Scope {scope_id} ends before it starts")
            scope_ids.add(scope_id)
            session["scopes"].append(
                {
                    "thread_id": thread_id,
                    "name_id": name_id,
                    "scope_id": scope_id,
                    "parent_scope_id": parent_id,
                    "start_ns": start_ns,
                    "end_ns": end_ns,
                }
            )
        elif record_type == 4:
            thread_id, name_id, timestamp_ns, value = reader.unpack(
                "<IIQd", "counter payload"
            )
            if not math.isfinite(value):
                raise TraceFormatError("Counter value must be finite")
            session["counters"].append(
                {
                    "thread_id": thread_id,
                    "name_id": name_id,
                    "timestamp_ns": timestamp_ns,
                    "value": value,
                }
            )
        elif record_type == 5:
            thread_id, name_id, timestamp_ns = reader.unpack(
                "<IIQ", "marker payload"
            )
            session["markers"].append(
                {
                    "thread_id": thread_id,
                    "name_id": name_id,
                    "timestamp_ns": timestamp_ns,
                }
            )
        else:
            raise TraceFormatError(
                f"Unknown record type {record_type} at byte {reader.offset - 1}"
            )

    _validate_references(session)
    return _normalize(session)


def parse_bytes(data: bytes) -> dict:
    return parse_stream(io.BytesIO(data))


def parse_file(path: Union[str, Path]) -> dict:
    with Path(path).open("rb") as stream:
        return parse_stream(stream)
