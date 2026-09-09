/* Pure inheritance/filter model, shared by the page and offline regression tests. */
((scope) => {
  "use strict";
  function createHierarchy(definitions) {
    const nodes = new Map(definitions.map(node => [node.id, node]));
    const parents = new Map(), children = new Map();
    for (const node of definitions) {
      parents.set(node.id, [...new Set(node.baseLinks
        .filter(base => base.resolution === "unique" && nodes.has(base.targets[0]))
        .map(base => base.targets[0]))]);
    }
    for (const [id, bases] of parents) for (const base of bases) {
      if (!children.has(base)) children.set(base, []);
      children.get(base).push(id);
    }
    for (const ids of children.values()) ids.sort((a, b) =>
      nodes.get(a).qualified.localeCompare(nodes.get(b).qualified) || nodes.get(a).path.localeCompare(nodes.get(b).path));
    function descendants(root) {
      const found = new Set();
      function visit(id) {
        if (found.has(id)) return;
        found.add(id);
        (children.get(id) || []).forEach(visit);
      }
      visit(root);
      return found;
    }
    function ancestors(id) {
      const found = new Set();
      function visit(current) {
        for (const parent of parents.get(current) || []) if (!found.has(parent)) {
          found.add(parent); visit(parent);
        }
      }
      visit(id);
      return found;
    }
    function filter(root, predicate) {
      const scope = descendants(root);
      const matches = new Set([...scope].filter(id => predicate(nodes.get(id))));
      const visible = new Set(matches);
      for (const id of matches) for (const ancestor of ancestors(id)) if (scope.has(ancestor)) visible.add(ancestor);
      return {matches, visible};
    }
    return {nodes, parents, children, descendants, ancestors, filter};
  }
  if (typeof module !== "undefined" && module.exports) module.exports = {createHierarchy};
  else scope.UnrealHierarchy = {createHierarchy};
})(typeof window === "undefined" ? globalThis : window);
