"""Offline regression checks for the source extractor and published atlas contracts."""
import unittest
from pathlib import Path

from BuildUnrealCatalog import base_clauses, extract, split_bases
from BuildUnrealGuide import load_js
from BuildUnrealPrimitives import resolve

DOCS = Path(__file__).resolve().parents[2] / "Docs/Unreal"


class ExtractorTests(unittest.TestCase):
    def test_absolute_bases_do_not_resolve_to_a_namespace_shadow(self):
        nodes = extract("class Base {}; namespace Inner { class Base {}; class Child : ::Base {}; }", "fixture.h", "Fixture")
        resolve(nodes)
        self.assertEqual(nodes[-1]["baseLinks"][0]["targets"], [nodes[0]["id"]])

    def test_comments_strings_and_forward_declarations_are_not_types(self):
        source = '''// class Fake {};
        /* struct AlsoFake {}; */
        const char* Text = R"tag(class RawFake {}; )tag";
        class Forward;
        namespace Outer { class API_API Real : public Base { struct Nested {}; }; }
        '''
        nodes = extract(source, "fixture.h", "Fixture")
        self.assertEqual([node["qualified"] for node in nodes], ["Outer::Real", "Outer::Real::Nested"])
        self.assertEqual(nodes[0]["bases"], ["Base"])
        self.assertEqual(nodes[0]["line"], 5)

    def test_multiple_template_bases_keep_inner_commas(self):
        self.assertEqual(split_bases("public Base<A, B>, protected virtual Other"), ["Base<A, B>", "Other"])

    def test_macro_templates_are_not_concrete_declarations(self):
        source = '#define DECLARE(Name) struct Name : Base ' + chr(92) + '\n{ int Value; };\nstruct Actual : Base {};'
        self.assertEqual([node["name"] for node in extract(source, "fixture.h", "Fixture")], ["Actual"])

    def test_scoped_enums_are_not_misidentified_as_classes(self):
        source = "enum class Mode : uint8 { One }; enum struct Flag { Two }; class Actual {};"
        self.assertEqual([node["name"] for node in extract(source, "fixture.h", "Fixture")], ["Actual"])

    def test_conditional_rhi_mixin_does_not_corrupt_primary_base(self):
        clauses = base_clauses("public FRHIViewableResource\n#if ENABLE_RHI_VALIDATION\n, public RHIValidation::FBufferResource\n#endif\n")
        self.assertEqual(clauses, [("FRHIViewableResource", []), ("RHIValidation::FBufferResource", ["#if ENABLE_RHI_VALIDATION"])])

    def test_shader_parameters_retain_namespace_and_enclosing_type(self):
        nodes = extract("namespace A { class Shader { BEGIN_SHADER_PARAMETER_STRUCT(FParameters, ) END_SHADER_PARAMETER_STRUCT() }; }", "fixture.h", "Fixture")
        self.assertEqual(nodes[-1]["qualified"], "A::Shader::FParameters")
        self.assertEqual(nodes[-1]["kind"], "shader parameters")


class PublishedGuideTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = load_js(DOCS / "catalog.js", "UnrealCatalog")
        cls.guide = load_js(DOCS / "guide.js", "UnrealGuide")
        cls.types = {node["id"]: node for node in cls.catalog["types"]}

    def test_inventory_identity_and_links_are_complete(self):
        self.assertEqual(len(self.types), self.catalog["meta"]["declarations"])
        self.assertEqual(self.catalog["meta"], self.guide["meta"])
        for node in self.types.values():
            self.assertGreater(node["line"], 0)
            self.assertTrue(node["path"].startswith("Engine/Source/Runtime/"))
            for base in node["baseLinks"]:
                self.assertNotIn("#", base["name"], (node["qualified"], base["name"]))
                self.assertTrue(set(base["targets"]) <= self.types.keys())
                if base["resolution"] == "unique":
                    self.assertEqual(len(base["targets"]), 1)

    def test_critical_inheritance_and_conditional_bases(self):
        explained = {node["qualified"]: node for node in self.guide["types"]}
        self.assertEqual(explained["FSceneViewState"]["bases"], ["FSceneViewStateInterface", "FRenderResource"])
        self.assertEqual(explained["FViewInfo"]["bases"], ["FSceneView"])
        for name in ("FRHIBuffer", "FRHITexture"):
            self.assertEqual(explained[name]["baseLinks"][0]["name"], "FRHIViewableResource")
            self.assertEqual(explained[name]["baseLinks"][0]["resolution"], "unique")
            self.assertEqual(explained[name]["baseLinks"][0]["conditions"], [])
            self.assertEqual(explained[name]["baseLinks"][1]["conditions"], ["#if ENABLE_RHI_VALIDATION"])

    def test_stages_have_actionable_content_and_acyclic_prerequisites(self):
        stages = {stage["id"]: stage for stage in self.guide["stages"]}
        self.assertEqual(len(stages), len(self.guide["stages"]))
        visited, visiting = set(), set()

        def visit(id):
            self.assertNotIn(id, visiting, "Cycle in pipeline prerequisites")
            if id in visited:
                return
            visiting.add(id)
            for dependency in stages[id]["depends"]:
                self.assertIn(dependency, stages)
                self.assertLess(stages[dependency]["column"], stages[id]["column"])
                visit(dependency)
            visiting.remove(id)
            visited.add(id)

        for stage in stages.values():
            for field in ("summary", "inputs", "outputs", "steps", "operations", "validation", "sources"):
                self.assertTrue(stage[field], (stage["id"], field))
            self.assertTrue(set(stage["typeIds"]) <= self.types.keys())
            visit(stage["id"])

    def test_main_frame_source_order_is_supported_by_anchors(self):
        stages = {stage["id"]: stage for stage in self.guide["stages"]}
        def line(id, query):
            return next(source["line"] for source in stages[id]["sources"] if query in source["label"])
        self.assertLess(line("lumen-lighting", "RenderLumenSceneLighting"), line("gbuffer", "RenderBasePass"))
        self.assertLess(line("gbuffer", "RenderBasePass"), line("decals-ao", "DispatchAsyncLumen"))
        self.assertLess(line("direct", "RenderLights"), line("composite", "bCompositeRegularLumenOnly"))


if __name__ == "__main__":
    unittest.main()
