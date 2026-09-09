"""Regression checks for macro signatures and adjacent Doxygen contracts."""
import tempfile
import unittest
from pathlib import Path

from sync_api_inventories import make_symbols


class MacroExtractionTest(unittest.TestCase):
    def test_continuation_bodies_do_not_consume_adjacent_macro_contracts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            header = root / "Parameters.h"
            header.write_text(
                '/** Private helper. */\n'
                '#define ARDA_INTERNAL_HELPER(T, Name) \\\n'
                '    using Name##Type = T; \\\n'
                '    Name##Type Name;\n\n'
                '/** Declares a value array. */\n'
                '#define ARDA_VALUE_ARRAY(T, Name, Count) \\\n'
                '    ARDA_INTERNAL_HELPER(T, Name)\n'
                '/** Declares a buffer array. */\n'
                '#define ARDA_BUFFER_ARRAY(Name, Count, Access) \\\n'
                '    ARDA_INTERNAL_HELPER(Buffer, Name)\n', encoding="utf-8")
            symbols = make_symbols(root, [("Parameters.h", "arda", "core")], "test")
            by_name = {item["name"]: item for item in symbols}
            self.assertEqual(set(by_name), {"ARDA_VALUE_ARRAY", "ARDA_BUFFER_ARRAY"})
            for name, signature, summary, line in [
                ("ARDA_VALUE_ARRAY", "#define ARDA_VALUE_ARRAY(T, Name, Count)", "Declares a value array.", 7),
                ("ARDA_BUFFER_ARRAY", "#define ARDA_BUFFER_ARRAY(Name, Count, Access)", "Declares a buffer array.", 10),
            ]:
                self.assertEqual(by_name[name]["signature"], signature)
                self.assertEqual(by_name[name]["summary"], summary)
                self.assertEqual(by_name[name]["sourceLine"], line)


if __name__ == "__main__":
    unittest.main()
