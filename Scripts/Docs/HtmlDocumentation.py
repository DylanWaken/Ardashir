"""Shared, dependency-free page shell for generated documentation chapters."""

import html
import re


MODULE_PAGES = {
    "ArdaBackend": (
        ("index.html", "Overview"), ("quick-guide.html", "Quick guide"),
        ("resource-recipes.html", "Resource recipes"), ("gpu-allocator.html", "GPU allocator"),
        ("cuda-interop.html", "CUDA operands"), ("cuda-sequences.html", "CUDA sequences"),
        ("cuda-external-calls.html", "External CUDA calls"),
        ("cuda-texture-buffers.html", "CUDA texture buffers"),
        ("validation.html", "GPU validation"), ("api-reference.html", "API reference"),
        ("documentation.html", "Documentation maintenance"),
    ),
    "ArdaRDG": (
        ("index.html", "Overview"), ("getting-started.html", "Getting started"),
        ("registering-nodes.html", "Registering nodes"), ("node-recipes.html", "Node recipes"),
        ("resources.html", "Resources"), ("inductor.html", "Inductor developer guide"),
        ("inductor-pipelines.html", "Pipeline assembly"), ("inductor-bindings.html", "Binding preparation"),
        ("batching.html", "Batching and parallel edits"),
        ("compilation.html", "Compilation"), ("execution.html", "Execution"),
        ("api-reference.html", "API reference"), ("documentation.html", "Documentation maintenance"),
    ),
    "ArdaTrace": (
        ("index.html", "Overview"), ("getting-started.html", "Getting started"),
        ("instrumentation.html", "Instrumentation"), ("viewer-and-format.html", "Viewer and capture format"),
    ),
    "ArdaScene": (("index.html", "Archived scene proposal"),),
    "Unreal": (
        ("scene-representation.html", "Scene representation"),
        ("nanite.html", "Nanite"),
        ("lumen.html", "Lumen"),
        ("material-compiler.html", "Material graph compilation"),
    ),
}


def heading_id(text):
    """Stable fragment spelling matching the previous GitHub heading links."""
    plain = html.unescape(re.sub(r"<[^>]*>", "", text)).lower()
    plain = re.sub(r"[^\w\- ]", "", plain)
    return plain.replace(" ", "-")


def html_page(title, body, module, module_home="index.html", toc=()):
    """Wrap authored HTML; body owns its H1, toc contains (fragment, title) pairs."""
    escape = html.escape
    pages = MODULE_PAGES.get(module, ((module_home, "Overview"),))
    links = "\n".join(
        f'        <a href="{escape(url, quote=True)}">{escape(label)}</a>' for url, label in pages
    )
    contents = ""
    if toc:
        entries = "\n".join(
            f'        <li><a href="#{escape(identifier, quote=True)}">{escape(label)}</a></li>'
            for identifier, label in toc
        )
        contents = f'''<nav class="toc" aria-label="On this page">
      <p><strong>On this page</strong></p>
      <ul>
{entries}
      </ul>
    </nav>'''
    page_body = body.rstrip()
    if contents:
        page_body = re.sub(r"(</h1>)", lambda match: match[1] + "\n" + contents, page_body, count=1)
    return f'''<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta name="description" content="{escape(title, quote=True)} — Ardashir engineering documentation.">
  <title>{escape(title)} | Ardashir</title>
  <link rel="stylesheet" href="../assets/site.css">
  <script src="../assets/glossary-data.js" defer></script>
  <script src="../assets/site.js" defer></script>
</head>
<body>
  <a class="skip-link" href="#main-content">Skip to content</a>
  <header class="site-header">
    <a class="site-brand" href="../index.html">Ardashir Docs</a>
    <nav aria-label="Primary">
      <a href="../ArdaBackend/index.html">ArdaBackend</a>
      <a href="../ArdaRDG/index.html">ArdaDependencyGraph</a>
      <a href="../glossary.html">Glossary</a>
    </nav>
  </header>
  <div class="site-layout">
    <aside class="sidebar" aria-label="{escape(module, quote=True)} pages">
      <h2>{escape(module)}</h2>
      <nav>
{links}
      </nav>
    </aside>
    <main id="main-content">
      <nav class="breadcrumbs" aria-label="Breadcrumb">
        <a href="../index.html">Docs</a> / <a href="{escape(module_home, quote=True)}">{escape(module)}</a>
      </nav>
      <article>
{page_body}
      </article>
    </main>
  </div>
  <footer class="site-footer"><p>{escape(module)} · Ardashir engineering documentation</p></footer>
</body>
</html>
'''
