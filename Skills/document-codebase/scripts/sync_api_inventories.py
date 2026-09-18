#!/usr/bin/env python3
"""Synchronize backend/RDG API inventories and the static glossary."""

from __future__ import annotations

import argparse
import collections
import hashlib
import html
import json
import re
import shutil
import subprocess
from pathlib import Path
from typing import Dict, Iterable, List, Sequence, Tuple

from validate_docs import Validator
from glossary_html import synchronize_glossary_page


BACKEND_BEGIN = "/* BEGIN GENERATED BACKEND PROVIDER API */"
BACKEND_END = "/* END GENERATED BACKEND PROVIDER API */"
RDG_BEGIN = "/* BEGIN GENERATED ARDA RDG API GAPS */"
RDG_END = "/* END GENERATED ARDA RDG API GAPS */"

CONTRACT_HEADERS = (
    (
        "Source/ArdaInfra/ArdaBackend/Public/RHI/Providers/ArdaBackendProvider.h",
        "arda",
        "backend-modules",
    ),
    ("Source/ArdaInfra/ArdaBackend/Public/RHI/Scheduling/ArdaSwapChain.h", "arda", "presentation"),
    (
        "Source/ArdaInfra/ArdaBackend/Public/RHI/Providers/ArdaRHIProviderDevice.h",
        "arda",
        "rhi-device",
    ),
    (
        "Source/ArdaInfra/ArdaBackend/Public/RHI/Pipelines/ArdaRHIProviderPipelineCache.h",
        "arda",
        "pipelines",
    ),
)

# Complete declaration coverage follows the canonical modules instead of a list
# of former monolithic headers.
COMPLETE_BACKEND_SOURCE_ROOTS = (
    "Source/ArdaInfra/ArdaBackend/Public/RHI/",
    "Source/ArdaInfra/ArdaBackend/Public/FileOperations/",
)

CALLABLE_KINDS = {
    "function",
    "method",
    "operator",
    "conversion operator",
    "constructor",
    "destructor",
}


def normalized(text: str) -> str:
    return re.sub(r"\s+", " ", text).strip().rstrip(";")


def signature_identity(text: str) -> str:
    """Compare C++ tokens rather than layout, preserving literal contents."""
    return " ".join(re.findall(r'''"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|\w+|[^\s]''', text.strip().rstrip(";")))


def preserve_symbol_ids(declarations: Sequence[Dict[str, object]], api: Dict[str, object]) -> None:
    """Keep published anchors through moves and unambiguous signature changes."""
    existing = {
        (item["qualifiedName"], item["kind"], signature_identity(str(item.get("signature", "")))): item["id"]
        for item in api.get("symbols", [])
    }
    old_counts = collections.Counter((item["qualifiedName"], item["kind"]) for item in api.get("symbols", []))
    new_counts = collections.Counter((item["qualifiedName"], item["kind"]) for item in declarations)
    unique_ids = {(item["qualifiedName"], item["kind"]): item["id"] for item in api.get("symbols", [])
        if old_counts[(item["qualifiedName"], item["kind"])] == 1}
    for item in declarations:
        key = (item["qualifiedName"], item["kind"], signature_identity(str(item["signature"])))
        if key in existing:
            item["id"] = existing[key]
        elif new_counts[key[:2]] == 1 and key[:2] in unique_ids:
            item["id"] = unique_ids[key[:2]]


def reconcile_backend_sources(
    api: Dict[str, object], declarations: Sequence[Dict[str, object]], repo: Path
) -> None:
    """Move authored contracts with their declarations and retire deleted fields.

    Resolve each symbol against its declaration, not its former header: a split
    header can move different declarations to different owning modules.
    """
    by_name: Dict[str, List[Dict[str, object]]] = collections.defaultdict(list)
    for declaration in declarations:
        by_name[str(declaration["qualifiedName"])].append(declaration)
    reconciled = []
    for symbol in api.get("symbols", []):
        candidates = by_name.get(str(symbol.get("qualifiedName", "")), [])
        signature = str(symbol.get("signature", ""))
        if symbol.get("kind") in CALLABLE_KINDS:
            signature = signature.split("{", 1)[0]
        exact = [item for item in candidates
            if signature_identity(str(item["signature"])) == signature_identity(signature)]
        declaration = exact[0] if len(exact) == 1 else candidates[0] if len(candidates) == 1 else None
        if declaration:
            # The authored anchor wins over a previously generated duplicate.
            declaration["id"] = symbol["id"]
            for field in ("name", "qualifiedName", "source", "sourceLine", "component", "signature", "kind"):
                symbol[field] = declaration[field]
        elif not (repo / str(symbol.get("source", ""))).exists():
            continue
        reconciled.append(symbol)
    api["symbols"] = reconciled


def humanize(name: str) -> str:
    name = name.lstrip("~")
    name = re.sub(r"^m(?:b(?=[A-Z])|(?=[A-Z]))", "", name)
    words = re.sub(r"(?<!^)(?=[A-Z])", " ", name).replace("_", " ")
    return words.lower() or name.lower()


def find_matching_brace(text: str, opening: int) -> int:
    depth = 0
    quote = ""
    escaped = False
    for index in range(opening, len(text)):
        char = text[index]
        if quote:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = ""
            continue
        if char in "\"'":
            quote = char
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return index
    return len(text)


def type_ranges(text: str) -> List[Tuple[int, int, str, str]]:
    text = Validator.mask_template_parameters(text)
    ranges: List[Tuple[int, int, str, str]] = []
    pattern = re.compile(
        r"\b(?P<kind>class|struct|enum(?:\s+class)?)\s+"
        r"(?P<name>[A-Za-z_]\w*)[^;{}]*\{"
    )
    for match in pattern.finditer(text):
        opening = text.find("{", match.start(), match.end())
        ranges.append(
            (opening, find_matching_brace(text, opening), match.group("name"), match.group("kind"))
        )
    return ranges


def owner_at(offset: int, ranges: Sequence[Tuple[int, int, str, str]]) -> str:
    owners = [item for item in ranges if item[0] < offset < item[1] and not item[3].startswith("enum")]
    return min(owners, key=lambda item: item[1] - item[0])[2] if owners else ""


def enum_owner_at(offset: int, ranges: Sequence[Tuple[int, int, str, str]]) -> str:
    owners = [item for item in ranges if item[0] < offset < item[1] and item[3].startswith("enum")]
    return min(owners, key=lambda item: item[1] - item[0])[2] if owners else ""


def offset_for_line(text: str, line: int) -> int:
    if line <= 1:
        return 0
    offset = 0
    for _ in range(line - 1):
        newline = text.find("\n", offset)
        if newline < 0:
            return len(text)
        offset = newline + 1
    return offset


def name_offset(text: str, name: str, line: int) -> int:
    start = offset_for_line(text, line)
    end = text.find("\n", start)
    if end < 0:
        end = len(text)
    match = re.search(r"(?<![A-Za-z0-9_])" + re.escape(name) + r"(?![A-Za-z0-9_])", text[start:end])
    if match:
        return start + match.start()
    match = re.search(r"(?<![A-Za-z0-9_])" + re.escape(name) + r"(?![A-Za-z0-9_])", text[start:])
    return start + match.start() if match else start


def callable_signature(text: str, name: str, offset: int) -> str:
    opening = text.find("(", offset + len(name))
    if opening < 0:
        return name + "()"
    depth = 0
    closing = opening
    for closing in range(opening, len(text)):
        if text[closing] == "(":
            depth += 1
        elif text[closing] == ")":
            depth -= 1
            if depth == 0:
                break
    end = closing + 1
    while end < len(text) and text[end] not in ";{":
        end += 1
    boundary = max(text.rfind(token, 0, offset) for token in ";{}") + 1
    signature = text[boundary:end].strip()
    signature = re.sub(r"^(?:public|private|protected)\s*:\s*", "", signature)
    return normalized(signature)


def declaration_signature(text: str, name: str, line: int, kind: str) -> str:
    offset = name_offset(text, name, line)
    if kind == "macro":
        # A macro's public signature is its name and formal arguments, not the
        # preceding declaration or its implementation's continuation lines.
        start = text.rfind("\n", 0, offset) + 1
        match = re.match(r"[ \t]*#[ \t]*define[ \t]+\w+(?:\([^\n]*?\))?", text[start:])
        return normalized(match.group(0)) if match else name
    if kind == "callable":
        return callable_signature(text, name, offset)
    if kind == "type":
        start = max(text.rfind("\n", 0, offset), text.rfind("}", 0, offset)) + 1
        boundary = max(text.rfind(token, 0, start) for token in ";{}") + 1
        template = re.search(r"template\s*<[^;{}]*>\s*$", text[boundary:start])
        if template:
            start = boundary + template.start()
        end = text.find("{", offset)
        return normalized(text[start:end] if end >= 0 else name)
    if kind == "enumerator":
        end = min(value for value in (text.find(",", offset), text.find("}", offset)) if value >= 0)
        return normalized(text[offset:end])
    start = max(text.rfind(token, 0, offset) for token in ";{}") + 1
    end = text.find(";", offset)
    if end < 0:
        end = text.find("\n", offset)
    signature = text[start:end].strip()
    signature = re.sub(r"^(?:public|private|protected)\s*:\s*", "", signature)
    return normalized(signature)


def api_kind(kind: str, name: str, signature: str, owner: str) -> str:
    if kind == "type":
        signature = Validator.mask_template_parameters(signature).strip()
        if signature.startswith("enum"):
            return "enum"
        return "struct" if signature.startswith("struct") else "class"
    if kind == "callable":
        if name.startswith("~"):
            return "destructor"
        if name.startswith("operator"):
            return "conversion operator" if re.match(r"operator\s+[A-Za-z_]", name) else "operator"
        if owner and name == owner:
            return "constructor"
        return "method" if owner else "function"
    return {
        "alias": "alias",
        "constant": "constant",
        "member": "member variable",
        "enumerator": "enumerator",
        "macro": "macro",
    }.get(kind, kind)


def description(name: str, kind: str, domain: str) -> Tuple[str, str]:
    subject = humanize(name)
    if kind in {"class", "struct", "enum", "alias", "constant", "macro"}:
        summary = f"Defines {subject} in the public {domain} contract."
    elif kind == "enumerator":
        summary = f"Selects the {subject} {domain} value."
    elif kind == "member variable":
        summary = f"Stores {subject} in this public {domain} value."
    elif kind == "destructor":
        summary = f"Destroys the {domain} object after dependent work is released."
    elif kind == "constructor":
        summary = f"Constructs the {domain} value from the declared inputs."
    else:
        summary = f"Performs {subject} through the public {domain} contract."
    details = summary + " The signature is generated from the current public header."
    return summary, details


def backend_specs(repo: Path) -> List[Tuple[str, str, str]]:
    overrides = {item[0]: item for item in CONTRACT_HEADERS}
    specs: List[Tuple[str, str, str]] = []
    # pathlib compares Windows paths without case, unlike POSIX. Use the exact
    # repository-relative spelling so generated order and duplicate ownership
    # remain the same on developer machines and Linux CI.
    for header in sorted(
        (p for p in (repo / "Source/ArdaInfra/ArdaBackend/Public").rglob("*")
         if p.suffix in {".h", ".cuh"}),
        key=lambda path: path.relative_to(repo).as_posix(),
    ):
        source = header.relative_to(repo).as_posix()
        if source in overrides:
            specs.append(overrides[source])
            continue
        if "/RHI/" in source:
            namespace = "arda"
            module = source.split("/RHI/", 1)[1].split("/", 1)[0]
            component = {
                "CUDA": "shaders",
                "Config": "rhi-types",
                "Context": "rhi-device",
                "Device": "rhi-device",
                "Interop": "external-interop",
                "Memory": "rhi-resources",
                "Pipelines": "pipelines",
                "Providers": "rhi-device",
                "Resources": "rhi-resources",
                "Scheduling": "rhi-device",
                "Shaders": "shaders",
            }.get(module, "rhi-resources")
            if header.name in {"ArdaAssert.h", "ArdaLog.h", "ArdaBackendDiagnostics.h", "ArdaRHIDiagnostics.h"}:
                component = "diagnostics"
        else:
            namespace = "arda"
            component = "core"
        specs.append((source, namespace, component))
    return specs


def source_contract(raw: str, line: int) -> Dict[str, object]:
    """Use the nearest declaration's Doxygen contract, preserving source as prose authority."""
    prefix = "\n".join(raw.splitlines()[:line - 1])
    comments = list(re.finditer(r"/\*\*(.*?)\*/", prefix, re.S))
    if not comments:
        return {}
    comment = comments[-1]
    # A completed earlier declaration means its comment belongs to another symbol.
    if re.search(r"[;{}]", prefix[comment.end():]):
        return {}
    body = re.sub(r"(?m)^\s*\*\s?", "", comment.group(1)).strip()
    if body.startswith("@file"):
        return {}
    prose = body.split("@", 1)[0].strip()
    result: Dict[str, object] = {}
    if prose:
        result.update(summary=normalized(prose.split("\n\n", 1)[0]), details=normalized(prose))
    params = re.findall(r"@param\s+(\w+)\s+([^@]+)", body)
    if params:
        result["params"] = [{"name": name, "description": normalized(value)} for name, value in params]
    returns = re.search(r"@return\s+([^@]+)", body)
    if returns:
        result["returns"] = normalized(returns.group(1))
    for field in ("ownership", "errors", "threading"):
        value = re.search(r"@" + field + r"\s+([^@]+)", body)
        if value:
            result[field] = normalized(value.group(1))
    return result


def rdg_specs(repo: Path) -> List[Tuple[str, str, str]]:
    return [
        (header.relative_to(repo).as_posix(), "arda", "core")
        for header in sorted(
            (repo / "Source/ArdaInfra/ArdaRenderGraph/Public").rglob("*.h"),
            key=lambda path: path.relative_to(repo).as_posix(),
        )
    ]


def make_symbols(
    repo: Path,
    specs: Iterable[Tuple[str, str, str]],
    domain: str,
) -> List[Dict[str, object]]:
    validator = Validator(repo / "Docs", repo, False)
    symbols: List[Dict[str, object]] = []
    semantic = set()
    for source, namespace, component in specs:
        header = repo / source
        raw = header.read_text(encoding="utf-8-sig")
        text = validator.strip_cpp_comments(raw)
        ranges = type_ranges(text)
        declarations = validator.extract_public_declarations([header])
        for name, _header, line, declared_kind in sorted(declarations, key=lambda item: (item[2], item[0], item[3])):
            offset = name_offset(text, name, line)
            owner = enum_owner_at(offset, ranges) if declared_kind == "enumerator" else owner_at(offset, ranges)
            signature = declaration_signature(text, name, line, declared_kind)
            kind = api_kind(declared_kind, name, signature, owner)
            qualified = "::".join(item for item in (namespace, owner, name) if item)
            key = (qualified, kind, signature_identity(signature))
            if key in semantic:
                continue
            semantic.add(key)
            digest = hashlib.sha1("\0".join((*key, source)).encode("utf-8")).hexdigest()[:8]
            slug = re.sub(r"[^a-z0-9]+", "-", qualified.lower()).strip("-")
            summary, details = description(name, kind, domain)
            symbols.append(
                {
                    "id": f"api-{slug}-{digest}",
                    "name": name,
                    "qualifiedName": qualified,
                    "kind": kind,
                    "component": component,
                    "page": "api-reference.html",
                    "signature": signature,
                    "summary": summary,
                    "details": details,
                    "source": source,
                    "params": [],
                    "returns": "See the declared result and status contract." if kind in {"function", "method", "operator", "conversion operator"} else "",
                    "ownership": "Owning handles retain their object; pointer and reference parameters are borrowed unless stated otherwise.",
                    "errors": "Failures and unsupported operations use the declared FArdaRHIStatus or TArdaRHIResult contract.",
                    "threading": "Calls follow the synchronization rules of the owning provider device or command list.",
                    "related": ["::".join(item for item in (namespace, owner) if item)],
                }
            )
            symbols[-1].update(source_contract(raw, line))
            symbols[-1]["sourceLine"] = line
    return symbols


def without_generated_block(asset: str, begin: str, end: str) -> str:
    if begin not in asset:
        return asset
    start = asset.index(begin)
    finish = asset.index(end, start) + len(end)
    return asset[:start] + asset[finish:]


def evaluate_api(asset: str, global_name: str) -> Dict[str, object]:
    try:
        import quickjs  # type: ignore

        context = quickjs.Context()
        context.eval("var window = {};")
        context.eval(asset)
        encoded = context.eval(
            "JSON.stringify(window[%s])" % json.dumps(global_name)
        )
        return json.loads(encoded)
    except ImportError:
        node = shutil.which("node")
        if not node:
            raise RuntimeError(
                "API synchronization requires Node.js or the optional quickjs Python module"
            )
        program = (
            "const fs=require('fs'),vm=require('vm');"
            "const box={window:{}};"
            "vm.runInNewContext(fs.readFileSync(0,'utf8'),box,{timeout:10000});"
            "process.stdout.write(JSON.stringify(box.window[process.argv[1]]));"
        )
        result = subprocess.run(
            [node, "-e", program, global_name],
            input=asset.encode("utf-8"),
            check=True,
            stdout=subprocess.PIPE,
            timeout=20,
        )
        return json.loads(result.stdout.decode("utf-8"))


def select_missing(
    declarations: Sequence[Dict[str, object]],
    api: Dict[str, object],
    complete_sources: Iterable[str] = (),
) -> List[Dict[str, object]]:
    current = [item for item in api.get("symbols", []) if isinstance(item, dict)]
    semantic = {
        (
            str(item.get("qualifiedName", "")).strip(),
            str(item.get("kind", "")).strip(),
            signature_identity(str(item.get("signature", ""))),
        )
        for item in current
    }
    names = {str(item.get("name", "")).strip() for item in current}
    signature_names = {
        token
        for item in current
        for token in re.findall(r"[A-Za-z_]\w*", str(item.get("signature", "")))
    }
    documented = collections.Counter(
        (str(item.get("source", "")), str(item.get("name", "")).strip())
        for item in current
        if item.get("kind") in CALLABLE_KINDS
    )
    declared = collections.Counter(
        (str(item["source"]), str(item["name"]))
        for item in declarations
        if item["kind"] in CALLABLE_KINDS
    )
    complete = set(complete_sources)
    missing: List[Dict[str, object]] = []
    for item in declarations:
        key = (
            str(item["qualifiedName"]),
            str(item["kind"]),
            signature_identity(str(item["signature"])),
        )
        if key in semantic:
            continue
        source_name = (str(item["source"]), str(item["name"]))
        if str(item["source"]) in complete:
            add = True
        elif item["kind"] in CALLABLE_KINDS:
            add = documented[source_name] < declared[source_name]
        else:
            name = str(item["name"])
            add = name not in names and name not in signature_names
        if not add:
            continue
        missing.append(item)
        semantic.add(key)
        names.add(str(item["name"]))
        signature_names.update(re.findall(r"[A-Za-z_]\w*", str(item["signature"])))
        if item["kind"] in CALLABLE_KINDS:
            documented[source_name] += 1
    return missing


def generated_block(
    symbols: Sequence[Dict[str, object]],
    global_name: str,
    begin: str,
    end: str,
    contracts: Sequence[Dict[str, object]] = (),
) -> str:
    data = json.dumps(symbols, indent=2, ensure_ascii=True)
    return f"""{begin}
(() => {{
  const generatedSymbols = {data};
  window.{global_name}.symbols.push(...generatedSymbols);
  const sourceContracts = {json.dumps(contracts, indent=2, ensure_ascii=True)};
  for (const contract of sourceContracts) {{
    const symbol = window.{global_name}.symbols.find(item => item.id === contract.id);
    if (symbol) Object.assign(symbol, contract);
  }}
}})();
{end}
"""


def synchronize_backend(asset: str, block: str, public_header_count: int) -> str:
    asset = re.sub(
        r'"generatedFrom": "Source/ArdaInfra/ArdaBackend/Public \(all \d+ unique public headers\)"',
        f'"generatedFrom": "Source/ArdaInfra/ArdaBackend/Public (all {public_header_count} unique public headers)"',
        asset,
    )
    provenance_end = asset.index("\n  ],", asset.index('"headerProvenance"'))
    for source, _namespace, _component in CONTRACT_HEADERS:
        literal = f'    "{source}"'
        if literal not in asset[:provenance_end]:
            asset = asset[:provenance_end] + ",\n" + literal + asset[provenance_end:]
            provenance_end += len(literal) + 2
    if BACKEND_BEGIN in asset:
        start = asset.index(BACKEND_BEGIN)
        end = asset.index(BACKEND_END, start) + len(BACKEND_END)
        asset = asset[:start] + block.rstrip() + asset[end:]
    else:
        asset = asset.rstrip() + "\n\n" + block
    return asset


def synchronize_block(asset: str, block: str, begin: str, end: str) -> str:
    if begin in asset:
        start = asset.index(begin)
        finish = asset.index(end, start) + len(end)
        return asset[:start] + block.rstrip() + asset[finish:]
    return asset.rstrip() + "\n\n" + block


def static_api_reference(template: str, asset: str, variable: str, module: str, marker: str) -> str:
    """Render the canonical inventory before JavaScript progressively adds filtering."""
    api = evaluate_api(asset, variable)
    symbols = sorted(api["symbols"], key=lambda s: (s["component"], s["kind"], s["qualifiedName"]))
    escape = lambda value: html.escape(str(value).replace("\r\n", "\n").replace("\r", "\n"), quote=True)
    index = '<ul class="api-index-list">\n' + "\n".join(
        f'<li><a href="#{escape(s["id"])}">{escape(s["qualifiedName"])} <span class="api-kind">{escape(s["kind"])}</span></a></li>'
        for s in symbols) + "\n</ul>"
    parts = []
    components = collections.defaultdict(list)
    for symbol in symbols:
        components[symbol["component"]].append(symbol)
    labels = {c["id"]: c["name"] for c in api["components"]}
    for component, entries in components.items():
        parts.append(f'<section class="api-group" id="api-component-{escape(component)}"><h3>{escape(labels.get(component, component))}</h3>')
        kinds = collections.defaultdict(list)
        for symbol in entries:
            kinds[symbol["kind"]].append(symbol)
        for kind, entries_by_kind in kinds.items():
            parts.append(f'<section class="api-kind-group"><h4>{escape(kind)}</h4>')
            for symbol in entries_by_kind:
                aliases = "".join(f'<span id="{escape(alias)}"></span>' for alias in symbol.get("aliases", []))
                parts.append(f'<section class="api-entry" id="{escape(symbol["id"])}" tabindex="-1">{aliases}<h5>{escape(symbol["qualifiedName"])}</h5><pre><code>{escape(symbol["signature"])}</code></pre><p>{escape(symbol["summary"])}</p>')
                if symbol.get("details") and symbol["details"] != symbol["summary"]:
                    parts.append(f'<p>{escape(symbol["details"])}</p>')
                parts.append('<dl>')
                for parameter in symbol.get("params", []):
                    if isinstance(parameter, dict):
                        parts.append(f'<dt>{escape(parameter.get("name", "Parameter"))}</dt><dd>{escape(parameter.get("description", ""))}</dd>')
                    else:
                        parts.append(f'<dt>Parameter</dt><dd>{escape(parameter)}</dd>')
                for field in ("returns", "ownership", "errors", "threading", "source"):
                    if symbol.get(field):
                        parts.append(f'<dt>{field.title()}</dt><dd>{escape(symbol[field])}</dd>')
                parts.append('</dl></section>')
            parts.append('</section>')
        parts.append('</section>')
    for name, selector, content in (("INDEX", f'data-api-index="{module}"', index), ("DETAILS", 'data-api-reference', "\n".join(parts))):
        begin, end = f"<!-- BEGIN STATIC {marker} {name} -->", f"<!-- END STATIC {marker} {name} -->"
        if begin not in template:
            template = template.replace(f'<div {selector}></div>', f'<div {selector}>{begin}{end}</div>')
        first, last = template.index(begin), template.index(end) + len(end)
        template = template[:first] + begin + "\n" + content + "\n" + end + template[last:]
    template = re.sub(r'<noscript>.*?</noscript>', '<noscript><p>The complete index and contracts below work without JavaScript. Use browser Find to search; JavaScript adds synchronized filters.</p></noscript>', template, flags=re.S)
    return template


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--repo-root", type=Path)
    args = parser.parse_args()
    repo = (args.repo_root or Path(__file__).resolve().parents[3]).resolve()
    glossary_path = repo / "Docs/glossary.html"
    glossary_current = glossary_path.read_text(encoding="utf-8")
    glossary = evaluate_api(
        (repo / "Docs/assets/glossary-data.js").read_text(encoding="utf-8"),
        "ArdaGlossary",
    )
    glossary_updated = synchronize_glossary_page(glossary_current, glossary)
    backend_path = repo / "Docs/assets/backend-api.js"
    backend_current = backend_path.read_text(encoding="utf-8-sig")
    backend_base = without_generated_block(
        backend_current, BACKEND_BEGIN, BACKEND_END
    )
    backend_source_specs = backend_specs(repo)
    backend_declarations = make_symbols(repo, backend_source_specs, "backend and RHI")
    preserve_symbol_ids(backend_declarations, evaluate_api(backend_current, "ArdaBackendApi"))
    backend_authored = evaluate_api(backend_base, "ArdaBackendApi")
    reconcile_backend_sources(backend_authored, backend_declarations, repo)
    backend_base = (
        "/* Machine-checkable public source inventory for ArdaBackend.\n"
        " * Source declarations own signatures and provenance; authored contracts retain their anchors.\n"
        " */\nwindow.ArdaBackendApi = " + json.dumps(backend_authored, indent=2) + ";\n"
    )
    backend_symbols = select_missing(
        backend_declarations,
        evaluate_api(backend_base, "ArdaBackendApi"),
        [source for source, _namespace, _component in backend_source_specs
            if source.startswith(COMPLETE_BACKEND_SOURCE_ROOTS)],
    )
    # Rich header contracts supersede older authored boilerplate while preserving
    # canonical IDs and related links. Generic one-line comments do not replace prose.
    authored = evaluate_api(backend_base, "ArdaBackendApi")["symbols"]
    declaration_counts = collections.Counter(
        (item["qualifiedName"], item["kind"]) for item in backend_declarations
    )
    contracts = []
    for declaration in backend_declarations:
        existing = next((s for s in authored if s["qualifiedName"] == declaration["qualifiedName"]
            and signature_identity(s["signature"]) == signature_identity(declaration["signature"])), None)
        if existing is None and declaration_counts[(declaration["qualifiedName"], declaration["kind"])] == 1:
            # Parameter names/defaults may improve while an unambiguous API keeps its canonical anchor.
            candidates = [s for s in authored if s["qualifiedName"] == declaration["qualifiedName"]
                and s["kind"] == declaration["kind"]]
            if len(candidates) == 1:
                existing = candidates[0]
        if existing:
            # Source moves must update provenance even when the source has only a
            # short comment and the authored prose remains the better contract.
            fields = ("source", "sourceLine", "component")
            if not declaration["ownership"].startswith("Owning handles retain"):
                fields += ("signature", "summary", "details", "params", "returns", "ownership", "errors", "threading")
            contracts.append({"id": existing["id"], **{field: declaration[field] for field in
                fields}})
    backend_block = generated_block(
        backend_symbols, "ArdaBackendApi", BACKEND_BEGIN, BACKEND_END, contracts
    )
    header_count = len(backend_source_specs)
    backend_updated = synchronize_backend(
        backend_base, backend_block, header_count
    )
    provenance = [source for source, _namespace, _component in backend_source_specs]
    backend_updated = re.sub(r'"headerProvenance": \[.*?\n  \]',
        '"headerProvenance": ' + json.dumps(provenance, indent=2).replace("\n", "\n  "),
        backend_updated, count=1, flags=re.S)
    reference_path = repo / "Docs/ArdaBackend/api-reference.html"
    reference_current = reference_path.read_text(encoding="utf-8")
    reference_updated = static_api_reference(reference_current, backend_updated, "ArdaBackendApi", "ArdaBackend", "BACKEND")

    rdg_path = repo / "Docs/assets/arda-rdg-api.js"
    rdg_current = rdg_path.read_text(encoding="utf-8-sig")
    # The persistent graph is the only authoring API. Rebuild from live declarations
    # so removed headers and symbols cannot survive in an authored inventory base.
    rdg_symbols = make_symbols(repo, rdg_specs(repo), "dependency graph")
    preserve_symbol_ids(rdg_symbols, evaluate_api(rdg_current, "ArdaRDGApi"))
    rdg_provenance = [source for source, _namespace, _component in rdg_specs(repo)]
    rdg_inventory = {
        "module": {"id": "arda-rdg", "name": "ArdaRenderGraph", "namespace": "arda",
            "summary": "Persistent dependency graphs compiled by ArdaInductor for graphics, CUDA, and transfer work."},
        "generatedFrom": f"Source/ArdaInfra/ArdaRenderGraph/Public (all {len(rdg_provenance)} unique public headers)",
        "headerProvenance": rdg_provenance,
        "components": [{"id": "core", "name": "Persistent graph API", "page": "api-reference.html",
            "summary": "Typed node registration, resource dependencies, compilation, and reusable GPU execution."}],
        "symbols": rdg_symbols,
    }
    rdg_updated = (f"/* Generated public source inventory for ArdaRenderGraph; derived from all {len(rdg_provenance)} public headers. */\n"
        + "window.ArdaRDGApi = " + json.dumps(rdg_inventory, indent=2) + ";\n")
    rdg_reference_path = repo / "Docs/ArdaRDG/api-reference.html"
    rdg_reference_current = rdg_reference_path.read_text(encoding="utf-8")
    rdg_reference_updated = static_api_reference(rdg_reference_current, rdg_updated, "ArdaRDGApi", "ArdaRenderGraph", "RDG")
    if args.check:
        stale = []
        if glossary_current != glossary_updated:
            stale.append(glossary_path)
        if backend_current != backend_updated:
            stale.append(backend_path)
        if reference_current != reference_updated:
            stale.append(reference_path)
        if rdg_current != rdg_updated:
            stale.append(rdg_path)
        if rdg_reference_current != rdg_reference_updated:
            stale.append(rdg_reference_path)
        if stale:
            print(
                "%s is stale; run %s"
                % (", ".join(str(path) for path in stale), Path(__file__).name)
            )
            return 1
        print(
            "API inventories are synchronized "
            f"({len(backend_symbols)} backend additions, "
            f"{len(rdg_symbols)} RDG additions)."
        )
        return 0
    backend_path.write_text(backend_updated, encoding="utf-8", newline="\n")
    reference_path.write_text(reference_updated, encoding="utf-8", newline="\n")
    rdg_path.write_text(rdg_updated, encoding="utf-8", newline="\n")
    rdg_reference_path.write_text(rdg_reference_updated, encoding="utf-8", newline="\n")
    glossary_path.write_text(glossary_updated, encoding="utf-8", newline="\n")
    print(
        f"Updated API inventories with {len(backend_symbols)} backend additions "
        f"and {len(rdg_symbols)} RDG additions."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
