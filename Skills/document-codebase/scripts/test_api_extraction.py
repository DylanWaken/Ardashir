"""Regression checks for public declaration extraction and Doxygen contracts."""
import builtins
import tempfile
import unittest
from pathlib import Path, PurePosixPath, PureWindowsPath
from unittest.mock import patch

from sync_api_inventories import backend_specs, make_symbols, preserve_symbol_ids, rdg_specs, reconcile_backend_sources, select_missing


class HeaderOrderingTest(unittest.TestCase):
    def test_header_inventory_order_is_independent_of_path_flavour(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory)
            cases = (
                (backend_specs, "Source/ArdaInfra/ArdaBackend/Public", (
                    "RHI/Config/ArdaConfig.h", "RHI/CUDA/ArdaKernel.cuh",
                    "RHI/Context/ArdaContext.h", "ArdaBackend.h",
                )),
                (rdg_specs, "Source/ArdaInfra/ArdaRenderGraph/Public", (
                    "ArdaGraph.h", "ArdaGPU.h", "ArdaGraph/ArdaNode.h",
                )),
            )
            for discover, public_root, names in cases:
                for name in names:
                    header = repo / public_root / name
                    header.parent.mkdir(parents=True, exist_ok=True)
                    header.write_text("#pragma once\n", encoding="utf-8")
                expected = sorted(f"{public_root}/{name}" for name in names)
                for flavour in (PureWindowsPath, PurePosixPath):
                    def platform_sorted(items, *, key=None, reverse=False):
                        values = list(items)
                        if key is None and values and isinstance(values[0], Path):
                            key = lambda path: flavour(path.as_posix())
                        return builtins.sorted(values, key=key, reverse=reverse)

                    with self.subTest(module=public_root, platform=flavour.__name__):
                        # Simulate both pathlib comparison rules on every CI host.
                        with patch("sync_api_inventories.sorted", platform_sorted, create=True):
                            self.assertEqual([source for source, _, _ in discover(repo)], expected)


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
                self.assertEqual(by_name[name]["kind"], "operator" if name == "operator<" else "method")
                self.assertEqual(by_name[name]["sourceLine"], line)
            self.assertEqual(
                by_name["MakeCheck"]["signature"],
                "eastl::function<FArdaRHIStatus(const IArdaRHIDevice&)> MakeCheck() const",
            )


class SourceOwnershipTest(unittest.TestCase):
    def test_container_changes_keep_unique_anchors_without_guessing_overloads(self):
        old = {"symbols": [
            {"id": "published-read", "qualifiedName": "arda::Read", "kind": "function",
             "signature": "bool Read(std::vector<uint8_t>& Bytes)"},
            {"id": "published-copy", "qualifiedName": "arda::Copy", "kind": "function",
             "signature": "void Copy(int Value)"},
        ]}
        new = [
            {"id": "new-read", "qualifiedName": "arda::Read", "kind": "function",
             "signature": "bool Read(eastl::vector<uint8_t>& Bytes)"},
            {"id": "new-copy-int", "qualifiedName": "arda::Copy", "kind": "function",
             "signature": "void Copy(int Value)"},
            {"id": "new-copy-long", "qualifiedName": "arda::Copy", "kind": "function",
             "signature": "void Copy(long Value)"},
        ]
        preserve_symbol_ids(new, old)
        self.assertEqual([item["id"] for item in new], ["published-read", "published-copy", "new-copy-long"])

    def test_split_headers_keep_anchors_without_duplicates_or_deleted_fields(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "Buffer.h").write_text(
                "namespace arda { struct Buffer { void Resize(unsigned Size = 0); }; }\n",
                encoding="utf-8",
            )
            (root / "Texture.h").write_text(
                "namespace arda { struct Texture { bool operator==(const Texture& Other) const; }; }\n",
                encoding="utf-8",
            )
            declarations = make_symbols(root, [("Buffer.h", "arda", "resources"),
                ("Texture.h", "arda", "resources")], "test")
            api = {"symbols": [
                {"id": "existing-resize", "name": "Resize", "qualifiedName": "arda::Buffer::Resize",
                 "kind": "method", "signature": "void Resize(unsigned Size)", "source": "Old.h"},
                {"id": "existing-equality", "name": "operator==", "qualifiedName": "arda::Texture::operator==",
                 "kind": "operator", "signature": "bool operator==(const Texture& Other) const { return true; }",
                 "source": "Old.h"},
                {"id": "removed-field", "name": "OldField", "qualifiedName": "arda::Texture::OldField",
                 "kind": "member variable", "signature": "bool OldField", "source": "Old.h"},
            ]}
            reconcile_backend_sources(api, declarations, root)
            self.assertEqual([item["source"] for item in api["symbols"]], ["Buffer.h", "Texture.h"])
            self.assertEqual([item["id"] for item in api["symbols"]], ["existing-resize", "existing-equality"])
            missing = select_missing(declarations, api, ["Buffer.h", "Texture.h"])
            self.assertFalse({"existing-resize", "existing-equality"} & {item["id"] for item in missing})


if __name__ == "__main__":
    unittest.main()
