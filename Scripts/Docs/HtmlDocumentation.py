"""Shared, dependency-free page shell for generated documentation chapters."""

import argparse
import html
import json
import posixpath
from pathlib import Path
from html.parser import HTMLParser
import re


MODULE_PAGES = {
    "ArdaBackend": (
        ("Start here", (
            ("index.html", "Overview"), ("getting-started.html", "Getting started"),
            ("quick-guide.html", "Quick guide"), ("architecture.html", "Architecture"),
        )),
        ("Rendering hardware interface", (
            ("backend-modules.html", "Backend modules"), ("rhi.html", "Resources and memory"),
            ("resource-recipes.html", "Resource recipes"), ("gpu-allocator.html", "GPU allocator"),
            ("commands.html", "Commands and submission"), ("external-interop.html", "External devices and resources"),
            ("shaders.html", "Shaders"), ("pipelines.html", "Pipeline states"),
            ("presentation.html", "Presentation"), ("diagnostics.html", "Diagnostics"),
        )),
        ("CUDA interoperability", (
            ("cuda-graphics.html", "How CUDA works with graphics"), ("cuda-interop.html", "Precompiled CUDA operands"),
            ("cuda-sequences.html", "CUDA sequences"), ("cuda-external-calls.html", "External CUDA calls"),
            ("cuda-texture-buffers.html", "CUDA texture buffers"), ("cuda-backend-plan.html", "CUDA implementation record"),
        )),
        ("Reference and testing", (
            ("api-reference.html", "API reference"), ("rhi-feature-guide.html", "RHI feature guide"),
            ("capability-recipes.html", "Capability recipes"), ("gpu-examples.html", "Complete GPU examples"),
            ("validation.html", "GPU validation setup"), ("validation-tests.html", "Validation test suites"),
            ("unreal-rhi-audit.html", "Unreal RHI comparison"), ("unreal-rhi-parity-plan.html", "Unreal RHI parity review"),
            ("simplification-review.html", "Backend and graph review"), ("documentation.html", "Maintain this documentation"),
        )),
    ),
    "ArdaRDG": (
        ("Start here", (
            ("index.html", "Overview"), ("getting-started.html", "Getting started"),
            ("native-compute.html", "Write a native compute node"), ("quick-guide.html", "Renderer quick guide"),
        )),
        ("Write graph operations", (
            ("passes.html", "Typed node definitions"), ("registering-nodes.html", "Register a graph node"),
            ("node-recipes.html", "Node recipes"), ("resources.html", "Resource handles and storage"),
            ("feature-requirements.html", "Feature requirements"), ("batching.html", "Batching and parallel edits"),
        )),
        ("Compile and run graphs", (
            ("architecture.html", "Architecture"), ("inductor.html", "ArdaInductor developer guide"),
            ("compilation.html", "Compilation"), ("inductor-pipelines.html", "Pipeline assembly"),
            ("inductor-bindings.html", "Binding preparation"), ("execution.html", "Execution and completion"),
            ("debugging.html", "Validation and debugging"),
        )),
        ("Reference", (
            ("api-reference.html", "API reference"), ("feature-modularity-audit.html", "Native feature coverage"),
            ("documentation.html", "Maintain this documentation"),
        )),
    ),
    "ArdaNativeCompute": (("Native compute", (
        ("index.html", "Overview"), ("providers.html", "Choose a GPU provider"),
        ("../ArdaRDG/native-compute.html", "Write a native compute node"),
        ("api-reference.html", "API reference"),
    )),),
    "Essays": (("Engineering essays", (("arda-graph-and-inductor.html", "How Arda turns a graph into GPU work"),)),),
    "": (("Start here", (
        ("index.html", "Overview"), ("ArdaRDG/getting-started.html", "Run your first graph"),
        ("ArdaBackend/quick-guide.html", "Set up a GPU backend"), ("glossary.html", "Glossary"),
    )),),
    "ArdaTrace": (("ArdaTrace", (
        ("index.html", "Overview"), ("getting-started.html", "Getting started"),
        ("instrumentation.html", "Instrumentation"), ("viewer-and-format.html", "Viewer and capture format"),
    )),),
    "ArdaScene": (("ArdaScene", (("index.html", "Archived scene proposal"),)),),
}

MODULE_NAMES = {"": "Documentation", "ArdaRDG": "ArdaDependencyGraph"}
DOCS = Path(__file__).resolve().parents[2] / "Docs"
MODULES = (("ArdaBackend", "GPU backend"), ("ArdaRDG", "Dependency graph"), ("ArdaNativeCompute", "Native compute"),
           ("ArdaGraph", "ArdaGraph"), ("ArdaTests", "ArdaTests"), ("ArdaTrace", "ArdaTrace"), ("ArdaScene", "ArdaScene"))
HOME_MODULE_ANCHORS = {"ArdaGraph": "module-ardagraph", "ArdaTests": "module-ardatests", "ArdaTrace": "module-ardatrace"}
SECTION_LINKS = (("index.html", "Overview", "book"), ("ArdaRDG/getting-started.html", "Guides", "guide"),
                 ("ArdaNativeCompute/api-reference.html", "Reference", "layers"))
ICONS = {
    "arrow": '<path d="M5 12h14m-6-6 6 6-6 6"/>',
    "previous": '<path d="M19 12H5m6-6-6 6 6 6"/>',
    "down": '<path d="m6 9 6 6 6-6"/>',
    "book": '<path d="M3 4h6a3 3 0 0 1 3 3v14a4 4 0 0 0-4-3H3zm18 0h-6a3 3 0 0 0-3 3v14a4 4 0 0 1 4-3h5z"/>',
    "guide": '<path d="m12 2 10 5-10 5L2 7zm-6 8v6c4 4 8 4 12 0v-6m4-3v8"/>',
    "layers": '<path d="m12 3 10 5-10 5L2 8zm-10 9 10 5 10-5M2 16l10 5 10-5"/>',
    "menu": '<path d="M3 6h18M3 12h18M3 18h18"/>',
    "system": '<rect x="2" y="3" width="20" height="14" rx="2"/><path d="M12 17v4m-4 0h8"/>',
    "sun": '<circle cx="12" cy="12" r="4"/><path d="M12 2v2m0 16v2M2 12h2m16 0h2M5 5l1.5 1.5m11 11L19 19M5 19l1.5-1.5m11-11L19 5"/>',
    "moon": '<path d="M20.8 13A9 9 0 0 1 11 3.2 9 9 0 1 0 20.8 13Z"/>',
    "search": '<circle cx="10.5" cy="10.5" r="6.5"/><path d="m16 16 5 5"/>',
    "close": '<path d="m6 6 12 12M6 18 18 6"/>',
    "list": '<path d="M4 4h16v16H4zm5 0v16"/>',
}


def icon(name):
    return (f'<svg class="icon icon-{name}" width="16" height="16" viewBox="0 0 24 24" '
            f'fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" '
            f'stroke-linejoin="round" aria-hidden="true">{ICONS[name]}</svg>')


def heading_id(text):
    plain = html.unescape(re.sub(r"<[^>]*>", "", text)).lower()
    return re.sub(r"[^\w\- ]", "", plain).replace(" ", "-")


def text_only(markup):
    return " ".join(html.unescape(re.sub(r"<[^>]+>", "", markup)).split())


def anchor(href, label, current="", image=""):
    active = ' aria-current="page"' if current and href == current else ""
    return f'<a href="{html.escape(href, quote=True)}"{active}>{image}{html.escape(label)}</a>'


def groups_for(module, module_home):
    return MODULE_PAGES.get(module, ((module, ((module_home, "Overview"),)),))


def chapter_nav(module, page, module_home):
    sections = []
    for index, (name, pages) in enumerate(groups_for(module, module_home)):
        links = "\n".join(anchor(url, label, page) for url, label in pages)
        open_group = " open" if index < 2 or any(url == page for url, _ in pages) else ""
        sections.append(f'<details class="sidebar-group"{open_group}><summary>{html.escape(name)}{icon("down")}</summary>'
                        f'<div class="sidebar-links">{links}</div></details>')
    return '<nav class="sidebar-chapters" aria-label="Documentation navigation">' + "\n".join(sections) + '</nav>'


def relative(module, url):
    return posixpath.relpath(url, module or ".")


def sections_nav(module, page):
    category = 2 if page in ("api-reference.html", "glossary.html") else 1 if module else 0
    links = []
    for index, (url, label, glyph) in enumerate(SECTION_LINKS):
        href = relative(module, url)
        links.append(anchor(href, label, href if index == category else "", icon(glyph)))
    return '<nav class="sidebar-sections" aria-label="Documentation sections">' + "\n".join(links) + '</nav>'


def module_sidebar(module, current_page="", module_home="index.html"):
    return ('<aside class="sidebar" aria-label="Docs sidebar"><div class="sidebar-inner">'
            + sections_nav(module, current_page) + '<hr>'
            + chapter_nav(module, current_page, module_home) + '</div></aside>')


def module_links(page, include_summaries=False):
    links = []
    for module, label in MODULES:
        target = module + "/" + page
        if (DOCS / target).is_file():
            links.append((target, label))
        elif include_summaries and module in HOME_MODULE_ANCHORS:
            links.append(("index.html#" + HOME_MODULE_ANCHORS[module], label))
    return links


def switcher(module):
    links = "\n".join(anchor(relative(module, url), label) for url, label in module_links("index.html", include_summaries=True))
    return (f'<details class="section-switcher"><summary aria-label="Choose a module">Modules{icon("down")}</summary>'
            f'<nav aria-label="Modules">{links}</nav></details>')


def top_navigation(module):
    docs = anchor(relative(module, "index.html"), "Docs")
    essays = anchor(relative(module, "Essays/arda-graph-and-inductor.html"), "Essays")
    glossary = anchor(relative(module, "glossary.html"), "Glossary")
    groups = []
    for name, target in (("Guides", "index.html"), ("Reference", "api-reference.html")):
        links = "".join(anchor(relative(module, url), label) for url, label in module_links(target))
        groups.append(f'<details class="nav-popover"><summary>{name}{icon("down")}</summary><div>{links}</div></details>')
    return docs + essays + "".join(groups) + glossary


def shell_header(module, page, title, module_home, toc):
    home = relative(module, "index.html")
    feature = relative(module, "ArdaRDG/native-compute.html")
    menu = top_navigation(module)
    outline_links = "".join(anchor("#" + identifier, label) for identifier, label in toc)
    mobile_outline = (f'<details class="sidebar-group mobile-outline"><summary>On this page{icon("down")}</summary>'
                      f'<div class="sidebar-links">{outline_links}</div></details>') if toc else ""
    search = (f'<button type="button" class="search-toggle" data-doc-search aria-label="Search documentation" hidden>'
              f'{icon("search")}<span>Search</span><kbd>Ctrl K</kbd></button>')
    choices = "".join(f'<button type="button" role="menuitemradio" tabindex="-1" aria-checked="false" data-theme-choice="{value}">'
                      f'{icon(glyph)}{value.title()}</button>' for value, glyph in
                      (("dark", "moon"), ("light", "sun"), ("system", "system")))
    theme = (f'<div class="theme-control" hidden><button type="button" class="theme-toggle" data-theme-toggle '
             f'aria-label="Change theme" aria-haspopup="menu" aria-expanded="false" aria-controls="theme-menu">'
             f'{icon("moon")}{icon("sun")}{icon("system")}</button>'
             f'<div class="theme-menu" id="theme-menu" role="menu" aria-label="Theme" hidden>{choices}</div></div>')
    return f'''<header class="site-header" id="top">
  <a class="announcement" href="{feature}"><span aria-hidden="true">//</span> Write a native compute node {icon("arrow")}</a>
  <div class="site-header__bar"><div class="site-header__inner">
    <a class="site-brand" href="{home}" aria-label="Ardashir home">Ardashir</a>
    <nav class="site-nav" aria-label="Primary">{menu}</nav>
    <div class="header-tools">{switcher(module)}{search}{theme}
      <details class="mobile-nav"><summary aria-label="Navigation menu">{icon("menu")}</summary>
        <div class="mobile-nav-panel"><nav aria-label="Primary navigation">{menu}</nav>{search}</div>
      </details>
    </div>
  </div></div>
</header>
<nav class="mobile-docs" aria-label="Docs menu"><details class="docs-menu"><summary>{icon("list")}<span>Docs menu</span><span aria-hidden="true">/</span><span class="current-title">{html.escape(title)}</span>{icon("down")}</summary>
  <div class="mobile-docs-panel">{sections_nav(module, page)}<hr>{chapter_nav(module, page, module_home)}{mobile_outline}</div>
</details>{switcher(module)}</nav>'''


def page_navigation(module, page, module_home):
    pages = [(url, label) for _, group in groups_for(module, module_home) for url, label in group]
    index = next((i for i, (url, _) in enumerate(pages) if page == url), None)
    if index is None:
        return ""
    links = []
    for neighbor, rel, label, glyph in ((index - 1, "prev", "Previous", "previous"), (index + 1, "next", "Next", "arrow")):
        if 0 <= neighbor < len(pages):
            url, name = pages[neighbor]
            direction = icon(glyph) + label if rel == "prev" else label + icon(glyph)
            links.append(f'<a href="{html.escape(url)}" rel="{rel}"><span class="page-nav-label">{direction}</span>'
                         f'<span class="page-nav-title">{html.escape(name)}</span></a>')
    return '<nav class="page-nav" aria-label="Document navigation">' + "".join(links) + '</nav>' if links else ""


def footer(module):
    groups = []
    for label, links in (
        ("Documentation", (("index.html", "Overview"), ("ArdaRDG/getting-started.html", "Getting started"), ("Essays/arda-graph-and-inductor.html", "Engineering essays"))),
        ("Modules", module_links("index.html", include_summaries=True)),
        ("Reference", module_links("api-reference.html") + [("glossary.html", "Glossary")]),
    ):
        anchors = "".join(anchor(relative(module, url), name) for url, name in links)
        groups.append(f'<div class="footer-group"><p>{label}</p>{anchors}</div>')
    return ('<footer class="site-footer"><div class="site-footer__inner">'
            + f'<a class="site-brand" href="{relative(module, "index.html")}">Ardashir</a>' + "".join(groups) + '</div></footer>')


class Outline(HTMLParser):
    """Read authored headings without indexing nested cards or generated API entries."""

    def __init__(self):
        super().__init__()
        self.sections = []
        self.entries = []
        self.title = None
        self.identifier = ""

    def handle_starttag(self, tag, attributes):
        attr = dict(attributes)
        if tag == "section":
            self.sections.append(attr.get("id", ""))
        if tag == "h2" and len(self.sections) <= 1:
            self.identifier = attr.get("id") or (self.sections[-1] if self.sections else "")
            self.title = []

    def handle_data(self, value):
        if self.title is not None:
            self.title.append(value)

    def handle_endtag(self, tag):
        if tag == "section" and self.sections:
            self.sections.pop()
        if tag == "h2" and self.title is not None:
            if self.identifier:
                self.entries.append((self.identifier, "".join(self.title).strip()))
            self.title = None


def outline_for(body):
    parser = Outline()
    parser.feed(body)
    return parser.entries


def shell(module, page, title, body, toc, module_home="index.html"):
    entries = "".join(f'<li>{anchor("#" + identifier, name)}</li>' for identifier, name in toc)
    outline = ('<nav class="toc" aria-label="On this page"><p class="toc-title">On this page</p>'
               f'<ul>{entries}</ul></nav>') if entries else ""
    navigation = page_navigation(module, page, module_home)
    navigation_line = "\n    " + navigation if navigation else ""
    return f'''<a class="skip-link" href="#main-content">Skip to content</a>
{shell_header(module, page, title, module_home, toc)}
<div class="site-layout">
  {module_sidebar(module, page, module_home)}
  <main id="main-content" tabindex="-1">
    <article class="prose" data-doc-article>
{body.strip()}
    </article>{navigation_line}
  </main>
  <aside class="page-outline" aria-label="Page outline">{outline}</aside>
</div>
{footer(module)}
<dialog class="doc-search" aria-label="Search documentation"><form method="dialog">
  <div class="search-field">{icon("search")}<input type="search" aria-label="Search documentation" placeholder="Search documentation" autocomplete="off"><button type="submit" aria-label="Close search">{icon("close")}</button></div>
  <div class="search-results" aria-live="polite"></div>
</form></dialog>'''


def document(title, description, page_body, module, scripts=(), styles=()):
    assets = relative(module, "assets")
    css = [f'<link rel="stylesheet" href="{assets}/site.css">']
    css.extend(f'<link rel="stylesheet" href="{html.escape(path)}">' for path in styles)
    files = ("glossary-data.js", *scripts, "doc-search-index.js", "site.js")
    js = [f'<script src="{assets}/{filename}" defer></script>' for filename in files]
    # Apply the saved appearance before CSS or large static API references can paint.
    appearance = """<script>
    (() => {
      let preference = "dark";
      try {
        preference = localStorage.getItem("arda-theme") || preference;
      } catch {
        // Local files and privacy settings can disable storage.
      }
      const systemLight = preference === "system" && !matchMedia("(prefers-color-scheme: dark)").matches;
      document.documentElement.dataset.theme = preference === "light" || systemLight ? "light" : "dark";
    })();
  </script>"""
    resources = "\n  ".join([appearance] + css + js)
    return f'''<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta name="color-scheme" content="light dark">
  <meta name="description" content="{html.escape(description, quote=True)}">
  <title>{html.escape(title)}</title>
  {resources}
</head>
<body>
{page_body}
</body>
</html>
'''


def html_page(title, body, module, module_home="index.html", toc=(), description="", current_page=""):
    page = shell(module, current_page, title, body, toc or outline_for(body), module_home)
    return document(f"{title} | Ardashir", description or title, page, module)


def chapter_markup(path):
    """Keep each hand-authored article verbatim while refreshing its surrounding navigation."""
    page = path.read_text(encoding="utf-8-sig")
    location = path.relative_to(DOCS)
    module = "" if location.parent == Path(".") else location.parent.as_posix()
    main = re.search(r'<main\b[^>]*>(.*?)</main>', page, re.S)[1]
    article = re.search(r'<article\b[^>]*>(.*)</article>', main, re.S)
    root_landing = location.as_posix() == "index.html" and "data-doc-article" not in main
    body = article[1] if article and not root_landing else main
    body = re.sub(r'<nav class="(?:page-nav|toc)"[^>]*>.*?</nav>', "", body, flags=re.S)
    body = re.sub(r'<p class="(?:eyebrow|kicker|page-eyebrow)">.*?</p>', "", body, flags=re.S)
    # Keep intentional newlines in code examples and strip empty indentation from removed menus.
    body = re.sub(r"^[ \t]+$", "", body, flags=re.M)
    if location.as_posix() == "index.html":
        for name, identifier in HOME_MODULE_ANCHORS.items():
            body = body.replace(f"<h3>{name}</h3>", f'<h3 id="{identifier}">{name}</h3>')
        body = body.replace("<p><code>ArdaTrace</code>", '<p id="module-ardatrace"><code>ArdaTrace</code>')
    # Authored anchors can point to useful subheadings as well as H2s.
    original_toc = re.search(r'<nav class="toc"[^>]*>(.*?)</nav>', page, re.S)
    toc = [(html.unescape(url), text_only(label)) for url, label in
           re.findall(r'<a\b[^>]*href="#([^"]+)"[^>]*>(.*?)</a>', original_toc[1], re.S)] if original_toc else []
    if not toc:
        toc = outline_for(body)
    title = text_only(re.search(r'<h1\b[^>]*>(.*?)</h1>', body, re.S)[1])
    description = html.unescape(re.search(r'<meta name="description" content="([^"]*)"', page)[1])
    page_title = text_only(re.search(r'<title>(.*?)</title>', page, re.S)[1])
    scripts = re.findall(r'<script src="(?:\.\./)?assets/([^"/]+-api\.js)"', page)
    rendered = document(page_title, description, shell(module, path.name, title, body, toc), module, scripts)
    search_entry = [location.as_posix(), title, MODULE_NAMES.get(module, module), description, toc]
    return rendered, search_entry


def update_pages(write):
    entries = []
    changes = []
    for path in sorted(DOCS.rglob("*.html")):
        if path.relative_to(DOCS).as_posix() == "Essays/arda-graph-and-inductor.html":
            from arda_graph_essay.build import render
            rendered, _, _ = render()
            headings = re.search(r'<nav class="toc"[^>]*>(.*?)</nav>', rendered, re.S)[1]
            toc = [(html.unescape(url), text_only(name)) for url, name in
                   re.findall(r'<a href="#([^"]+)">(.*?)</a>', headings, re.S)]
            entries.append([path.relative_to(DOCS).as_posix(), text_only(re.search(r'<h1>(.*?)</h1>', rendered)[1]),
                            "Essays", html.unescape(re.search(r'<meta name="description" content="([^"]*)"', rendered)[1]), toc])
        else:
            rendered, entry = chapter_markup(path)
            entries.append(entry)
        if path.read_text(encoding="utf-8-sig") != rendered:
            changes.append((path, rendered))
    index = DOCS / "assets" / "doc-search-index.js"
    data = "// Generated by Scripts/Docs/HtmlDocumentation.py.\nwindow.ArdaDocPages = [\n"
    data += ",\n".join("  " + json.dumps(row, ensure_ascii=False, separators=(",", ":")) for row in entries) + "\n];\n"
    if not index.exists() or index.read_text(encoding="utf-8") != data:
        changes.append((index, data))
    if write:
        for path, content in changes:
            path.write_text(content, encoding="utf-8", newline="\n")
    elif changes:
        for path, _ in changes:
            print("Stale layout:", path.relative_to(DOCS))
        return 1
    print(f"{len(entries)} documentation pages, {len(changes)} updated files")
    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--write", action="store_true", help="Update the HTML shell and page-search index.")
    mode.add_argument("--check", action="store_true", help="Check the committed HTML shell and page-search index.")
    raise SystemExit(update_pages(parser.parse_args().write))
