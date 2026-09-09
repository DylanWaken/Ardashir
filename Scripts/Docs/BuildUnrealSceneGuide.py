#!/usr/bin/env python3
"""Build a practical scene ingestion guide with checked source anchors and enums."""
import argparse
import hashlib
import json
import re
from pathlib import Path

from BuildUnrealGuide import load_js
from BuildUnrealCatalog import mask_cpp
from UnrealSceneContent import ENUMS, GEOMETRY, SECTIONS


def build(engine, docs):
    guide = load_js(docs / "guide.js", "UnrealGuide")
    storage = load_js(docs / "primitive-storage.js", "UnrealPrimitiveStorage")
    profiles = {item["id"]: item for item in storage["profiles"]}
    primitive = load_js(docs / "primitives.js", "UnrealPrimitives")
    classes = {node["id"]: node for node in primitive["types"]}
    files = {}

    def read(path):
        raw = (engine / path).read_bytes()
        files[path] = hashlib.sha256(raw).hexdigest()
        return raw.decode("utf-8-sig")

    def anchor(path, query):
        matches = [i+1 for i, line in enumerate(read(path).splitlines()) if query in line]
        if not matches:
            raise ValueError(f"Missing scene-guide anchor: {path} / {query}")
        return dict(path=path, line=matches[0], label=query)

    sections = json.loads(json.dumps(SECTIONS))
    for section in sections:
        for item in section["items"]:
            query = item.pop("sourceQuery")
            item["sources"] = [anchor(*query)] if query else []
        if section["id"] == "geometry":
            for id, title, fields, work in GEOMETRY:
                profile = profiles[id]
                section["items"].append(dict(title=title, text=profile["role"], fields=[fields], work=[work],
                    sources=[anchor(resource["source"]["path"], resource["source"]["label"]) for resource in profile["resources"]],
                    unrealTypes=sorted({classes[id]["qualified"] for id in profile["classIds"]}), storage=profile["resources"]))
    enums = []
    for definition in ENUMS:
        source = read(definition["path"])
        match = re.search(r"\benum\s+" + definition["name"] + r"\b[^{}]*\{(.*?)\};", source, re.S)
        if not match:
            raise ValueError(f"Missing enum: {definition['name']}")
        body = mask_cpp(match[1])
        body = re.sub(r"UMETA\([^)]*\)", "", body)
        values = []
        for entry in body.split(","):
            token = re.sub(r"\s+", " ", entry).strip()
            name = token.split("=")[0].strip()
            if name and name not in definition["exclude"]:
                if not re.fullmatch(r"[A-Za-z_]\w*", name):
                    raise ValueError(f"Unexpected enum member: {token}")
                values.append(token)
        enums.append(dict(name=definition["name"], values=values, note=definition["note"],
            source=anchor(definition["path"], "enum " + definition["name"])))
    type_sections = {}
    for node in guide["types"]:
        group = node["group"]
        section = {"world":"contract", "scene":"instances", "views":"views", "gpu-scene":"instances", "materials":"materials", "nanite":"geometry", "lumen":"fidelity", "shadows":"fidelity", "rdg":"textures", "rhi":"textures"}[group]
        if "Material" in node["qualified"]:
            section = "materials"
        elif "Light" in node["qualified"] and group == "world":
            section = "lighting"
        elif "Texture" in node["qualified"] and group == "world":
            section = "textures"
        elif "Mesh" in node["qualified"] and group == "world":
            section = "geometry"
        type_sections[node["id"]] = section
    type_sections.update({id:"geometry" for id in classes})
    return dict(meta=guide["meta"], sections=sections, enums=enums, typeSections=type_sections,
        files=[dict(path=path, sha256=sha) for path, sha in sorted(files.items())])


def reference(data):
    lines = ["# Unreal scene representation for an independent renderer", "",
        f"Source: Unreal Engine {data['meta']['version']}, commit `{data['meta']['commit']}`.", "",
        "This is an authored integration/implementation guide inferred from source responsibilities. Suggested records, conversion choices and acceptance tests are not a public Unreal export ABI or claims of implemented Ardashir support. Source anchors are one-based and checked against the pinned local checkout. The interactive page is [scene-types.html](scene-types.html).", ""]
    for section in data["sections"]:
        lines.extend([f"## {section['title']} ({section['id']})", "", section["summary"], ""])
        for item in section["items"]:
            lines.extend(["### " + item["title"], "", item["text"], "", "**Data to carry**", ""])
            lines.extend(["- " + value for value in item["fields"]] + ["", "**Work / conversion policy**", ""])
            lines.extend(["- " + value for value in item["work"]] + [""])
            if item.get("unrealTypes"):
                lines.extend(["Unreal examples: " + ", ".join(f"`{name}`" for name in item["unrealTypes"]) + ".", ""])
            for resource in item.get("storage", []):
                lines.extend(["Storage example: " + resource["name"], "", "GPU data: " + resource["gpu"], "", "CPU boundary: " + resource["cpu"], ""])
            for source in item["sources"]:
                lines.append(f"Source: `{source['path']}:{source['line']}` — `{source['label']}`.")
            for title, url in item.get("links", []):
                lines.append(f"[{title}]({url}).")
            lines.append("")
        if section["id"] in ("geometry", "materials"):
            for enum in data["enums"]:
                if (enum["name"] == "EPrimitiveType") != (section["id"] == "geometry"):
                    continue
                lines.extend(["### " + enum["name"], "", enum["note"], ""] + ["- `" + value + "`" for value in enum["values"]] + ["",
                    f"Source: `{enum['source']['path']}:{enum['source']['line']}`.", ""])
    return "\n".join(lines).rstrip() + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine-root", type=Path, default=Path("D:/UnrealEngine"))
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[2] / "Docs/Unreal")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    data = build(args.engine_root, args.output)
    outputs = {"scene-guide.js":"window.UnrealSceneGuide = " + json.dumps(data, ensure_ascii=False, separators=(",", ":")) + ";\n", "scene-reference.md":reference(data)}
    for name, value in outputs.items():
        path = args.output / name
        if args.check:
            if not path.exists() or path.read_text(encoding="utf-8") != value:
                raise SystemExit(f"Stale scene guide: {name}")
        else:
            path.write_text(value, encoding="utf-8", newline="\n")
    print(f"Built {len(data['sections'])} scene sections, {sum(len(s['items']) for s in data['sections'])} requirements and {len(data['enums'])} source enums.")


if __name__ == "__main__":
    main()
