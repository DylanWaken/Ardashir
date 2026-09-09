/* Test published inheritance and filtering using the same model as the page. */
const {test} = require("node:test");
const assert = require("node:assert/strict");
global.window = {};
require("../../Docs/Unreal/primitives.js");
require("../../Docs/Unreal/primitive-storage.js");
const {createHierarchy} = require("../../Docs/Unreal/tree-model.js");
const data = window.UnrealPrimitives;
const storage = window.UnrealPrimitiveStorage;
const model = createHierarchy(data.types);
const find = (name, suffix = "") => {
  const result = data.types.filter(node => node.qualified === name && node.path.endsWith(suffix));
  assert.equal(result.length, 1, name + " must resolve to one definition");
  return result[0];
};
const chain = name => [...model.ancestors(find(name).id)].map(id => model.nodes.get(id).qualified);

test("every published primitive is reachable from one of the two real class roots", () => {
  const components = model.descendants(data.roots.UPrimitiveComponent);
  const proxies = model.descendants(data.roots.FPrimitiveSceneProxy);
  assert.equal(components.size + proxies.size, data.types.length);
  assert.equal(new Set([...components, ...proxies]).size, data.types.length);
  for (const [parent, children] of model.children) for (const id of children) {
    assert.ok(model.nodes.get(id).baseLinks.some(base => base.resolution === "unique" && base.targets[0] === parent));
  }
  assert.equal(data.unresolved.length, 0);
});

test("component/proxy relationships preserve non-obvious source inheritance", () => {
  assert.deepEqual(chain("UHierarchicalInstancedStaticMeshComponent"), ["UInstancedStaticMeshComponent", "UStaticMeshComponent", "UMeshComponent", "UPrimitiveComponent"]);
  assert.deepEqual(chain("UNiagaraComponent"), ["UFXSystemComponent", "UPrimitiveComponent"]);
  assert.deepEqual(chain("Nanite::FSkinnedSceneProxy"), ["Nanite::FSceneProxyBase", "FPrimitiveSceneProxy"]);
  assert.deepEqual(chain("Nanite::FSceneProxy"), ["Nanite::FSceneProxyBase", "FPrimitiveSceneProxy"]);
  assert.deepEqual(chain("FLandscapeNaniteSceneProxy"), ["Nanite::FSceneProxy", "Nanite::FSceneProxyBase", "FPrimitiveSceneProxy"]);
  assert.ok(find("FGeometryCollectionSceneProxy").bases.includes("FGeometryCollectionSceneProxyBase"));
  assert.ok(find("FNaniteGeometryCollectionSceneProxy").bases.includes("FGeometryCollectionSceneProxyBase"));
  assert.deepEqual(chain("UWaterMeshComponent"), ["UMeshComponent", "UPrimitiveComponent"]);
  assert.deepEqual(chain("UWaterBodyComponent"), ["UPrimitiveComponent"]);
});

test("same-name local and global spline proxy definitions are never merged", () => {
  const local = find("FSplineMeshSceneProxy", "Components/SplineComponent.cpp");
  const global = find("FSplineMeshSceneProxy", "Public/SplineMeshSceneProxy.h");
  assert.notEqual(local.id, global.id);
  assert.deepEqual(local.bases, ["FPrimitiveSceneProxy"]);
  assert.deepEqual(global.bases, ["FStaticMeshSceneProxy", "FSplineMeshSceneProxyCommon"]);
  assert.equal(storage.associations[local.id][0].profileId, "debug");
  assert.equal(storage.associations[global.id][0].profileId, "spline");
});

test("filtering a deep leaf or module preserves the original complete ancestry", () => {
  const target = find("UFoliageInstancedStaticMeshComponent");
  const filtered = model.filter(data.roots.UPrimitiveComponent, node => node.id === target.id);
  assert.equal(filtered.matches.size, 1);
  assert.deepEqual(filtered.visible, new Set([target.id, ...model.ancestors(target.id)]));
  const niagara = model.filter(data.roots.UPrimitiveComponent, node => node.module === "Niagara");
  assert.ok(niagara.visible.has(find("UFXSystemComponent").id));
  assert.ok(!niagara.matches.has(find("UFXSystemComponent").id));
  assert.equal(model.filter(data.roots.UPrimitiveComponent, () => false).visible.size, 0);
});

test("multiple real bases are retained and ambiguous candidate bases create no edges", () => {
  const base = (name, targets, resolution = "unique") => ({name, targets, resolution});
  const node = (id, bases = []) => ({id, qualified:id, path:id + ".h", baseLinks:bases});
  const fixture = createHierarchy([node("Root"), node("A", [base("Root", ["Root"])]), node("B", [base("Root", ["Root"])]), node("Both", [base("A", ["A"]), base("B", ["B"])]), node("Unknown", [base("Alias", ["A", "B"], "ambiguous")])]);
  assert.deepEqual(fixture.parents.get("Both"), ["A", "B"]);
  assert.deepEqual(fixture.parents.get("Unknown"), []);
  assert.equal(fixture.descendants("Root").size, 4);
});

test("storage associations are separate, source-anchored, and explicit about base context", () => {
  const profiles = new Map(storage.profiles.map(profile => [profile.id, profile]));
  for (const node of data.types) {
    assert.ok(storage.associations[node.id].length);
    for (const association of storage.associations[node.id]) {
      assert.ok(profiles.has(association.profileId));
      assert.ok(association.fromId === node.id || model.ancestors(node.id).has(association.fromId));
    }
  }
  const inherited = storage.associations[find("UFoliageInstancedStaticMeshComponent").id][0];
  assert.equal(inherited.fromId, find("UHierarchicalInstancedStaticMeshComponent").id);
  for (const profile of profiles.values()) {
    for (const id of [...profile.classIds, ...profile.peerIds]) assert.ok(model.nodes.has(id));
    for (const resource of profile.resources) {
      assert.ok(resource.gpu && resource.cpu && resource.source.line > 0);
      assert.ok(storage.files.some(file => file.path === resource.source.path));
    }
  }
  assert.equal(find("UWaterMeshComponent").module, "Water");
  assert.equal(find("UGeometryCollectionComponent").module, "GeometryCollectionEngine");
});
