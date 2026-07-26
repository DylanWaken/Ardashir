import struct
import unittest

from trace_reader import (
    ENDIAN_MARKER,
    MAGIC,
    MAX_STRING_BYTES,
    TraceFormatError,
    parse_bytes,
)


def header(origin=1_000):
    return MAGIC + struct.pack("<IIQ", 1, ENDIAN_MARKER, origin)


def text_record(record_type, identifier, text):
    encoded = text.encode("utf-8")
    return bytes([record_type]) + struct.pack("<II", identifier, len(encoded)) + encoded


def valid_capture(name="Render", thread="Main"):
    return b"".join(
        [
            header(),
            text_record(1, 10, name),
            text_record(2, 20, thread),
            b"\x03" + struct.pack("<IIQQQQ", 20, 10, 30, 0, 1_100, 1_600),
            b"\x04" + struct.pack("<IIQd", 20, 10, 1_250, 42.5),
            b"\x05" + struct.pack("<IIQ", 20, 10, 1_400),
            b"\xff",
        ]
    )


class TraceReaderTests(unittest.TestCase):
    def test_parses_and_normalizes_valid_capture(self):
        session = parse_bytes(valid_capture())

        self.assertEqual(session["version"], 1)
        self.assertEqual(session["origin_ns"], 1_000)
        self.assertEqual(session["start_ns"], 1_100)
        self.assertEqual(session["end_ns"], 1_600)
        self.assertEqual(session["duration_ns"], 500)
        self.assertEqual(session["names"], [{"id": 10, "label": "Render"}])
        self.assertEqual(session["threads"], [{"id": 20, "label": "Main"}])
        self.assertEqual(session["summary"]["scope_count"], 1)
        self.assertEqual(session["counters"][0]["value"], 42.5)

    def test_accepts_declarations_after_records(self):
        capture = b"".join(
            [
                header(),
                b"\x05" + struct.pack("<IIQ", 20, 10, 1_400),
                text_record(1, 10, "Late name"),
                text_record(2, 20, "Late thread"),
                b"\xff",
            ]
        )
        self.assertEqual(parse_bytes(capture)["summary"]["marker_count"], 1)

    def test_accepts_thread_name_updates(self):
        capture = b"".join(
            [
                header(),
                text_record(2, 20, "Thread 1"),
                text_record(2, 20, "Render Thread"),
                b"\xff",
            ]
        )
        self.assertEqual(
            parse_bytes(capture)["threads"],
            [{"id": 20, "label": "Render Thread"}],
        )

    def test_rejects_bad_header_fields(self):
        cases = [
            b"NOTTRACE" + struct.pack("<IIQ", 1, ENDIAN_MARKER, 0) + b"\xff",
            MAGIC + struct.pack("<IIQ", 2, ENDIAN_MARKER, 0) + b"\xff",
            MAGIC + struct.pack("<IIQ", 1, 0x04030201, 0) + b"\xff",
        ]
        for capture in cases:
            with self.subTest(capture=capture[:12]):
                with self.assertRaises(TraceFormatError):
                    parse_bytes(capture)

    def test_rejects_unknown_truncated_and_missing_end(self):
        cases = [
            header() + b"\x63",
            header() + b"\x01\x01",
            header(),
            header() + b"\xfftrailing",
        ]
        for capture in cases:
            with self.subTest(length=len(capture)):
                with self.assertRaises(TraceFormatError):
                    parse_bytes(capture)

    def test_rejects_oversized_and_invalid_utf8_strings(self):
        oversized = header() + b"\x01" + struct.pack("<II", 1, MAX_STRING_BYTES + 1)
        invalid_utf8 = header() + b"\x01" + struct.pack("<II", 1, 1) + b"\xff"
        for capture in (oversized, invalid_utf8):
            with self.assertRaises(TraceFormatError):
                parse_bytes(capture)

    def test_rejects_invalid_references(self):
        unknown_thread = b"".join(
            [
                header(),
                text_record(1, 10, "Name"),
                b"\x05" + struct.pack("<IIQ", 99, 10, 5),
                b"\xff",
            ]
        )
        unknown_name = b"".join(
            [
                header(),
                text_record(2, 20, "Thread"),
                b"\x05" + struct.pack("<IIQ", 20, 99, 5),
                b"\xff",
            ]
        )
        unknown_parent = b"".join(
            [
                header(),
                text_record(1, 10, "Name"),
                text_record(2, 20, "Thread"),
                b"\x03" + struct.pack("<IIQQQQ", 20, 10, 30, 999, 1, 2),
                b"\xff",
            ]
        )
        for capture in (unknown_thread, unknown_name, unknown_parent):
            with self.assertRaises(TraceFormatError):
                parse_bytes(capture)

    def test_rejects_scope_end_before_start(self):
        capture = b"".join(
            [
                header(),
                text_record(1, 10, "Name"),
                text_record(2, 20, "Thread"),
                b"\x03" + struct.pack("<IIQQQQ", 20, 10, 30, 0, 20, 10),
                b"\xff",
            ]
        )
        with self.assertRaisesRegex(TraceFormatError, "ends before"):
            parse_bytes(capture)

    def test_rejects_zero_scope_id_and_child_outside_parent(self):
        declarations = (
            header()
            + text_record(1, 10, "Name")
            + text_record(2, 20, "Thread")
        )
        zero_id = (
            declarations
            + b"\x03" + struct.pack("<IIQQQQ", 20, 10, 0, 0, 1, 2)
            + b"\xff"
        )
        outside_parent = (
            declarations
            + b"\x03" + struct.pack("<IIQQQQ", 20, 10, 30, 0, 10, 20)
            + b"\x03" + struct.pack("<IIQQQQ", 20, 10, 31, 30, 5, 15)
            + b"\xff"
        )
        for capture in (zero_id, outside_parent):
            with self.assertRaises(TraceFormatError):
                parse_bytes(capture)

    def test_rejects_parent_cycles_and_cross_thread_parents(self):
        declarations = (
            header()
            + text_record(1, 10, "Name")
            + text_record(2, 20, "Thread A")
            + text_record(2, 21, "Thread B")
        )
        cycle = (
            declarations
            + b"\x03" + struct.pack("<IIQQQQ", 20, 10, 30, 31, 1, 2)
            + b"\x03" + struct.pack("<IIQQQQ", 20, 10, 31, 30, 1, 2)
            + b"\xff"
        )
        cross_thread = (
            declarations
            + b"\x03" + struct.pack("<IIQQQQ", 20, 10, 30, 0, 1, 2)
            + b"\x03" + struct.pack("<IIQQQQ", 21, 10, 31, 30, 1, 2)
            + b"\xff"
        )
        for capture in (cycle, cross_thread):
            with self.assertRaises(TraceFormatError):
                parse_bytes(capture)

    def test_rejects_duplicate_ids_and_nonfinite_counter(self):
        duplicate_name = (
            header()
            + text_record(1, 1, "A")
            + text_record(1, 1, "B")
            + b"\xff"
        )
        nonfinite = b"".join(
            [
                header(),
                text_record(1, 10, "Counter"),
                text_record(2, 20, "Thread"),
                b"\x04" + struct.pack("<IIQd", 20, 10, 1, float("nan")),
                b"\xff",
            ]
        )
        for capture in (duplicate_name, nonfinite):
            with self.assertRaises(TraceFormatError):
                parse_bytes(capture)


if __name__ == "__main__":
    unittest.main()
