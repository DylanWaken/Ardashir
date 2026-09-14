"""Regression checks for public declaration extraction and Doxygen contracts."""
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


class CallableExtractionTest(unittest.TestCase):
    def test_function_template_types_are_not_methods_but_returning_methods_are(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "Requirements.h").write_text(
                "namespace arda {\n"
                "struct FRequirements\n"
                "{\n"
                "    eastl::function<FArdaRHIStatus(const IArdaRHIDevice&)> mCheck;\n"
                "    using FCheck = eastl::function<FArdaRHIStatus(const IArdaRHIDevice&)>;\n"
                "    eastl::function<FArdaRHIStatus(const IArdaRHIDevice&)> MakeCheck() const;\n"
                "    eastl::vector<eastl::function<FArdaRHIStatus()>> GetChecks() const;\n"
                "    void SetCheck(eastl::function<FArdaRHIStatus()> Check);\n"
                "    bool operator<(const FRequirements&) const;\n"
                "};\n"
                "}\n",
                encoding="utf-8",
            )
            symbols = make_symbols(root, [("Requirements.h", "arda", "core")], "test")
            by_name = {item["name"]: item for item in symbols}
            self.assertEqual(
                set(by_name),
                {"FRequirements", "mCheck", "FCheck", "MakeCheck", "GetChecks", "SetCheck", "operator<"},
            )
            self.assertEqual(by_name["mCheck"]["kind"], "member variable")
            self.assertEqual(by_name["FCheck"]["kind"], "alias")
            for name, line in [("MakeCheck", 6), ("GetChecks", 7), ("SetCheck", 8), ("operator<", 9)]:
                self.assertIn(by_name[name]["kind"], {"method", "conversion operator"})
                self.assertEqual(by_name[name]["sourceLine"], line)
            self.assertEqual(
                by_name["MakeCheck"]["signature"],
                "eastl::function<FArdaRHIStatus(const IArdaRHIDevice&)> MakeCheck() const",
            )


if __name__ == "__main__":
    unittest.main()
