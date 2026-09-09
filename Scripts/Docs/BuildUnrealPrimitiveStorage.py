#!/usr/bin/env python3
"""Resolve class-to-storage explanations independently of the inheritance inventory."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from BuildUnrealGuide import load_js
from UnrealPrimitiveStorage import COMMON, PROFILES, TOPOLOGY


def build(engine, docs):
    primitive = load_js(docs / "primitives.js", "UnrealPrimitives")
    nodes = {node["id"]: node for node in primitive["types"]}
    files = {}

    def lookup(selector):
        name, _, suffix = selector.partition("@")
        matches = [node for node in nodes.values() if node["qualified"] == name and node["path"].endswith(suffix)]
        if len(matches) != 1:
            raise ValueError(f"Non-unique class selector: {selector}: {[(n['path'], n['line']) for n in matches]}")
        return matches[0]["id"]

    def anchored(item):
        result = dict(item)
        path, query = result.pop("sourceQuery")
        raw = (engine / path).read_bytes()
        files[path] = hashlib.sha256(raw).hexdigest()
        matches = [index + 1 for index, line in enumerate(raw.decode("utf-8-sig").splitlines()) if query in line]
        if not matches:
            raise ValueError(f"Missing source anchor: {path} / {query}")
        result["source"] = dict(path=path, line=matches[0], label=query)
        return result

    profiles, direct = [], {}
    for authored in PROFILES:
        item = dict(authored)
        item["classIds"] = [lookup(selector) for selector in item.pop("classes")]
        item["peerIds"] = [lookup(selector) for selector in item.pop("peers")]
        item["resources"] = [anchored(resource) for resource in item["resources"]]
        for id in item["classIds"]:
            if id in direct:
                raise ValueError(f"Duplicate profile mapping: {nodes[id]['qualified']}")
            direct[id] = item["id"]
        profiles.append(item)
    associations = {}
    for node in nodes.values():
        frontier, seen = [node["id"]], set()
        while frontier:
            mapped = [id for id in frontier if id in direct]
            if mapped:
                associations[node["id"]] = [dict(profileId=direct[id], fromId=id) for id in mapped]
                break
            seen.update(frontier)
            frontier = sorted({base["targets"][0] for id in frontier for base in nodes[id]["baseLinks"]
                if base["resolution"] == "unique" and base["targets"][0] in nodes and base["targets"][0] not in seen})
        if node["id"] not in associations:
            raise ValueError(f"No profile for {node['qualified']}")
    return dict(meta=primitive["meta"], profiles=profiles, associations=associations,
        common=[anchored(item) for item in COMMON], topology=anchored(TOPOLOGY),
        files=[dict(path=path, sha256=sha) for path, sha in sorted(files.items())])


def reference(data, primitive):
    nodes = {node["id"]: node for node in primitive["types"]}
    children = {}
    for node in nodes.values():
        for base in node["baseLinks"]:
            if base["resolution"] == "unique" and base["targets"][0] in nodes:
                children.setdefault(base["targets"][0], []).append(node["id"])
    lines = ["# Primitive classes and GPU storage", "",
        f"Unreal Engine {data['meta']['version']}, commit `{data['meta']['commit']}`.", "",
        "Generated from `primitives.js` and authored storage associations. The interactive page is [scene-types.html](scene-types.html).", "",
        "## What each edge means", "",
        "Every indentation below means a direct, uniquely resolved C++ base-class relationship. The two trees start at UPrimitiveComponent and FPrimitiveSceneProxy. Creating a proxy, owning a buffer, and producing a render target are associations, never tree edges. All bases, including interfaces and mixins outside these two roots, appear in the declaration inventory below. A multiply derived class can appear under each applicable primitive base. No class is reparented to make a module group.", "",
        primitive["meta"]["primitiveScope"], "",
        f"Discovered {len(nodes)} primitive class definitions. Class IDs include source locations, so same-name local/conditional definitions remain distinct. The extractor does not model function names as scopes; paths and lines disambiguate local classes.", "",
        "GPU associations below are reviewed explanations for major rendering families, not compiler-extracted ownership or a complete field layout for every optional plugin. Unmapped subclasses show their nearest mapped base profile explicitly as context; overrides may select different resources. Component-to-proxy choices listed are related implementations, not inheritance or a guarantee that every configuration instantiates every listed proxy. CPU wrappers and the data actually resident on the GPU are distinguished.", "",
        "## Actual inheritance trees", ""]

    def branch(id, depth, seen):
        if id in seen:
            raise ValueError("Cycle in primitive inheritance")
        node = nodes[id]
        lines.append("  " * depth + f"- `{node['qualified']}` — `{node['path']}:{node['line']}`")
        for child in sorted(children.get(id, []), key=lambda id: (nodes[id]["qualified"], nodes[id]["path"])):
            branch(child, depth + 1, seen | {id})

    for root in primitive["roots"].values():
        branch(root, 0, set())
        lines.append("")
    lines.extend(["## All declared bases", ""])
    for node in nodes.values():
        lines.extend([f"- `{node['qualified']}` ({node['path']}:{node['line']}) — bases: " + (", ".join(f"`{base}`" for base in node["bases"]) or "none") + "."])
    lines.extend(["", "## Shared GPU storage and CPU boundaries", ""])

    def storage(item):
        source = item["source"]
        lines.extend([f"### {item['name']}", "", "GPU contents: " + item["gpu"], "", "CPU role / conditions: " + item["cpu"], "",
            f"Source: `{source['path']}:{source['line']}` — `{source['label']}`.", ""])

    for item in data["common"]:
        storage(item)
    lines.extend(["## Class-specific storage associations", ""])
    for profile in data["profiles"]:
        lines.extend([f"### {profile['title']} ({profile['id']})", "",
            "Mapped definitions: " + ", ".join(f"`{nodes[id]['qualified']}` ({nodes[id]['path']}:{nodes[id]['line']})" for id in profile["classIds"]) + ".", "",
            profile["role"], "", profile["caveat"], ""])
        if profile["peerIds"]:
            lines.extend(["Related implementations (association only): " + ", ".join(f"`{nodes[id]['qualified']}`" for id in profile["peerIds"]) + ".", ""])
        for item in profile["resources"]:
            storage(item)
    topo = data["topology"]
    lines.extend(["## " + topo["name"], "", topo["text"], "",
        f"Source: `{topo['source']['path']}:{topo['source']['line']}`.", "",
        "## Per-class explanation provenance", ""])
    for node in nodes.values():
        inherited = data["associations"][node["id"]]
        labels = [item["profileId"] + (" (direct mapping)" if item["fromId"] == node["id"] else " (base context from " + nodes[item["fromId"]]["qualified"] + "; subclass storage not separately reviewed)") for item in inherited]
        lines.append(f"- `{node['qualified']}` ({node['path']}:{node['line']}): " + "; ".join(labels) + ".")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine-root", type=Path, default=Path("D:/UnrealEngine"))
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[2] / "Docs/Unreal")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    data = build(args.engine_root, args.output)
    outputs = {"primitive-storage.js": "window.UnrealPrimitiveStorage = " + json.dumps(data, ensure_ascii=False, separators=(",", ":")) + ";\n",
        "primitive-reference.md": reference(data, load_js(args.output / "primitives.js", "UnrealPrimitives"))}
    for filename, value in outputs.items():
        path = args.output / filename
        if args.check:
            if not path.exists() or path.read_text(encoding="utf-8") != value:
                raise SystemExit(f"Stale primitive storage output: {filename}")
        else:
            path.write_text(value, encoding="utf-8", newline="\n")
    print(f"Resolved {len(data['profiles'])} storage profiles and {len(data['associations'])} class associations.")


if __name__ == "__main__":
    main()
