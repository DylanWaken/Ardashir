#!/usr/bin/env python3
"""Discover actual primitive-component/proxy inheritance throughout the Unreal checkout."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path

from BuildUnrealCatalog import extract
from BuildUnrealGuide import load_js

ROOTS = ("UPrimitiveComponent", "FPrimitiveSceneProxy")


def resolve(nodes):
    names = {}
    for node in nodes:
        names.setdefault(node["qualified"], []).append(node["id"])
    for node in nodes:
        node["baseLinks"] = []
        for index, base in enumerate(node["bases"]):
            name = re.sub(r"<.*", "", base).strip().removeprefix("::")
            scope = node["qualified"].split("::")[:-1]
            candidates = []
            for length in ([0] if base.strip().startswith("::") else range(len(scope), -1, -1)):
                candidates = names.get("::".join(scope[:length] + [name]), [])
                if candidates:
                    break
            node["baseLinks"].append(dict(name=base, targets=candidates,
                conditions=node.get("baseConditions", [[]] * len(node["bases"]))[index],
                resolution="unique" if len(candidates) == 1 else "ambiguous" if candidates else "external or unparsed"))


def collect(engine: Path, catalog: dict):
    records = {node["id"]: dict(node) for node in catalog["types"]}
    module_files = subprocess.run(["rg", "--files", "--glob", "*.Build.cs", "Engine/Source/Runtime", "Engine/Plugins"],
        cwd=engine, capture_output=True, text=True, encoding="utf-8", check=True).stdout.splitlines()
    modules = {str(Path(path).parent).replace("\\", "/"): Path(path).name.removesuffix(".Build.cs") for path in module_files}
    searched, parsed, manifests = set(), set(), []
    frontier = set(ROOTS)
    # Search declarations mentioning each newly discovered base, rather than indexing
    # every unrelated shader/helper class in all plugin source files.
    while frontier:
        terms = sorted(frontier - searched)
        if not terms:
            break
        searched.update(terms)
        pattern = r"\b(?:class|struct)\b[^;{}]*:\s*[^;{}]*\b(?:" + "|".join(re.escape(term) for term in terms) + r")\b[^;{}]*\{"
        result = subprocess.run(["rg", "-l", "-U", "--glob", "*.h", "--glob", "*.cpp", "--glob", "*.inl",
            "--glob", "!**/ThirdParty/**", "--glob", "!**/Tests/**", "--glob", "!**/Test/**", pattern,
            "Engine/Source/Runtime", "Engine/Plugins"], cwd=engine, capture_output=True, text=True, encoding="utf-8")
        if result.returncode not in (0, 1):
            raise RuntimeError(result.stderr)
        paths = sorted(set(line.replace("\\", "/") for line in result.stdout.splitlines()) - parsed)
        for relative in paths:
            parsed.add(relative)
            raw = (engine / relative).read_bytes()
            module = next((modules[str(parent).replace("\\", "/")] for parent in Path(relative).parents
                if str(parent).replace("\\", "/") in modules), "Unknown module")
            entries = extract(raw.decode("utf-8", errors="replace"), relative, module)
            for node in entries:
                records[node["id"]] = node
            manifests.append(dict(path=relative, sha256=hashlib.sha256(raw).hexdigest()))
        nodes = list(records.values())
        resolve(nodes)
        reachable = {node["id"] for node in nodes if node["qualified"] in ROOTS}
        changed = True
        while changed:
            changed = False
            for node in nodes:
                if node["id"] not in reachable and any(base["resolution"] == "unique" and base["targets"][0] in reachable for base in node["baseLinks"]):
                    reachable.add(node["id"])
                    changed = True
        frontier = {records[id]["name"] for id in reachable} - searched
        print(f"Primitive discovery: {len(reachable)} classes; {len(parsed)} source files inspected; {len(frontier)} new base names", flush=True)
    result = sorted((records[id] for id in reachable), key=lambda node: (node["qualified"], node["path"], node["line"]))
    roots = {name: next(node["id"] for node in result if node["qualified"] == name) for name in ROOTS}
    unresolved = [dict(id=node["id"], name=node["qualified"], path=node["path"], line=node["line"], base=base)
        for node in records.values() if node["id"] not in reachable for base in node["baseLinks"]
        if base["resolution"] == "ambiguous" and set(base["targets"]) & reachable]
    # Also keep resolved non-primitive mixins in an auxiliary reference pool.
    related_ids = {id for node in result for base in node["baseLinks"] for id in base["targets"]} - reachable
    return dict(meta=dict(catalog["meta"], primitiveClasses=len(result), inspectedPrimitiveFiles=len(parsed),
        primitiveScope="Actual explicit class/struct inheritance rooted at UPrimitiveComponent and FPrimitiveSceneProxy, discovered transitively across Engine/Source/Runtime and Engine/Plugins (.h/.cpp/.inl). Includes optional plugins, editor helpers inside plugins, local classes and inactive conditional definitions; excludes ThirdParty, Test and Tests folders. Only uniquely resolved bases become tree edges. Aliases and unsupported declaration macros can remain unparsed; ambiguous descendants are reported separately. This is source-definition coverage, not a promise every class exists or renders in one build."),
        roots=roots, types=result, relatedTypes=[records[id] for id in sorted(related_ids)], unresolved=unresolved,
        files=sorted(manifests, key=lambda item: item["path"]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine-root", type=Path, default=Path("D:/UnrealEngine"))
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[2] / "Docs/Unreal")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    data = collect(args.engine_root, load_js(args.output / "catalog.js", "UnrealCatalog"))
    value = "window.UnrealPrimitives = " + json.dumps(data, ensure_ascii=False, separators=(",", ":")) + ";\n"
    path = args.output / "primitives.js"
    if args.check:
        if path.read_text(encoding="utf-8") != value:
            raise SystemExit("Stale primitive inheritance inventory")
    else:
        path.write_text(value, encoding="utf-8", newline="\n")
    print(f"Wrote {len(data['types'])} actual primitive classes; {len(data['unresolved'])} ambiguous descendants.")


if __name__ == "__main__":
    main()
