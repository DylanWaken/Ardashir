#!/usr/bin/env python3
"""Resolve selected Unreal function references and validate every operation flow."""
import argparse
import hashlib
import json
from pathlib import Path

from BuildUnrealGuide import load_js
from UnrealStageDetails import DETAILS, FILES


def build(engine, docs):
    guide = load_js(docs / "guide.js", "UnrealGuide")
    if set(DETAILS) != {stage["id"] for stage in guide["stages"]}:
        raise ValueError("Every pipeline stage must have function and flow details")
    stages, files = {}, {}
    for stage in guide["stages"]:
        id = stage["id"]
        authored = DETAILS[id]
        calls = []
        for index, call in enumerate(authored["calls"]):
            path = "Engine/Source/Runtime/" + FILES[call["file"]]
            raw = (engine / path).read_bytes()
            files[path] = hashlib.sha256(raw).hexdigest()
            lines = raw.decode("utf-8-sig").splitlines()
            matches = [i+1 for i, line in enumerate(lines) if call["query"] in line]
            if not matches:
                raise ValueError(f"Missing function reference in {id}: {path} / {call['query']}")
            line = call["line"] or matches[0]
            if line not in matches:
                raise ValueError(f"Moved function anchor {id}: {path}:{line} / {call['query']}; matches={matches}")
            if lines[line-1].lstrip().startswith(("//", "/*", "*")):
                raise ValueError(f"Function anchor points to comment: {id} / {call['query']}")
            calls.append(dict(id=f"call-{index}", name=call["name"], role=call["role"], when=call["when"], kind=call["kind"],
                source=dict(path=path, line=line, label=call["query"])))
        nodes = []
        for index, op in enumerate(authored["flow"]["nodes"]):
            if not all(0 <= call < len(calls) for call in op["calls"]):
                raise ValueError(f"Unknown call for {id} / {op['label']}")
            nodes.append(dict(id=f"op-{index}", label=op["label"], detail=op["detail"], gate=op["gate"],
                callIds=[f"call-{call}" for call in op["calls"]]))
        edges = []
        for start, end, label in authored["flow"]["edges"]:
            if not 0 <= start < end < len(nodes):
                raise ValueError(f"Invalid/backwards flow edge in {id}: {start} -> {end}")
            edges.append(dict(source=f"op-{start}", target=f"op-{end}", label=label))
        if len(nodes) < 3 or len(calls) < 2:
            raise ValueError(f"Insufficient stage detail: {id}")
        incident = {edge[key] for edge in edges for key in ("source", "target")}
        if incident != {node["id"] for node in nodes}:
            raise ValueError(f"Disconnected flow operation: {id}")
        stages[id] = dict(overview=authored["overview"], calls=calls, flow=dict(nodes=nodes, edges=edges), sceneSections=authored["sceneSections"])
    return dict(meta=guide["meta"], stages=stages, files=[dict(path=path, sha256=sha) for path, sha in sorted(files.items())])


def reference(data, guide):
    lines = ["# Deferred-frame functions and operation flows", "",
        f"Unreal Engine {data['meta']['version']}, commit `{data['meta']['commit']}`.", "",
        "Companion to [the pipeline walkthrough](reference.md) and [interactive deferred frame](deferred-pipeline.html). These are selected source-checked function call sites/helper definitions. They are not an exhaustive dynamic call trace or a complete C++ call graph. Flow arrows describe key control/data prerequisites; source call order does not imply serial GPU execution. Many CPU functions declare RDG passes whose GPU work executes later. Conditional alternatives remain visible and are labeled. Proposed operation descriptions paraphrase the source responsibilities.", ""]
    for stage in guide["stages"]:
        item = data["stages"][stage["id"]]
        lines.extend([f"## {stage['title']} ({stage['id']})", "", item["overview"], "", "### Selected functions", ""])
        for call in item["calls"]:
            source = call["source"]
            lines.extend([f"- `{call['id']}` **{call['name']}** — {call['kind']}. {call['role']} Context: {call['when']}. Source: `{source['path']}:{source['line']}`."])
        lines.extend(["", "### Key operation flow", ""])
        for node in item["flow"]["nodes"]:
            lines.append(f"- `{node['id']}` **{node['label']}**" + (f" [illustrated {node['gate']} path]" if node["gate"] else "") + ": " + node["detail"] + " Related functions: " + ", ".join(node["callIds"]) + ".")
        lines.extend(["", "Edges (control/data prerequisites):", ""])
        for edge in item["flow"]["edges"]:
            lines.append(f"- `{edge['source']}` → `{edge['target']}`" + (" — " + edge["label"] if edge["label"] else ""))
        lines.extend(["", "Scene-input sections: " + ", ".join(f"[{section}](scene-types.html#{section})" for section in item["sceneSections"]) + ".", ""])
    return "\n".join(lines).rstrip() + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine-root", type=Path, default=Path("D:/UnrealEngine"))
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[2] / "Docs/Unreal")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    data = build(args.engine_root, args.output)
    outputs = {"stage-details.js":"window.UnrealStageDetails = " + json.dumps(data, ensure_ascii=False, separators=(",", ":")) + ";\n",
        "stage-functions.md":reference(data, load_js(args.output / "guide.js", "UnrealGuide"))}
    for name, value in outputs.items():
        path = args.output / name
        if args.check:
            if not path.exists() or path.read_text(encoding="utf-8") != value:
                raise SystemExit(f"Stale stage details: {name}")
        else:
            path.write_text(value, encoding="utf-8", newline="\n")
    print(f"Built {len(data['stages'])} stage flows and {sum(len(s['calls']) for s in data['stages'].values())} function references.")


if __name__ == "__main__":
    main()
