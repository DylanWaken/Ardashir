"""Render the static glossary from the same definitions used by tooltips."""

import html
import re


# Topic links belong to the static chapter index; definitions remain in the
# six-field glossary-data.js rows shared with tooltips and the offline validator.
TOPIC_LINKS = {
    "topological-order": ("ArdaRDG/compilation.html#objectives", "Compilation objectives and legal orders"),
    "critical-path": ("ArdaRDG/compilation.html#seeds", "Critical-path scheduling seeds"),
    "list-scheduling": ("ArdaRDG/compilation.html#seeds", "Constructing scheduling seeds"),
    "makespan": ("ArdaRDG/compilation.html#cost", "Estimating schedule cost"),
    "reachability": ("ArdaRDG/compilation.html#search", "Legal search candidates and memory ordering"),
    "incumbent": ("ArdaRDG/compilation.html#search", "Searching from the best accepted candidate"),
    "exponential-moving-average": ("ArdaRDG/compilation.html#adaptive", "Timing estimates and adaptive scheduling"),
}


def synchronize_glossary_page(page, glossary):
    entries = []
    for item in sorted(glossary.values(), key=lambda value: value["term"].casefold()):
        term, category, definition, project = (
            html.escape(item[key], quote=True)
            for key in ("term", "category", "definition", "project")
        )
        anchor = html.escape(item["id"], quote=True)
        related = "".join(
            '<li><a href="#term-%s">%s</a></li>'
            % (html.escape(link["slug"], quote=True),
               html.escape(glossary[link["slug"]]["term"]))
            for link in item["related"]
        )
        topic = ""
        if item["slug"] in TOPIC_LINKS:
            href, label = TOPIC_LINKS[item["slug"]]
            topic = (
                '          <p><strong>Read more:</strong> <a href="%s">%s</a></p>\n'
                % (html.escape(href, quote=True), html.escape(label))
            )
        entries.append(
            f'        <article class="glossary-entry" id="{anchor}" data-glossary-entry '
            f'data-category="{category}" data-search="{term} {category} {definition} {project}">\n'
            f'          <div class="glossary-entry__meta"><h3>{term}</h3><span class="category">{category}</span></div>\n'
            f'          <p>{definition}</p>\n'
            f'          <p><strong>In Ardashir:</strong> {project}</p>\n'
            f'          <div class="glossary-related"><strong>Related:</strong><ul class="related-links">{related}</ul></div>\n'
            f'{topic}'
            '        </article>'
        )
    pattern = r'(<div class="glossary-list">).*?(\n          </div>\n        </section>)'
    page, count = re.subn(pattern, lambda match: match[1] + "\n" + "\n".join(entries) + match[2], page, flags=re.S)
    if count != 1:
        raise ValueError("Expected exactly one static glossary list")
    return page
