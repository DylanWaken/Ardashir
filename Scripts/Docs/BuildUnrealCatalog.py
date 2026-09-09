#!/usr/bin/env python3
"""Index explicit Unreal rendering C++ definitions; never copy engine implementations."""
from __future__ import annotations

import argparse
import bisect
import hashlib
import json
import re
import subprocess
from collections import Counter
from pathlib import Path

MODULES = ("RHI", "RHICore", "RenderCore", "Renderer", "D3D12RHI", "VulkanRHI", "MetalRHI", "OpenGLDrv", "NullDrv", "Landscape")
ENGINE_DIRS = ("Public", "Classes/Components", "Classes/Engine", "Classes/Materials", "Classes/VT", "Classes/SparseVolumeTexture")
ENGINE_NAMES = re.compile(r"Scene|View|Render|Mesh|Material|Texture|Light|Shadow|Vertex|Index|Nanite|DistanceField|Primitive|Instance|World|ActorComponent|Decal|Fog|Atmosphere|Sky|Reflection|RayTracing|Skeletal|Skin|Volum|Virtual", re.I)
DEFINITION = re.compile(r"\b(class|struct)\s+(?:(?:\w+_API|alignas\s*\([^)]*\))\s+)*([A-Za-z_]\w*)\s*(?:<[^;{}]*>)?\s*(?:final\s*)?(?::\s*([^;{}]+?))?\s*\{")
NAMESPACE = re.compile(r"\bnamespace\s+([A-Za-z_]\w*(?:::\w+)*)\s*\{")
PARAMETERS = re.compile(r"\bBEGIN_(?:GLOBAL_)?SHADER_PARAMETER_STRUCT(?:_WITH_CONSTRUCTOR)?\s*\(\s*(\w+)")
TOKENS = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|R"([^\s()\\]{0,16})\([\s\S]*?\)\1"|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')


def mask_cpp(text: str) -> str:
    return TOKENS.sub(lambda match: re.sub(r"[^\n]", " ", match.group()), text)


def base_clauses(text: str) -> list[tuple[str, list[str]]]:
    # Preserve conditional mixins without allowing #if/#endif into a C++ type name.
    conditions, ranges, masked_lines, offset = [], [], [], 0
    for line in text.splitlines(keepends=True):
        directive = re.match(r"\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)", line)
        if directive:
            op, expression = directive.groups()
            if op in {"if", "ifdef", "ifndef"}:
                conditions.append(f"#{op} {expression.strip()}")
            elif op in {"elif", "else"} and conditions:
                conditions[-1] = f"#{op} {expression.strip()} (alternative branch)".strip()
            elif op == "endif" and conditions:
                conditions.pop()
            masked_lines.append(re.sub(r"[^\n]", " ", line))
        else:
            masked_lines.append(line)
            ranges.append((offset, offset + len(line), list(conditions)))
        offset += len(line)
    masked = "".join(masked_lines)
    parts, start, depth = [], 0, 0
    for index, char in enumerate(masked):
        if char == "," and depth == 0:
            parts.append((start, masked[start:index]))
            start = index + 1
        else:
            depth += (char == "<") - (char == ">")
    parts.append((start, masked[start:]))
    result = []
    for start, part in parts:
        name = re.sub(r"\s+", " ", re.sub(r"\b(public|private|protected|virtual)\b", "", part)).strip()
        if not name:
            continue
        first_token = start + len(part) - len(part.lstrip())
        active = next((condition for left, right, condition in ranges if left <= first_token < right), [])
        result.append((name, active))
    return result


def split_bases(text: str) -> list[str]:
    return [name for name, _ in base_clauses(text)]


def extract(text: str, path: str, module: str) -> list[dict]:
    masked = mask_cpp(text)
    # Macro definitions are templates for declarations, not declarations themselves.
    # Keep #if lines for conditional-base annotation; mask #define bodies/continuations.
    macro_lines, continuing = [], False
    for line in masked.splitlines(keepends=True):
        is_macro = continuing or bool(re.match(r"\s*#\s*define\b", line))
        continuing = is_macro and line.rstrip().endswith("\\")
        macro_lines.append(re.sub(r"[^\n]", " ", line) if is_macro else line)
    masked = "".join(macro_lines)
    newlines = [index for index, char in enumerate(text) if char == "\n"]
    stack, ends = [], {}
    for index, char in enumerate(masked):
        if char == "{":
            stack.append(index)
        elif char == "}" and stack:
            ends[stack.pop()] = index
    definitions = [match for match in DEFINITION.finditer(masked)
        if not re.search(r"\benum\s*$", masked[max(0, match.start() - 20):match.start()])]
    scopes = [(match.end() - 1, ends.get(match.end() - 1, len(text)), match.group(1)) for match in NAMESPACE.finditer(masked)]
    scopes += [(match.end() - 1, ends.get(match.end() - 1, len(text)), match.group(2)) for match in definitions]
    result = []
    for match in definitions:
        kind, name, bases = match.groups()
        if name in {"alignas", "UE_DEPRECATED", "RENDERER_API", "ENGINE_API"} or name.endswith("_API"):
            continue
        parents = [scope for scope in scopes if scope[0] < match.start() < scope[1]]
        parents.sort()
        qualified = "::".join([scope[2] for scope in parents] + [name])
        body = masked[match.end():ends.get(match.end() - 1, match.end())]
        methods = list(dict.fromkeys(re.findall(r"\b([A-Z][A-Za-z0-9_]*)\s*\([^;{}]*\)\s*(?:const\s*)?(?:override\s*)?(?:final\s*)?(?:;|\{)", body)))[:10]
        fields = list(dict.fromkeys(re.findall(r"\b([A-Z][A-Za-z0-9_]*)\s*(?:=[^;{}]*|\[[^;{}]*\])?;", body)))[:10]
        clauses = base_clauses(bases or "")
        result.append({"id": "type-" + hashlib.sha1(f"{path}:{match.start()}:{name}".encode()).hexdigest()[:12],
            "name": name, "qualified": qualified, "kind": kind, "module": module,
            "bases": [base for base, _ in clauses], "baseConditions": [condition for _, condition in clauses], "path": path,
            "line": bisect.bisect_left(newlines, match.start()) + 1,
            "methods": methods, "fields": fields})
    # Unreal's shader parameter macros declare real structs but have no literal 'struct X {' token.
    for match in PARAMETERS.finditer(masked):
        name = match.group(1)
        parents = sorted(scope for scope in scopes if scope[0] < match.start() < scope[1])
        qualified = "::".join([scope[2] for scope in parents] + [name])
        result.append({"id": "type-" + hashlib.sha1(f"{path}:{match.start()}:{name}".encode()).hexdigest()[:12],
            "name": name, "qualified": qualified, "kind": "shader parameters", "module": module,
            "bases": [], "baseConditions": [], "path": path, "line": bisect.bisect_left(newlines, match.start()) + 1,
            "methods": [], "fields": []})
    return result


def collect(engine: Path) -> tuple[list[dict], dict]:
    runtime = engine / "Engine/Source/Runtime"
    files = {}
    for module in MODULES:
        module_root = runtime / ("Apple/MetalRHI" if module == "MetalRHI" else module)
        if not module_root.is_dir():
            raise ValueError(f"Missing indexed module: {module_root}")
        for path in module_root.rglob("*"):
            if path.suffix in {".h", ".cpp", ".inl", ".mm"} and "Tests" not in path.parts:
                files[path] = module
    for folder in ENGINE_DIRS:
        for path in (runtime / "Engine" / folder).rglob("*.h"):
            if ENGINE_NAMES.search(path.name):
                files[path] = "Engine"
    # Scene root and base component types are essential even when the filenames are generic.
    for relative in ("Engine/Classes/GameFramework/Actor.h", "Engine/Classes/Components/ActorComponent.h"):
        files[runtime / relative] = "Engine"
    nodes, manifests = [], []
    for path, module in sorted(files.items()):
        if not path.is_file():
            continue
        raw = path.read_bytes()
        relative = path.relative_to(engine).as_posix()
        entries = extract(raw.decode("utf-8", errors="replace"), relative, module)
        nodes.extend(entries)
        manifests.append({"path": relative, "sha256": hashlib.sha256(raw).hexdigest(), "declarations": len(entries)})
    names = {}
    for node in nodes:
        names.setdefault(node["qualified"], []).append(node["id"])
    for node in nodes:
        links = []
        for base_index, base in enumerate(node["bases"]):
            base_name = re.sub(r"<.*", "", base).strip()
            scope = node["qualified"].split("::")[:-1]
            candidates = []
            for length in range(len(scope), -1, -1):
                candidates = names.get("::".join(scope[:length] + [base_name]), [])
                if candidates:
                    break
            links.append({"name": base, "conditions": node["baseConditions"][base_index], "targets": candidates, "resolution": "unique" if len(candidates) == 1 else "ambiguous" if candidates else "external or unparsed"})
        node["baseLinks"] = links
    version = json.loads((engine / "Engine/Build/Build.version").read_text(encoding="utf-8-sig"))
    commit = subprocess.check_output(["git", "-c", f"safe.directory={engine.as_posix()}", "-C", str(engine), "rev-parse", "HEAD"], text=True).strip()
    meta = {"version": ".".join(str(version[key]) for key in ("MajorVersion", "MinorVersion", "PatchVersion")),
        "commit": commit, "engineRoot": engine.as_posix(), "modules": list(MODULES) + ["Engine"],
        "engineFolders": list(ENGINE_DIRS), "files": len(manifests), "declarations": len(nodes),
        "counts": dict(sorted(Counter(node["module"] for node in nodes).items())),
        "scope": "Explicit class/struct definitions and BEGIN_SHADER_PARAMETER_STRUCT-family declarations in the listed runtime rendering modules, plus rendering-related Engine headers. Includes inactive preprocessor alternatives. Test directories, plugins, editor/developer modules, enums, aliases, forward declarations, template instantiations, macro-definition templates and other macro-generated classes are excluded. This lexical inventory is not a compiler AST or a claim to enumerate every Unreal type."}
    return nodes, {"meta": meta, "files": manifests}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine-root", type=Path, default=Path("D:/UnrealEngine"))
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[2] / "Docs/Unreal")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    nodes, manifest = collect(args.engine_root.resolve())
    output = {"catalog.js": "window.UnrealCatalog = " + json.dumps({"meta": manifest["meta"], "types": nodes}, ensure_ascii=False, separators=(",", ":")) + ";\n",
              "source-manifest.json": json.dumps(manifest, ensure_ascii=False, indent=2) + "\n"}
    if not args.check:
        args.output.mkdir(parents=True, exist_ok=True)
    for name, value in output.items():
        path = args.output / name
        if args.check:
            if not path.exists() or path.read_text(encoding="utf-8") != value:
                raise SystemExit(f"Stale Unreal inventory: {path}")
        else:
            path.write_text(value, encoding="utf-8", newline="\n")
    print(f"Unreal {manifest['meta']['version']}: {len(nodes)} declarations across {len(manifest['files'])} files; {manifest['meta']['commit']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
