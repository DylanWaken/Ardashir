#!/usr/bin/env python3
"""Validate the dependency-free Ardashir static documentation site."""

from __future__ import annotations

import argparse
import collections
import html.parser
import json
import os
import re
import shutil
import subprocess
import sys
import urllib.parse
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Sequence, Set, Tuple


EXTERNAL_SCHEMES = {
    "data", "ftp", "ftps", "http", "https", "irc", "javascript", "mailto",
    "news", "sms", "ssh", "tel", "urn",
}
API_REQUIRED = {
    "id", "name", "qualifiedName", "kind", "component", "page", "signature",
    "summary", "details", "source", "params", "returns", "ownership", "errors",
    "threading", "related",
}
COMPONENT_REQUIRED = {"id", "name", "page", "summary"}
CPP_IGNORED = {
    "alignas", "alignof", "and", "asm", "auto", "bitand", "bitor", "bool",
    "break", "case", "catch", "char", "char8_t", "char16_t", "char32_t",
    "class", "compl", "concept", "const", "consteval", "constexpr",
    "constinit", "const_cast", "continue", "co_await", "co_return",
    "co_yield", "decltype", "default", "delete", "do", "double",
    "dynamic_cast", "else", "enum", "explicit", "export", "extern", "false",
    "float", "for", "friend", "goto", "if", "inline", "int", "long",
    "mutable", "namespace", "new", "noexcept", "not", "nullptr", "operator",
    "or", "private", "protected", "public", "register", "reinterpret_cast",
    "requires", "return", "short", "signed", "sizeof", "static",
    "static_assert", "static_cast", "struct", "switch", "template", "this",
    "thread_local", "throw", "true", "try", "typedef", "typeid", "typename",
    "union", "unsigned", "using", "virtual", "void", "volatile", "wchar_t",
    "while", "xor", "override", "final", "std", "eastl",
}


def ascii_text(value: object) -> str:
    return str(value).encode("ascii", "backslashreplace").decode("ascii")


def posix(path: Path) -> str:
    return path.as_posix()


@dataclass
class Finding:
    path: str
    message: str
    line: Optional[int] = None


@dataclass
class Link:
    source: Path
    value: str
    attribute: str
    line: Optional[int] = None


@dataclass
class HtmlInfo:
    path: Path
    ids: Set[str] = field(default_factory=set)
    links: List[Link] = field(default_factory=list)
    terms: List[Tuple[str, int]] = field(default_factory=list)
    api_mounts: List[Tuple[str, str, int]] = field(default_factory=list)
    feature_sections: List["FeatureSection"] = field(default_factory=list)
    html_count: int = 0
    html_lang: str = ""
    title_count: int = 0
    title_text: List[str] = field(default_factory=list)
    viewport_count: int = 0
    main_count: int = 0


@dataclass
class FeatureSection:
    line: int
    identifier: str
    classes: Set[str]
    headings: List[str] = field(default_factory=list)
    api_links: int = 0
    pre_blocks: int = 0
    table_rows: int = 0
    entry_markers: int = 0


@dataclass(frozen=True)
class ModuleConfig:
    """Everything that varies between documented public modules."""

    name: str
    source_public_root: str
    docs_root: str
    api_js_path: str
    api_global: str
    api_page: str = "api-reference.html"
    include_only_umbrellas: Tuple[str, ...] = ()
    required_public_headers: Tuple[str, ...] = ()


MODULES = (
    ModuleConfig(
        name="ArdaBackend",
        source_public_root="Source/ArdaBackend/Public",
        docs_root="ArdaBackend",
        api_js_path="assets/backend-api.js",
        api_global="ArdaBackendApi",
        include_only_umbrellas=(
            "Source/ArdaBackend/Public/RHIWrappers/ArdaRHI.h",
        ),
    ),
    ModuleConfig(
        name="ArdaRenderGraph",
        source_public_root="Source/ArdaRenderGraph/Public",
        docs_root="ArdaRDG",
        api_js_path="assets/arda-rdg-api.js",
        api_global="ArdaRDGApi",
        required_public_headers=(
            "ArdaRenderGraph.h",
            "ArdaRenderGraphBlackboard.h",
            "ArdaRenderGraphBuilder.h",
            "ArdaRenderGraphDefinitions.h",
            "ArdaRenderGraphLog.h",
            "ArdaRenderGraphParameters.h",
            "ArdaRenderGraphPass.h",
            "ArdaRenderGraphResources.h",
        ),
    ),
)


class SiteHtmlParser(html.parser.HTMLParser):
    def __init__(self, path: Path, fail: Any) -> None:
        super().__init__(convert_charrefs=True)
        self.info = HtmlInfo(path)
        self.fail = fail
        self._in_title = False
        self._sections: List[FeatureSection] = []
        self._heading_sections: List[FeatureSection] = []
        self._heading_text: List[str] = []

    def handle_starttag(self, tag: str, attrs: List[Tuple[str, Optional[str]]]) -> None:
        self._tag(tag, attrs)

    def handle_startendtag(self, tag: str, attrs: List[Tuple[str, Optional[str]]]) -> None:
        self._tag(tag, attrs)

    def handle_endtag(self, tag: str) -> None:
        tag = tag.lower()
        if tag == "title":
            self._in_title = False
        if re.fullmatch(r"h[1-6]", tag) and self._heading_sections:
            heading = "".join(self._heading_text).strip()
            if heading:
                for section in self._heading_sections:
                    section.headings.append(heading)
            self._heading_sections = []
            self._heading_text = []
        if tag == "section" and self._sections:
            self._sections.pop()

    def handle_data(self, data: str) -> None:
        if self._in_title:
            self.info.title_text.append(data)
        if self._heading_sections:
            self._heading_text.append(data)

    def _tag(self, tag: str, attrs: List[Tuple[str, Optional[str]]]) -> None:
        tag = tag.lower()
        values = {name.lower(): (value or "") for name, value in attrs}
        line = self.getpos()[0]
        if tag == "section":
            section = FeatureSection(
                line=line,
                identifier=values.get("id", "").strip(),
                classes=set(values.get("class", "").split()),
            )
            self.info.feature_sections.append(section)
            self._sections.append(section)
        identifier = values.get("id")
        if identifier:
            if identifier in self.info.ids:
                self.fail(self.info.path, "duplicate id %r" % identifier, line)
            self.info.ids.add(identifier)
        for attribute in ("href", "src"):
            if attribute in values and values[attribute].strip():
                self.info.links.append(
                    Link(self.info.path, values[attribute].strip(), attribute, line)
                )
        href = values.get("href", "").strip()
        if href:
            parsed_href = urllib.parse.urlsplit(href.replace("\\", "/"))
            if Path(parsed_href.path).name.lower() == "api-reference.html":
                for section in self._sections:
                    section.api_links += 1
        if "data-term" in values:
            for slug in values["data-term"].split():
                self.info.terms.append((slug, line))
        for attribute in (
            "data-api-component", "data-api-reference", "data-api-index",
            "data-api-list",
        ):
            if attribute in values:
                self.info.api_mounts.append((attribute, values[attribute].strip(), line))
        entry_classes = {
            "api-entry", "api-list", "api-reference", "api-details",
            "symbol-list", "symbol-index", "member-list", "method-list",
        }
        if entry_classes & set(values.get("class", "").split()):
            for section in self._sections:
                section.entry_markers += 1
        if identifier and re.match(r"api-(?:symbol|entry)-", identifier):
            for section in self._sections:
                section.entry_markers += 1
        if tag == "pre":
            for section in self._sections:
                section.pre_blocks += 1
        elif tag == "tr":
            for section in self._sections:
                section.table_rows += 1
        elif re.fullmatch(r"h[1-6]", tag):
            self._heading_sections = list(self._sections)
            self._heading_text = []
        if tag == "html":
            self.info.html_count += 1
            self.info.html_lang = values.get("lang", "").strip()
        elif tag == "title":
            self.info.title_count += 1
            self._in_title = True
        elif tag == "main":
            self.info.main_count += 1
        elif tag == "meta" and values.get("name", "").lower() == "viewport":
            if values.get("content", "").strip():
                self.info.viewport_count += 1


class Validator:
    def __init__(self, docs: Path, repo: Path, verbose: bool) -> None:
        self.docs = docs.resolve()
        self.repo = repo.resolve()
        self.verbose = verbose
        self.findings: List[Finding] = []
        self.notes: List[str] = []
        self.html: Dict[Path, HtmlInfo] = {}
        self.xml_ids: Dict[Path, Set[str]] = {}
        self.xml_links: List[Link] = []

    def fail(self, path: object, message: str, line: Optional[int] = None) -> None:
        try:
            display = posix(Path(path).resolve().relative_to(self.repo))
        except (OSError, ValueError, TypeError):
            display = posix(Path(path)) if isinstance(path, (str, Path)) else str(path)
        self.findings.append(Finding(display, message, line))

    def note(self, message: str) -> None:
        self.notes.append(message)

    def validate(self) -> int:
        if not self.docs.is_dir():
            self.fail(self.docs, "Docs root does not exist or is not a directory")
            return self.report()
        self.parse_html()
        self.parse_xml()
        glossary = self.load_js_data(
            self.docs / "assets" / "glossary-data.js",
            "ArdaGlossary",
            "const terms",
        )
        apis: Dict[ModuleConfig, Any] = {}
        for module in MODULES:
            apis[module] = self.load_js_data(
                self.docs / module.api_js_path,
                module.api_global,
                "window." + module.api_global,
            )
        self.validate_links(apis)
        self.validate_glossary(glossary)
        for module in MODULES:
            self.validate_api(apis[module], module)
        return self.report()

    def parse_html(self) -> None:
        paths = sorted(self.docs.rglob("*.html"))
        if not paths:
            self.fail(self.docs, "no HTML files found")
        for path in paths:
            parser = SiteHtmlParser(path, self.fail)
            try:
                parser.feed(path.read_text(encoding="utf-8-sig"))
                parser.close()
            except (OSError, UnicodeError, html.parser.HTMLParseError) as exc:
                self.fail(path, "cannot parse HTML: %s" % exc)
                continue
            info = parser.info
            self.html[path.resolve()] = info
            if info.html_count != 1:
                self.fail(path, "expected exactly one html element (found %d)" % info.html_count)
            if not info.html_lang:
                self.fail(path, "missing non-empty html lang attribute")
            if info.title_count != 1 or not "".join(info.title_text).strip():
                self.fail(path, "missing exactly one non-empty title")
            if info.viewport_count != 1:
                self.fail(path, "missing exactly one non-empty meta viewport")
            if info.main_count != 1:
                self.fail(path, "expected exactly one main element (found %d)" % info.main_count)
        self.note("HTML: %d file(s), %d id(s)" % (
            len(self.html), sum(len(item.ids) for item in self.html.values())
        ))

    def parse_xml(self) -> None:
        paths = sorted({
            path.resolve()
            for suffix in ("*.svg", "*.xml")
            for path in self.docs.rglob(suffix)
        })
        for path in paths:
            try:
                root = ET.parse(str(path)).getroot()
            except (OSError, ET.ParseError) as exc:
                self.fail(path, "cannot parse XML: %s" % exc)
                continue
            ids: Set[str] = set()
            for element in root.iter():
                identifier = element.attrib.get("id")
                if identifier:
                    if identifier in ids:
                        self.fail(path, "duplicate XML id %r" % identifier)
                    ids.add(identifier)
                for attribute, value in element.attrib.items():
                    local_attr = attribute.rsplit("}", 1)[-1]
                    if local_attr == "href" and value.strip():
                        self.xml_links.append(Link(path, value.strip(), "href"))
            self.xml_ids[path] = ids
            if root.tag.rsplit("}", 1)[-1].lower() == "svg":
                self.validate_svg(path, root, ids)
        self.note("SVG/XML: %d file(s), %d id(s)" % (
            len(paths), sum(len(ids) for ids in self.xml_ids.values())
        ))

    def validate_svg(self, path: Path, root: ET.Element, ids: Set[str]) -> None:
        if not root.attrib.get("viewBox", "").strip():
            self.fail(path, "SVG root is missing viewBox")
        children = list(root)
        titles = [e for e in children if e.tag.rsplit("}", 1)[-1] == "title"]
        descs = [e for e in children if e.tag.rsplit("}", 1)[-1] == "desc"]
        if not titles or not "".join(titles[0].itertext()).strip():
            self.fail(path, "SVG root is missing a non-empty title")
        if not descs or not "".join(descs[0].itertext()).strip():
            self.fail(path, "SVG root is missing a non-empty desc")
        labelled = set(root.attrib.get("aria-labelledby", "").split())
        title_id = titles[0].attrib.get("id", "") if titles else ""
        desc_id = descs[0].attrib.get("id", "") if descs else ""
        if not title_id or not desc_id or not {title_id, desc_id}.issubset(labelled):
            self.fail(
                path,
                "SVG aria-labelledby must reference the root title and desc ids",
            )
        for reference in labelled:
            if reference not in ids:
                self.fail(path, "SVG aria-labelledby references missing id %r" % reference)

    def canonical_api_page(self, module: ModuleConfig) -> Path:
        return (self.docs / module.docs_root / module.api_page).resolve()

    def generated_api_fragments(self, api: Any) -> Set[str]:
        fragments = self.api_symbol_ids(api)
        if not isinstance(api, dict):
            return fragments
        components = api.get("components", [])
        if isinstance(components, list):
            for component in components:
                if isinstance(component, dict):
                    identifier = component.get("id")
                    if isinstance(identifier, str) and identifier:
                        fragments.add("api-component-" + identifier)
        return fragments

    @staticmethod
    def api_symbol_ids(api: Any) -> Set[str]:
        fragments: Set[str] = set()
        if not isinstance(api, dict):
            return fragments
        symbols = api.get("symbols", [])
        if isinstance(symbols, list):
            for symbol in symbols:
                if isinstance(symbol, dict):
                    identifier = symbol.get("id")
                    if isinstance(identifier, str) and identifier:
                        fragments.add(identifier)
        return fragments

    def validate_links(self, apis: Dict[ModuleConfig, Any]) -> None:
        api_pages = {
            self.canonical_api_page(module): (
                module,
                self.generated_api_fragments(api),
                self.api_symbol_ids(api),
            )
            for module, api in apis.items()
        }
        feature_api_links: collections.Counter = collections.Counter()
        all_links = [
            link for info in self.html.values() for link in info.links
        ] + self.xml_links
        for link in all_links:
            raw = link.value
            if "\\" in raw:
                self.fail(link.source, "%s URL uses backslashes: %s" % (
                    link.attribute, raw
                ), link.line)
                continue
            if raw.startswith("/") or raw.startswith("//"):
                self.fail(link.source, "%s URL is root-relative: %s" % (
                    link.attribute, raw
                ), link.line)
                continue
            parsed = urllib.parse.urlsplit(raw)
            if parsed.scheme.lower() in EXTERNAL_SCHEMES or parsed.netloc:
                continue
            if parsed.scheme:
                continue
            decoded_path = urllib.parse.unquote(parsed.path)
            target = (link.source.parent / decoded_path).resolve() if decoded_path else link.source.resolve()
            try:
                target.relative_to(self.docs)
            except ValueError:
                self.fail(link.source, "relative URL escapes Docs root: %s" % raw, link.line)
                continue
            if target.is_dir():
                target = target / "index.html"
            if not target.exists():
                self.fail(link.source, "broken relative %s URL: %s" % (
                    link.attribute, raw
                ), link.line)
                continue
            fragment = urllib.parse.unquote(parsed.fragment)
            target_api = api_pages.get(target)
            is_feature_html = (
                link.attribute == "href"
                and link.source.resolve() in self.html
                and link.source.resolve() not in api_pages
            )
            if is_feature_html and target_api is not None:
                module, _generated_fragments, symbol_ids = target_api
                feature_api_links[module.name] += 1
                query = urllib.parse.parse_qs(
                    parsed.query, keep_blank_values=True
                )
                if "symbol" in query:
                    requested = query["symbol"][-1] if query["symbol"] else ""
                    self.fail(
                        link.source,
                        "noncanonical API query link requests symbol %r; "
                        "use %s#<%s symbol id>"
                        % (
                            requested,
                            module.api_page,
                            Path(module.api_js_path).name,
                        ),
                        link.line,
                    )
                if fragment and (
                    not fragment.startswith("api-")
                    or fragment not in _generated_fragments
                ):
                    self.fail(
                        link.source,
                        "API fragment link requests symbol id %r, which is not "
                        "a %s symbol id"
                        % (fragment, Path(module.api_js_path).name),
                        link.line,
                    )
            if fragment:
                known = self.html.get(target, None)
                ids = known.ids if known else self.xml_ids.get(target)
                generated = (
                    target_api is not None
                    and fragment in target_api[1]
                )
                if ids is not None and fragment not in ids and not generated:
                    self.fail(link.source, "broken fragment %r in %s" % (
                        fragment, raw
                    ), link.line)
        self.note("Links: %d href/src reference(s)" % len(all_links))
        for module in MODULES:
            self.note(
                "%s feature API links: %d canonical target reference(s) checked"
                % (module.name, feature_api_links[module.name])
            )

    @staticmethod
    def balanced(text: str, marker: str, opener: str) -> str:
        start = text.find(marker)
        if start < 0:
            raise ValueError("marker %r not found" % marker)
        start = text.find(opener, start + len(marker))
        if start < 0:
            raise ValueError("opening %r not found after %r" % (opener, marker))
        closer = {"{": "}", "[": "]"}[opener]
        depth = 0
        quote = ""
        escaped = False
        line_comment = False
        block_comment = False
        index = start
        while index < len(text):
            char = text[index]
            nxt = text[index + 1] if index + 1 < len(text) else ""
            if line_comment:
                if char in "\r\n":
                    line_comment = False
            elif block_comment:
                if char == "*" and nxt == "/":
                    block_comment = False
                    index += 1
            elif quote:
                if escaped:
                    escaped = False
                elif char == "\\":
                    escaped = True
                elif char == quote:
                    quote = ""
            elif char in "\"'":
                quote = char
            elif char == "/" and nxt == "/":
                line_comment = True
                index += 1
            elif char == "/" and nxt == "*":
                block_comment = True
                index += 1
            elif char == opener:
                depth += 1
            elif char == closer:
                depth -= 1
                if depth == 0:
                    return text[start:index + 1]
            index += 1
        raise ValueError("unterminated %r data after %r" % (opener, marker))

    def load_js_data(self, path: Path, global_name: str, fallback_marker: str) -> Any:
        if not path.is_file():
            self.fail(path, "required JavaScript data file is missing")
            return None
        node = shutil.which("node")
        if node:
            program = (
                "const fs=require('fs'),vm=require('vm');"
                "const box={window:{}};"
                "vm.runInNewContext(fs.readFileSync(process.argv[1],'utf8'),box,"
                "{filename:process.argv[1],timeout:10000});"
                "process.stdout.write(JSON.stringify(box.window[process.argv[2]]));"
            )
            try:
                result = subprocess.run(
                    [node, "-e", program, str(path), global_name],
                    check=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    timeout=15,
                )
                value = json.loads(result.stdout.decode("utf-8"))
                self.note("%s: parsed with Node" % path.name)
                return value
            except (OSError, subprocess.SubprocessError, UnicodeError, json.JSONDecodeError) as exc:
                self.note("%s: Node parse failed; using static extraction (%s)" % (
                    path.name, ascii_text(exc)
                ))
        try:
            text = path.read_text(encoding="utf-8-sig")
            if global_name in {"ArdaBackendApi", "ArdaRDGApi"}:
                return json.loads(self.balanced(text, fallback_marker, "{"))
            rows = json.loads(self.balanced(text, fallback_marker, "["))
            glossary: Dict[str, Dict[str, Any]] = {}
            for row in rows:
                if not isinstance(row, list) or len(row) != 6:
                    raise ValueError("glossary terms row is not a six-item array")
                slug, term, category, definition, project, related = row
                glossary[slug] = {
                    "id": "term-" + slug,
                    "slug": slug,
                    "term": term,
                    "category": category,
                    "definition": definition,
                    "project": project,
                    "related": [
                        {"slug": item, "href": "#term-" + item} for item in related
                    ],
                }
            self.note("%s: parsed with static extraction" % path.name)
            return glossary
        except (OSError, UnicodeError, ValueError, json.JSONDecodeError) as exc:
            self.fail(path, "cannot extract JavaScript object data: %s" % exc)
            return None

    def validate_glossary(self, glossary: Any) -> None:
        path = self.docs / "assets" / "glossary-data.js"
        if not isinstance(glossary, dict):
            if glossary is not None:
                self.fail(path, "ArdaGlossary must be an object")
            return
        glossary_page = self.html.get((self.docs / "glossary.html").resolve())
        if glossary_page is None:
            self.fail(self.docs / "glossary.html", "required glossary page is missing")
            glossary_ids: Set[str] = set()
        else:
            glossary_ids = glossary_page.ids
        slugs = set(glossary)
        for slug, item in glossary.items():
            if not isinstance(item, dict):
                self.fail(path, "glossary entry %r is not an object" % slug)
                continue
            if item.get("slug") != slug:
                self.fail(path, "glossary key %r does not match entry slug" % slug)
            anchor = "term-" + slug
            if item.get("id") != anchor:
                self.fail(path, "glossary entry %r has invalid id" % slug)
            if anchor not in glossary_ids:
                self.fail(self.docs / "glossary.html", "missing glossary anchor %r" % anchor)
        term_uses = 0
        for info in self.html.values():
            for slug, line in info.terms:
                term_uses += 1
                if slug not in slugs:
                    self.fail(info.path, "data-term %r is absent from glossary data" % slug, line)
                if "term-" + slug not in glossary_ids:
                    self.fail(info.path, "data-term %r has no glossary.html anchor" % slug, line)
        self.note("Glossary: %d term(s), %d data-term use(s)" % (len(slugs), term_uses))

    def validate_api(self, api: Any, module: ModuleConfig) -> None:
        path = self.docs / module.api_js_path
        if not isinstance(api, dict):
            if api is not None:
                self.fail(path, "%s must be an object" % module.api_global)
            return
        components = api.get("components")
        symbols = api.get("symbols")
        if not isinstance(components, list) or not isinstance(symbols, list):
            self.fail(path, "API data requires components and symbols arrays")
            return
        module_docs = (self.docs / module.docs_root).resolve()
        canonical_page = self.canonical_api_page(module)
        canonical_name = canonical_page.name
        api_pages = sorted(module_docs.rglob(module.api_page)) if module_docs.is_dir() else []
        if len(api_pages) != 1:
            self.fail(
                module_docs,
                "%s requires exactly one %s (found %d)"
                % (module.name, module.api_page, len(api_pages)),
            )
        canonical_info = self.html.get(canonical_page)
        if canonical_info is None:
            self.fail(canonical_page, "canonical module API page is missing")
        else:
            reference_mounts = [
                mount for mount in canonical_info.api_mounts
                if mount[0] == "data-api-reference"
            ]
            if len(reference_mounts) != 1:
                self.fail(
                    canonical_page,
                    "expected exactly one data-api-reference mount (found %d)"
                    % len(reference_mounts),
                )
        forbidden_mounts = {
            "data-api-component", "data-api-reference", "data-api-index",
            "data-api-list",
        }
        for html_path, info in self.html.items():
            try:
                html_path.relative_to(module_docs)
            except ValueError:
                continue
            if html_path == canonical_page:
                continue
            for attribute, value, line in info.api_mounts:
                if attribute in forbidden_mounts:
                    suffix = "=%r" % value if value else ""
                    self.fail(
                        html_path,
                        "feature page contains forbidden generated API mount %s%s"
                        % (attribute, suffix),
                        line,
                    )
            self.validate_feature_api_sections(info)
            self.validate_textbook_sections(info)
        component_map: Dict[str, Dict[str, Any]] = {}
        for index, component in enumerate(components):
            label = "component[%d]" % index
            if not isinstance(component, dict):
                self.fail(path, "%s is not an object" % label)
                continue
            missing = COMPONENT_REQUIRED - set(component)
            if missing:
                self.fail(path, "%s missing fields: %s" % (
                    label, ", ".join(sorted(missing))
                ))
            identifier = component.get("id")
            if not isinstance(identifier, str) or not identifier:
                self.fail(path, "%s has invalid id" % label)
                continue
            if identifier in component_map:
                self.fail(path, "duplicate API component id %r" % identifier)
            component_map[identifier] = component
            page = component.get("page")
            if page != canonical_name:
                self.fail(
                    path,
                    "component %r page must be canonical %s (found %r)"
                    % (identifier, canonical_name, page),
                )
        seen_ids: Set[str] = set()
        source_paths: Set[str] = set()
        counts: collections.Counter = collections.Counter()
        api_names: Set[str] = set()
        api_signatures: List[str] = []
        semantic_symbols: Dict[Tuple[str, str, str], str] = {}
        for index, symbol in enumerate(symbols):
            label = "symbol[%d]" % index
            if not isinstance(symbol, dict):
                self.fail(path, "%s is not an object" % label)
                continue
            missing = API_REQUIRED - set(symbol)
            if missing:
                self.fail(path, "%s missing fields: %s" % (
                    label, ", ".join(sorted(missing))
                ))
            identifier = symbol.get("id")
            if not isinstance(identifier, str) or not identifier:
                self.fail(path, "%s has invalid id" % label)
            elif not identifier.startswith("api-"):
                self.fail(
                    path,
                    "%s id is not a stable API fragment: %r"
                    % (label, identifier),
                )
            elif identifier in seen_ids:
                self.fail(path, "duplicate API symbol id %r" % identifier)
            else:
                seen_ids.add(identifier)
            name = symbol.get("name")
            if isinstance(name, str) and name:
                api_names.add(self.canonical_cpp_name(name))
            signature = symbol.get("signature")
            if isinstance(signature, str):
                api_signatures.append(signature)
            qualified_name = symbol.get("qualifiedName")
            kind = symbol.get("kind")
            if all(isinstance(value, str) and value.strip() for value in (
                qualified_name, kind, signature
            )):
                semantic_key = (
                    str(qualified_name).strip(),
                    str(kind).strip(),
                    re.sub(r"\s+", " ", str(signature)).strip(),
                )
                previous_id = semantic_symbols.get(semantic_key)
                if previous_id is not None:
                    self.fail(
                        path,
                        "duplicate API declaration %r (%s and %s)"
                        % (qualified_name, previous_id, identifier or label),
                    )
                else:
                    semantic_symbols[semantic_key] = str(identifier or label)
            component_id = symbol.get("component")
            component = component_map.get(component_id)
            if component is None:
                self.fail(path, "%s references invalid component %r" % (
                    identifier or label, component_id
                ))
            page = symbol.get("page")
            if not isinstance(page, str) or not page:
                self.fail(path, "%s has invalid page" % (identifier or label))
            else:
                page_path = (module_docs / page).resolve()
                if page_path not in self.html:
                    self.fail(path, "%s page does not exist: %s" % (
                        identifier or label, page
                    ))
                if page != canonical_name:
                    self.fail(
                        path,
                        "%s page must be canonical %s (found %r)"
                        % (identifier or label, canonical_name, page),
                    )
            source = symbol.get("source")
            if isinstance(source, str) and source:
                normalized = source.replace("\\", "/")
                source_paths.add(normalized)
                source_file = (self.repo / Path(normalized)).resolve()
                if not source_file.is_file():
                    self.fail(path, "%s source does not exist: %s" % (
                        identifier or label, source
                    ))
            else:
                self.fail(path, "%s has invalid source" % (identifier or label))
            counts[(str(component_id), str(kind))] += 1
            for field_name in ("name", "qualifiedName", "kind", "signature", "summary", "details"):
                if field_name in symbol and (
                    not isinstance(symbol[field_name], str) or not symbol[field_name].strip()
                ):
                    self.fail(path, "%s has empty required field %s" % (
                        identifier or label, field_name
                    ))
            for field_name in ("params", "related"):
                if field_name in symbol and not isinstance(symbol[field_name], list):
                    self.fail(path, "%s field %s must be an array" % (
                        identifier or label, field_name
                    ))
        headers = sorted(
            item.resolve()
            for item in (self.repo / module.source_public_root).rglob("*.h")
        )
        expected_sources = {
            posix(header.relative_to(self.repo)) for header in headers
        }
        if module.required_public_headers:
            actual_names = {header.name for header in headers}
            required_names = set(module.required_public_headers)
            for missing_name in sorted(required_names - actual_names):
                self.fail(
                    self.repo / module.source_public_root,
                    "%s required public header is missing: %s"
                    % (module.name, missing_name),
                )
            for unexpected_name in sorted(actual_names - required_names):
                self.fail(
                    self.repo / module.source_public_root / unexpected_name,
                    "%s has an unconfigured public header; add it to "
                    "required_public_headers" % module.name,
                )
        header_provenance: Set[str] = set()
        raw_header_provenance = api.get("headerProvenance", [])
        if not isinstance(raw_header_provenance, list):
            self.fail(path, "headerProvenance must be an array when present")
        else:
            for index, entry in enumerate(raw_header_provenance):
                label = "headerProvenance[%d]" % index
                if not isinstance(entry, str) or not entry.strip():
                    self.fail(path, "%s must be a non-empty string" % label)
                    continue
                if "\\" in entry:
                    self.fail(path, "%s must use forward slashes: %s" % (label, entry))
                normalized = entry.strip().replace("\\", "/")
                if normalized in header_provenance:
                    self.fail(path, "duplicate headerProvenance entry: %s" % normalized)
                header_provenance.add(normalized)
        for stale_source in sorted(header_provenance - expected_sources):
            self.fail(path, "headerProvenance is not a current public header: %s" % stale_source)
        represented_sources = source_paths | header_provenance
        include_only_sources = {
            posix(header.relative_to(self.repo))
            for header in headers
            if self.is_include_only_header(header)
        }
        configured_umbrellas = set(module.include_only_umbrellas)
        for umbrella in sorted(configured_umbrellas - expected_sources):
            self.fail(
                path,
                "configured include-only umbrella is not a current public "
                "header: %s" % umbrella,
            )
        for umbrella in sorted(configured_umbrellas & expected_sources):
            if umbrella not in include_only_sources:
                self.fail(
                    path,
                    "configured include-only umbrella contains public "
                    "declarations and cannot be exempt: %s" % umbrella,
                )
        include_only_sources &= configured_umbrellas
        for missing_source in sorted(
            expected_sources - represented_sources - include_only_sources
        ):
            self.fail(path, "public header absent from API source provenance: %s" % missing_source)
        for stale_source in sorted(source_paths - expected_sources):
            if stale_source.startswith(module.source_public_root.rstrip("/") + "/"):
                self.fail(path, "API source provenance is not a current public header: %s" % stale_source)
        declarations = self.extract_public_declarations(headers)
        uncovered: Dict[str, List[Tuple[str, int, str]]] = collections.defaultdict(list)
        signature_blob = "\n".join(api_signatures)
        for name, header, line, kind in declarations:
            canonical = self.canonical_cpp_name(name)
            if canonical in CPP_IGNORED:
                continue
            in_signature = re.search(
                r"(?<![A-Za-z0-9_])%s(?![A-Za-z0-9_])" % re.escape(name),
                signature_blob,
            )
            if canonical not in api_names and not in_signature:
                rel = posix(header.relative_to(self.repo))
                uncovered[canonical].append((rel, line, kind))
        for name in sorted(uncovered):
            locations = ", ".join(
                "%s:%d (%s)" % item for item in uncovered[name][:3]
            )
            self.fail(path, "likely uncovered public declaration %r; found at %s" % (
                name, locations
            ))
        callable_kinds = {
            "function", "method", "operator", "conversion operator",
            "constructor", "destructor",
        }
        declared_overloads: collections.Counter = collections.Counter(
            (
                posix(header.relative_to(self.repo)),
                self.canonical_cpp_name(name),
            )
            for name, header, _line, kind in declarations
            if kind == "callable"
        )
        documented_overloads: collections.Counter = collections.Counter(
            (
                str(symbol.get("source", "")).replace("\\", "/"),
                self.canonical_cpp_name(str(symbol.get("name", ""))),
            )
            for symbol in symbols
            if isinstance(symbol, dict) and symbol.get("kind") in callable_kinds
        )
        for (source, name), declaration_count in sorted(declared_overloads.items()):
            documented_count = documented_overloads[(source, name)]
            if documented_count < declaration_count:
                self.fail(
                    path,
                    "missing callable overload(s) for %r in %s: public header "
                    "has %d, API data has %d"
                    % (name, source, declaration_count, documented_count),
                )
        represented_count = len(expected_sources & represented_sources)
        exempt_count = len(include_only_sources - represented_sources)
        accounted_count = len(expected_sources & (represented_sources | include_only_sources))
        self.note(
            "%s API: %d component(s), %d symbol(s), %d/%d header(s) accounted "
            "(%d represented, %d include-only exempt)" % (
                module.name, len(component_map), len(symbols), accounted_count,
                len(expected_sources), represented_count, exempt_count,
            )
        )
        self.note("%s public declaration heuristic: %d candidate(s), %d uncovered name(s)" % (
            module.name, len(declarations), len(uncovered)
        ))
        for component_id in sorted(component_map):
            pieces = [
                "%s=%d" % (kind, count)
                for (component, kind), count in sorted(counts.items())
                if component == component_id
            ]
            self.note("%s API %s: %s" % (
                module.name, component_id, ", ".join(pieces) or "no symbols"
            ))

    def validate_textbook_sections(self, info: HtmlInfo) -> None:
        """Require stable, linkable structure on authored module chapters."""
        section_ids = {section.identifier for section in info.feature_sections}
        for section in info.feature_sections:
            if not section.identifier:
                self.fail(
                    info.path,
                    "textbook chapter section is missing an id",
                    section.line,
                )
            if not section.headings:
                label = "#%s" % section.identifier if section.identifier else "section"
                self.fail(
                    info.path,
                    "textbook chapter %s is missing a heading" % label,
                    section.line,
                )
        for required in ("objectives", "summary"):
            if required not in section_ids:
                self.fail(
                    info.path,
                    "textbook chapter is missing required #%s section" % required,
                )

    def validate_feature_api_sections(self, info: HtmlInfo) -> None:
        """Reject likely appended API catalogs while allowing curated link lists."""
        for section in info.feature_sections:
            heading = " ".join(section.headings).lower()
            catalog_heading = bool(re.search(
                r"\b(?:api reference|detailed reference|symbol index|"
                r"method index|member index|signature catalog)\b",
                heading,
            ))
            looks_like_dump = (
                section.entry_markers > 0
                or (section.pre_blocks >= 5 and section.api_links >= 5)
                or (section.table_rows >= 10 and section.api_links >= 5)
                or (catalog_heading and (
                    section.pre_blocks >= 2
                    or section.table_rows >= 5
                    or section.api_links >= 8
                ))
            )
            if looks_like_dump:
                label = "#%s" % section.identifier if section.identifier else "section"
                self.fail(
                    info.path,
                    "feature %s looks like an appended API dump "
                    "(%d API links, %d signatures, %d table rows)"
                    % (
                        label, section.api_links, section.pre_blocks,
                        section.table_rows,
                    ),
                    section.line,
                )

    @staticmethod
    def canonical_cpp_name(name: str) -> str:
        name = name.strip()
        name = re.sub(r"\s+", " ", name)
        name = re.sub(r"operator\s+", "operator ", name)
        return name

    @staticmethod
    def strip_cpp_comments(text: str) -> str:
        def preserve_lines(match: re.Match) -> str:
            return "\n" * match.group(0).count("\n")
        text = re.sub(r"/\*.*?\*/", preserve_lines, text, flags=re.S)
        return re.sub(r"//[^\r\n]*", "", text)

    def is_include_only_header(self, header: Path) -> bool:
        """Return true only when a header contains includes and no declarations."""
        try:
            text = self.strip_cpp_comments(header.read_text(encoding="utf-8-sig"))
        except (OSError, UnicodeError):
            return False
        includes = 0
        guard_names: Set[str] = set()
        for raw_line in text.splitlines():
            line = raw_line.strip()
            if not line:
                continue
            if re.fullmatch(r"#\s*include\s*[<\"].*[>\"]", line):
                includes += 1
                continue
            if re.fullmatch(r"#\s*pragma\s+once", line):
                continue
            guard = re.fullmatch(r"#\s*ifndef\s+([A-Za-z_]\w*)", line)
            if guard:
                guard_names.add(guard.group(1))
                continue
            guard = re.fullmatch(
                r"#\s*if\s+!\s*defined\s*\(\s*([A-Za-z_]\w*)\s*\)", line
            )
            if guard:
                guard_names.add(guard.group(1))
                continue
            define = re.fullmatch(r"#\s*define\s+([A-Za-z_]\w*)", line)
            if define and define.group(1) in guard_names:
                continue
            if re.fullmatch(r"#\s*(?:if|ifdef|elif)\b.*", line):
                continue
            if re.fullmatch(r"#\s*(?:else|endif)\b.*", line):
                continue
            return False
        return includes > 0

    @staticmethod
    def mask_cpp_function_bodies(text: str) -> str:
        """Preserve declarations while hiding implementation statements."""
        chars = list(text)
        boundary = 0
        index = 0
        while index < len(text):
            char = text[index]
            if char in ";}":
                boundary = index + 1
                index += 1
                continue
            if char != "{":
                index += 1
                continue
            prefix = text[boundary:index]
            is_type_or_namespace = re.search(
                r"\b(?:class|struct|union|enum|namespace)\b[^;{}]*$",
                prefix,
            )
            if ")" not in prefix or is_type_or_namespace:
                boundary = index + 1
                index += 1
                continue
            depth = 1
            end = index + 1
            quote = ""
            escaped = False
            while end < len(text) and depth:
                current = text[end]
                if quote:
                    if escaped:
                        escaped = False
                    elif current == "\\":
                        escaped = True
                    elif current == quote:
                        quote = ""
                elif current in "\"'":
                    quote = current
                elif current == "{":
                    depth += 1
                elif current == "}":
                    depth -= 1
                end += 1
            if depth:
                index += 1
                continue
            for hidden in range(index + 1, end - 1):
                if chars[hidden] not in "\r\n":
                    chars[hidden] = " "
            boundary = end
            index = end
        return "".join(chars)

    @staticmethod
    def mask_cpp_implementation(text: str) -> str:
        """Hide macro expansions and explicit non-public class regions."""
        def preserve_lines(match: re.Match) -> str:
            return "\n" * match.group(0).count("\n")
        def mask_default_class_private(match: re.Match) -> str:
            prefix = match.group("declaration")
            private = match.group("private")
            return prefix + ("\n" * private.count("\n"))
        text = re.sub(
            r"(?m)^[ \t]*#[ \t]*define(?:[^\r\n]*\\\r?\n)*[^\r\n]*",
            preserve_lines,
            text,
        )
        text = Validator.mask_cpp_function_bodies(text)
        # A class is private until its first public label; structs are public.
        default_private = re.compile(
            r"(?ms)^(?P<indent>[ \t]*)"
            r"(?P<declaration>class\s+[A-Za-z_]\w*[^;{}]*\{)"
            r"(?P<private>.*?)(?=^(?P=indent)public:|^(?P=indent)};)"
        )
        text = default_private.sub(mask_default_class_private, text)
        # Access labels and the closing class brace use the same indentation.
        # This avoids stopping at a nested class's more-indented closing brace.
        pattern = re.compile(
            r"(?ms)^([ \t]*)(?:private|protected):.*?"
            r"(?=^\1public:|^\1};)"
        )
        previous = None
        while previous != text:
            previous = text
            text = pattern.sub(preserve_lines, text)
        return text

    def extract_public_declarations(
        self, headers: Sequence[Path]
    ) -> List[Tuple[str, Path, int, str]]:
        records: List[Tuple[str, Path, int, str]] = []
        for header in headers:
            try:
                raw = header.read_text(encoding="utf-8-sig")
            except (OSError, UnicodeError) as exc:
                self.fail(header, "cannot read public header: %s" % exc)
                continue
            text = self.strip_cpp_comments(raw)
            undefined = set(re.findall(r"(?m)^\s*#\s*undef\s+([A-Za-z_]\w*)", text))
            for match in re.finditer(r"(?m)^\s*#\s*define\s+([A-Za-z_]\w*)", text):
                name = match.group(1)
                is_public_macro = (
                    name.startswith("ARDA_")
                    or (
                        name.startswith("ARDG_")
                        and not name.startswith("ARDG_INTERNAL_")
                    )
                )
                if is_public_macro and name not in undefined:
                    records.append((name, header, text.count("\n", 0, match.start()) + 1, "macro"))
            text = self.mask_cpp_implementation(text)
            for match in re.finditer(
                r"\b(?:enum\s+class|enum|struct|class)\s+([A-Za-z_]\w*)"
                r"(?=[^;{}]*\{)",
                text,
            ):
                records.append((
                    match.group(1), header,
                    text.count("\n", 0, match.start()) + 1, "type",
                ))
            for pattern in (
                r"\busing\s+([A-Za-z_]\w*)\s*=",
                r"\btypedef\b[^;{}]*?\b([A-Za-z_]\w*)\s*;",
            ):
                for match in re.finditer(pattern, text, flags=re.S):
                    records.append((
                        match.group(1), header,
                        text.count("\n", 0, match.start()) + 1, "alias",
                    ))
            for match in re.finditer(
                r"\b(?:inline\s+)?(?:static\s+)?constexpr\b[^;{}]*?"
                r"\b([A-Za-z_]\w*)\s*(?==|;)", text
            ):
                records.append((
                    match.group(1), header,
                    text.count("\n", 0, match.start()) + 1, "constant",
                ))
            for match in re.finditer(
                r"(?m)^[ \t]*(?!#)(?:mutable\s+)?"
                r"(?:[A-Za-z_]\w*(?:::\w+)*(?:\s*<[^;\n{}]+>)?[\s*&]+)+"
                r"(m(?:b[A-Z]|[A-Z])[A-Za-z0-9_]*)\s*(?:[=;{\[])", text
            ):
                records.append((
                    match.group(1), header,
                    text.count("\n", 0, match.start()) + 1, "member",
                ))
            records.extend(self.extract_enumerators(text, header))
            records.extend(self.extract_callables(text, header))
        unique: Dict[Tuple[str, str, int, str], Tuple[str, Path, int, str]] = {}
        for record in records:
            key = (record[0], str(record[1]), record[2], record[3])
            unique[key] = record
        return list(unique.values())

    def extract_enumerators(
        self, text: str, header: Path
    ) -> List[Tuple[str, Path, int, str]]:
        records: List[Tuple[str, Path, int, str]] = []
        pattern = re.compile(
            r"\benum(?:\s+class)?\s+[A-Za-z_]\w*(?:\s*:\s*[^{]+)?\s*\{"
        )
        for match in pattern.finditer(text):
            start = match.end() - 1
            depth = 0
            end = -1
            for index in range(start, len(text)):
                if text[index] == "{":
                    depth += 1
                elif text[index] == "}":
                    depth -= 1
                    if depth == 0:
                        end = index
                        break
            if end < 0:
                continue
            body = text[start + 1:end]
            offset = start + 1
            for piece in body.split(","):
                enum_match = re.match(r"\s*([A-Za-z_]\w*)", piece)
                if enum_match:
                    at = offset + enum_match.start(1)
                    records.append((
                        enum_match.group(1), header,
                        text.count("\n", 0, at) + 1, "enumerator",
                    ))
                offset += len(piece) + 1
        return records

    def extract_callables(
        self, text: str, header: Path
    ) -> List[Tuple[str, Path, int, str]]:
        records: List[Tuple[str, Path, int, str]] = []
        boundary = 0
        for match in re.finditer(r"[;{}]", text):
            chunk = text[boundary:match.start()]
            terminator = match.group(0)
            chunk_start = boundary
            boundary = match.end()
            if "(" not in chunk or len(chunk) > 1600:
                continue
            cleaned = re.sub(
                r"\[\[.*?\]\]",
                lambda item: " " * len(item.group(0)),
                chunk,
                flags=re.S,
            )
            candidates = list(re.finditer(
                r"(operator\s*(?:\(\)|\[\]|[!<>=+\-*/%&|^~]+|"
                r"[A-Za-z_]\w*)|~?[A-Za-z_]\w*)\s*\(",
                cleaned,
            ))
            if not candidates:
                continue
            name_match = candidates[0]
            prefix = cleaned[:name_match.start(1)].strip()
            if not prefix or prefix.startswith("#"):
                candidate_name = self.canonical_cpp_name(name_match.group(1))
                if candidate_name.startswith("operator"):
                    prefix = "operator"
                elif not re.search(
                    r"\b(?:class|struct)\s+%s\b"
                    % re.escape(candidate_name.lstrip("~")),
                    text,
                ):
                    continue
            if "=" in prefix or re.search(r"\b(return|if|for|while|switch|catch|sizeof)\b", prefix):
                continue
            name = self.canonical_cpp_name(name_match.group(1))
            plain = name.lstrip("~")
            if plain in CPP_IGNORED or plain.startswith("ARDA_"):
                continue
            if terminator == "{" or terminator == ";":
                records.append((
                    name, header,
                    text.count("\n", 0, chunk_start + name_match.start(1)) + 1,
                    "callable",
                ))
        return records

    def report(self) -> int:
        for note in self.notes:
            print(ascii_text("[INFO] " + note))
        findings = sorted(
            self.findings,
            key=lambda item: (item.path.lower(), item.line or 0, item.message),
        )
        for item in findings:
            location = item.path + (":%d" % item.line if item.line else "")
            print(ascii_text("[ERROR] %s: %s" % (location, item.message)))
        if self.verbose and not findings:
            print("[INFO] No validation findings.")
        print(ascii_text(
            "Validation %s: %d error(s)."
            % ("FAILED" if findings else "PASSED", len(findings))
        ))
        return 1 if findings else 0


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    script = Path(__file__).resolve()
    default_docs = (script.parent / ".." / ".." / ".." / "Docs").resolve()
    parser = argparse.ArgumentParser(
        description="Validate Ardashir static HTML, SVG, glossary, and backend API coverage."
    )
    parser.add_argument(
        "docs",
        nargs="?",
        type=Path,
        default=default_docs,
        help="static Docs directory (default: ../../../Docs relative to this script)",
    )
    parser.add_argument(
        "--repo-root",
        type=Path,
        help="repository root (default: parent of the Docs directory)",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="print detailed inventory summaries",
    )
    return parser.parse_args(argv)


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    docs = args.docs.resolve()
    repo = args.repo_root.resolve() if args.repo_root else docs.parent.resolve()
    return Validator(docs, repo, args.verbose).validate()


if __name__ == "__main__":
    sys.exit(main())
