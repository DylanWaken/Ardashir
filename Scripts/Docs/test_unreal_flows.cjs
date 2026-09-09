/* Exercise the published operation DAGs and their scene/function references. */
const {test} = require('node:test');
const assert = require('node:assert/strict');
global.window = {};
require('../../Docs/Unreal/guide.js');
require('../../Docs/Unreal/scene-guide.js');
require('../../Docs/Unreal/stage-details.js');
const {layout} = require('../../Docs/Unreal/operation-flow.js');
const guide = window.UnrealGuide;
const scene = window.UnrealSceneGuide;
const details = window.UnrealStageDetails;

test('every timeline stage has a connected, source-backed operation flow and valid scene links', () => {
  assert.deepEqual(new Set(Object.keys(details.stages)), new Set(guide.stages.map(s => s.id)));
  const sections = new Set(scene.sections.map(s => s.id));
  for (const [id, stage] of Object.entries(details.stages)) {
    const calls = new Map(stage.calls.map(c => [c.id, c]));
    const nodes = new Set(stage.flow.nodes.map(n => n.id));
    assert.equal(nodes.size, stage.flow.nodes.length, id);
    for (const node of stage.flow.nodes) {
      assert.ok(node.callIds.length, id + '/' + node.id);
      for (const callId of node.callIds) assert.ok(calls.has(callId));
      assert.ok(stage.flow.edges.some(e => e.source === node.id || e.target === node.id));
    }
    for (const edge of stage.flow.edges) assert.ok(nodes.has(edge.source) && nodes.has(edge.target));
    for (const call of calls.values()) {
      assert.ok(call.source.line > 0 && call.role && call.when);
      assert.ok(details.files.some(file => file.path === call.source.path && /^[a-f0-9]{64}$/.test(file.sha256)));
    }
    for (const section of stage.sceneSections) assert.ok(sections.has(section));
  }
});

test('diagram layout keeps every node inside its canvas without overlapping nodes or backward edges', () => {
  for (const stage of Object.values(details.stages)) {
    const result = layout(stage.flow);
    const boxes = [...result.placed.values()];
    for (const box of boxes) {
      assert.ok(box.x >= 0 && box.y >= 0);
      assert.ok(box.x + box.width <= result.width && box.y + box.height <= result.height);
    }
    for (let i = 0; i < boxes.length; i++) for (let j = i+1; j < boxes.length; j++) {
      const a = boxes[i], b = boxes[j];
      assert.ok(a.x+a.width <= b.x || b.x+b.width <= a.x || a.y+a.height <= b.y || b.y+b.height <= a.y);
    }
    for (const edge of stage.flow.edges) assert.ok(result.placed.get(edge.source).column < result.placed.get(edge.target).column);
  }
});

test('DAG layout accepts shuffled input but rejects cycles and missing endpoints', () => {
  const nodes = [{id:'end'}, {id:'start'}, {id:'middle'}];
  const edges = [{source:'start', target:'middle'}, {source:'middle', target:'end'}];
  const result = layout({nodes, edges});
  assert.equal(result.placed.get('end').column, 2);
  assert.throws(() => layout({nodes, edges:[...edges, {source:'end', target:'start'}]}), /cycle/);
  assert.throws(() => layout({nodes, edges:[{source:'absent', target:'end'}]}), /unresolved/);
});

test('Nanite recovery can bypass current-HZB and post culling; trace and shadow alternatives are parallel branches', () => {
  const flow = details.stages['nanite-visibility'].flow;
  const decision = flow.nodes.find(n => n.label === 'Two-pass enabled?');
  const choices = flow.edges.filter(e => e.source === decision.id);
  assert.deepEqual(new Set(choices.map(e => e.label)), new Set(['yes', 'no']));
  assert.equal(flow.nodes.find(n => n.id === choices.find(e => e.label === 'no').target).label, 'Export visibility / depth');
  for (const [id, gates] of [['trace-scene',['software','hardware']], ['shadows',['vsm','conventional']]]) {
    const stage = details.stages[id];
    const result = layout(stage.flow);
    const branches = gates.map(gate => stage.flow.nodes.find(n => n.gate === gate));
    assert.equal(result.placed.get(branches[0].id).column, result.placed.get(branches[1].id).column);
  }
});

test('composition names its actual direct producer and DOF stays before the temporal upscaler', () => {
  const composite = details.stages.composite;
  const direct = composite.flow.nodes.find(n => n.label === 'Direct-lit scene ready');
  assert.ok(direct.callIds.some(id => composite.calls.find(c => c.id === id).name === 'RenderLights'));
  const post = details.stages.post;
  const dof = post.flow.nodes.find(n => n.callIds.some(id => post.calls.find(c => c.id === id).name === 'DiaphragmDOF::AddPasses'));
  const temporal = post.flow.nodes.find(n => n.callIds.some(id => post.calls.find(c => c.id === id).name === 'AddMainTemporalSuperResolutionPasses'));
  const result = layout(post.flow);
  assert.ok(result.placed.get(dof.id).column < result.placed.get(temporal.id).column);
});

test('scene requirements retain resource policies and source enum aliases without sentinel classifications', () => {
  for (const section of scene.sections) for (const item of section.items) assert.ok(item.text && item.fields.length && item.work.length);
  for (const target of Object.values(scene.typeSections)) assert.ok(scene.sections.some(s => s.id === target));
  const blend = scene.enums.find(e => e.name === 'EBlendMode').values;
  assert.ok(blend.includes('BLEND_TranslucentGreyTransmittance = BLEND_Translucent'));
  assert.ok(blend.includes('BLEND_ColoredTransmittanceOnly = BLEND_Modulate'));
  for (const enumeration of scene.enums) assert.ok(!enumeration.values.some(value => /\b\w+_MAX\b|\bPT_Num\b/.test(value)));
  assert.ok(scene.enums.find(e => e.name === 'EMaterialDomain').values.includes('MD_RuntimeVirtualTexture'));
});
