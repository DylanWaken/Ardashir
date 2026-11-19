"""Regression checks for public declaration extraction and Doxygen contracts."""
import builtins
import tempfile
import unittest
from pathlib import Path, PurePosixPath, PureWindowsPath
from unittest.mock import patch

from sync_api_inventories import COMPLETE_BACKEND_SOURCE_ROOTS, backend_specs, make_symbols, preserve_symbol_ids, rdg_specs, reconcile_backend_sources, select_missing


class HeaderOrderingTest(unittest.TestCase):
    def test_file_contracts_never_inherit_gpu_status_boilerplate(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = "Source/ArdaInfra/ArdaBackend/Public/FileOperations/ArdaFixture.h"
            header = root / source
            header.parent.mkdir(parents=True)
            header.write_text(
                "namespace arda {\n"
                "class FArdaFileFixture {\npublic:\n"
                "/** Reads a file. */\nvoid Read();\n"
                "/** Waits for a file. @errors Invalid waits throw. @threading External callers may wait. */\n"
                "void Wait();\n};\n}\n", encoding="utf-8")
            symbols = make_symbols(root, [(source, "arda", "core")], "test")
            by_name = {symbol["name"]: symbol for symbol in symbols}
            self.assertEqual(by_name["Read"]["errors"], "")
            self.assertEqual(by_name["Read"]["threading"], "")
            self.assertEqual(by_name["Wait"]["errors"], "Invalid waits throw.")
            self.assertEqual(by_name["Wait"]["threading"], "External callers may wait.")

    def test_file_members_do_not_collide_with_other_modules(self):
        source = "Source/ArdaInfra/ArdaBackend/Public/FileOperations/ArdaFileLocation.h"
        declared = {
            "name": "mFunction", "qualifiedName": "arda::FArdaFileFilter::mFunction",
            "kind": "member variable", "source": source,
            "signature": "std::function<void(const char*)> mFunction",
        }
        existing = {
            "name": "mFunction", "qualifiedName": "arda::FArdaLogRecord::mFunction",
            "kind": "member variable", "source": "ArdaLog.h",
            "signature": "const char* mFunction",
        }
        complete = [source] if source.startswith(COMPLETE_BACKEND_SOURCE_ROOTS) else []
        self.assertEqual(select_missing([declared], {"symbols": [existing]}, complete), [declared])

    def test_header_inventory_order_is_independent_of_path_flavour(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory)
            cases = (
                (backend_specs, "Source/ArdaInfra/ArdaBackend/Public", (
                    "RHI/Config/ArdaConfig.h", "RHI/CUDA/ArdaKernel.cuh",
                    "RHI/Context/ArdaContext.h", "ArdaBackend.h",
                )),
                (rdg_specs, "Source/ArdaInfra/ArdaDependencyGraph/Public", (
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
    def test_node_parameter_aggregates_keep_types_fields_and_ownership(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "Parameters.h").write_text(
                "namespace arda {\n"
                "/** Describes a pixel pass. */\n"
                "ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaPixelParameters)\n"
                "    ARDA_NODE_STAGE_INPUT(FArdaGraphFragments, mInput)\n"
                "    /** Texture sampled by the pixel shader. */\n"
                "    ARDA_NODE_SHADER_TEXTURE(mAlbedo,\n"
                "        FArdaPixelShader::FArdaParameters, mSource)\n"
                "    ARDA_NODE_COLOR_TARGET_ACCESS(mColor, EArdaDependencyAccess::ReadWrite)\n"
                "    ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mCount, SelectCount(1, 2))\n"
                "ARDA_END_NODE_PARAMETER_STRUCT()\n"
                "ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaRayParameters)\n"
                "    ARDA_NODE_STAGE_INPUTS(FArdaGraphRayShader, mShaders)\n"
                "ARDA_END_NODE_PARAMETER_STRUCT()\n"
                "struct FArdaAfter { int mValue; };\n"
                "}\n", encoding="utf-8")
            symbols = make_symbols(root, [("Parameters.h", "arda", "core")], "test")
            by_name = {item["name"]: item for item in symbols}
            self.assertEqual(set(by_name), {"FArdaPixelParameters", "mInput", "mAlbedo",
                "mColor", "mCount", "FArdaRayParameters", "mShaders", "FArdaAfter"})
            aggregate = by_name["FArdaPixelParameters"]
            self.assertEqual(aggregate["kind"], "struct")
            self.assertEqual(aggregate["signature"], "struct FArdaPixelParameters")
            self.assertEqual(aggregate["sourceLine"], 3)
            self.assertEqual(aggregate["summary"], "Describes a pixel pass.")
            for name, line in (("mInput", 4), ("mAlbedo", 6), ("mColor", 8), ("mCount", 9)):
                self.assertEqual(by_name[name]["kind"], "member variable")
                self.assertEqual(by_name[name]["qualifiedName"], "arda::FArdaPixelParameters::" + name)
                self.assertEqual(by_name[name]["sourceLine"], line)
            self.assertEqual(by_name["mAlbedo"]["summary"], "Texture sampled by the pixel shader.")
            self.assertNotEqual(by_name["mColor"]["summary"], by_name["mAlbedo"]["summary"])
            self.assertEqual(by_name["mAlbedo"]["signature"],
                "ARDA_NODE_SHADER_TEXTURE(mAlbedo, FArdaPixelShader::FArdaParameters, mSource)")
            self.assertEqual(by_name["mCount"]["signature"],
                "ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mCount, SelectCount(1, 2))")
            self.assertEqual(by_name["mShaders"]["qualifiedName"], "arda::FArdaRayParameters::mShaders")
            self.assertEqual(by_name["FArdaAfter"]["qualifiedName"], "arda::FArdaAfter")

    def test_constexpr_provider_id_array_is_indexed_by_name(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "Provider.h").write_text(
                'namespace arda {\n/** Registry ID. */\n'
                'inline constexpr char ArdaNativeNvapiProviderId[] = "nvapi";\n}\n',
                encoding="utf-8",
            )
            symbols = make_symbols(root, [("Provider.h", "arda", "nvapi")], "native-compute")
            self.assertEqual([(s["qualifiedName"], s["kind"]) for s in symbols],
                             [("arda::ArdaNativeNvapiProviderId", "constant")])

    def test_named_node_callable_is_a_constant_with_its_own_contract(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "Node.h").write_text(
                "namespace arda {\n"
                "class FArdaDemoNode {};\n"
                "/** Records one demo operation through the registered node. */\n"
                "ARDA_DECLARE_GRAPH_NODE(\n"
                "    FArdaDemoNode, DemoOperation);\n"
                "}\n", encoding="utf-8")
            symbols = make_symbols(root, [("Node.h", "arda", "core")], "test")
            callables = [item for item in symbols if item["name"] == "DemoOperation"]
            self.assertEqual(len(callables), 1)
            operation = callables[0]
            self.assertEqual(operation["kind"], "constant")
            self.assertEqual(operation["qualifiedName"], "arda::DemoOperation")
            self.assertEqual(operation["signature"], "extern const FArdaDemoNode DemoOperation")
            self.assertEqual(operation["sourceLine"], 4)
            self.assertEqual(operation["summary"], "Records one demo operation through the registered node.")
            self.assertFalse(any(item["kind"] == "function" for item in symbols))

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


class EnumExtractionTest(unittest.TestCase):
    def test_forward_enum_does_not_consume_the_following_struct(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "Requirements.h").write_text(
                "namespace arda {\n"
                "enum class EForward : uint8_t;\n"
                "struct FRequirement\n"
                "{\n"
                "    eastl::string mName;\n"
                "};\n"
                "enum class ERoute : uint8_t\n"
                "{\n"
                "    Native,\n"
                "    Alternative = 2\n"
                "};\n"
                "}\n", encoding="utf-8")
            symbols = make_symbols(root, [("Requirements.h", "arda", "core")], "test")
            enumerators = [item for item in symbols if item["kind"] == "enumerator"]
            self.assertEqual(
                [(item["qualifiedName"], item["signature"], item["sourceLine"])
                 for item in enumerators],
                [("arda::ERoute::Native", "Native", 9),
                 ("arda::ERoute::Alternative", "Alternative = 2", 10)],
            )
            self.assertTrue(any(item["qualifiedName"] == "arda::FRequirement::mName"
                                and item["kind"] == "member variable" for item in symbols))


class CallableExtractionTest(unittest.TestCase):
    def test_identical_members_in_template_specializations_keep_distinct_scopes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "Codec.h").write_text(
                "namespace arda {\n"
                "template <class T>\n"
                "struct TArdaCodec<T, eastl::enable_if_t<IsIntegral<T>>>\n"
                "{\n"
                "    static void Read(FArchive& Archive, T& Value);\n"
                "    static void Write(FArchive& Archive, const T& Value);\n"
                "};\n"
                "template <class T>\n"
                "struct TArdaCodec<T, eastl::enable_if_t<IsFloating<T>>>\n"
                "{\n"
                "    static void Read(FArchive& Archive, T& Value);\n"
                "    static void Write(FArchive& Archive, const T& Value);\n"
                "};\n"
                "template <>\n"
                "struct TArdaCodec<int>\n"
                "{\n"
                "    TArdaCodec();\n"
                "};\n"
                "}\n", encoding="utf-8")
            symbols = make_symbols(root, [("Codec.h", "arda", "core")], "test")
            methods = [item for item in symbols if item["kind"] == "method"]
            self.assertEqual(len(methods), 4)
            self.assertEqual(len({item["id"] for item in methods}), 4)
            self.assertEqual({item["qualifiedName"] for item in methods}, {
                "arda::TArdaCodec<T, eastl::enable_if_t<IsIntegral<T>>>::Read",
                "arda::TArdaCodec<T, eastl::enable_if_t<IsIntegral<T>>>::Write",
                "arda::TArdaCodec<T, eastl::enable_if_t<IsFloating<T>>>::Read",
                "arda::TArdaCodec<T, eastl::enable_if_t<IsFloating<T>>>::Write",
            })
            constructors = [item for item in symbols if item["kind"] == "constructor"]
            self.assertEqual(len(constructors), 1)
            self.assertEqual(constructors[0]["qualifiedName"], "arda::TArdaCodec<int>::TArdaCodec")

    def test_trailing_return_initializers_are_not_function_bodies(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "Invoke.h").write_text(
                "namespace arda {\n"
                "struct FGraphEdit {\n"
                "    template <class Node, class... Arguments>\n"
                "    auto Invoke(Arguments&&... Values)\n"
                "        -> decltype(typename Node::FArdaParameters{Forward(Values)...}, FOperation{})\n"
                "    { return Record(Values...); }\n"
                "    void After();\n"
                "};\n"
                "struct FExecutionNode {\n"
                "    template <class... Arguments>\n"
                "    static auto Invoke(FGraphEdit& Edit, Arguments&&... Values)\n"
                "        -> decltype(FArdaParameters{Forward(Values)...}, FOperation{});\n"
                "};\n"
                "}\n", encoding="utf-8")
            symbols = make_symbols(root, [("Invoke.h", "arda", "core")], "test")
            by_name = {item["qualifiedName"]: item for item in symbols}
            self.assertEqual(by_name["arda::FGraphEdit::Invoke"]["signature"],
                "template <class Node, class... Arguments> auto Invoke(Arguments&&... Values) "
                "-> decltype(typename Node::FArdaParameters{Forward(Values)...}, FOperation{})")
            self.assertEqual(by_name["arda::FExecutionNode::Invoke"]["signature"],
                "template <class... Arguments> static auto Invoke(FGraphEdit& Edit, Arguments&&... Values) "
                "-> decltype(FArdaParameters{Forward(Values)...}, FOperation{})")
            self.assertEqual(by_name["arda::FGraphEdit::After"]["signature"], "void After()")

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
    def test_new_specialization_at_an_old_source_line_does_not_steal_an_anchor(self):
        common = {"kind": "constant", "signature": "static constexpr bool mSupported = true",
                  "source": "Parameters.h"}
        old = {"symbols": [{**common, "id": "api-arda-tardakey-buffer-msupported-original",
            "qualifiedName": "arda::TArdaKey<Buffer>::mSupported", "sourceLine": 82}]}
        new = [
            {**common, "id": "new-external", "qualifiedName": "arda::TArdaKey<External<T>>::mSupported",
             "sourceLine": 82},
            {**common, "id": "new-buffer", "qualifiedName": "arda::TArdaKey<Buffer>::mSupported",
             "sourceLine": 92},
        ]
        preserve_symbol_ids(new, old)
        self.assertEqual([item["id"] for item in new],
                         ["new-external", "api-arda-tardakey-buffer-msupported-original"])
        corrupted = {"symbols": [{**item, "id": old["symbols"][0]["id"]} for item in new]}
        new[1]["id"] = "new-buffer"
        preserve_symbol_ids(new, corrupted)
        self.assertEqual([item["id"] for item in new],
                         ["new-external", "api-arda-tardakey-buffer-msupported-original"])

    def test_corrected_trailing_returns_keep_overloaded_anchors(self):
        old = {"symbols": [
            {"id": "published-typed", "qualifiedName": "arda::Invoke", "kind": "function",
             "signature": "auto Invoke(FParameters Value) -> FOperation"},
            {"id": "published-positional", "qualifiedName": "arda::Invoke", "kind": "function",
             "signature": "auto Invoke(Arguments&&... Values) -> decltype(FParameters"},
        ]}
        new = [
            {"id": "new-typed", "qualifiedName": "arda::Invoke", "kind": "function",
             "signature": "auto Invoke(FParameters Value) -> FOperation"},
            {"id": "new-positional", "qualifiedName": "arda::Invoke", "kind": "function",
             "signature": "auto Invoke(Arguments&&... Values) -> decltype(FParameters{Forward(Values)...}, FOperation{})"},
            {"id": "new-unrelated", "qualifiedName": "arda::Invoke", "kind": "function",
             "signature": "auto Invoke(int Value) -> decltype(FParameters{Value}, FOperation{})"},
        ]
        preserve_symbol_ids(new, old)
        self.assertEqual([item["id"] for item in new],
            ["published-typed", "published-positional", "new-unrelated"])
        ambiguous = [
            {**new[1], "id": "candidate-one"},
            {**new[1], "id": "candidate-two",
             "signature": "auto Invoke(Arguments&&... Values) -> decltype(FParameters{Other(Values)...}, FOperation{})"},
        ]
        preserve_symbol_ids(ambiguous, old)
        self.assertEqual([item["id"] for item in ambiguous], ["candidate-one", "candidate-two"])

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
