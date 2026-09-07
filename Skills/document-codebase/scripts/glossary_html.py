"""Render the static glossary from the same definitions used by tooltips."""

import html
import re


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
        entries.append(
            f'        <article class="glossary-entry" id="{anchor}" data-glossary-entry '
            f'data-category="{category}" data-search="{term} {category} {definition} {project}">\n'
            f'          <div class="glossary-entry__meta"><h3>{term}</h3><span class="category">{category}</span></div>\n'
            f'          <p>{definition}</p>\n'
            f'          <p><strong>In Ardashir:</strong> {project}</p>\n'
            f'          <div class="glossary-related"><strong>Related:</strong><ul class="related-links">{related}</ul></div>\n'
            '        </article>'
        )
    pattern = r'(<div class="glossary-list">).*?(\n          </div>\n        </section>)'
    page, count = re.subn(pattern, lambda match: match[1] + "\n" + "\n".join(entries) + match[2], page, flags=re.S)
    if count != 1:
        raise ValueError("Expected exactly one static glossary list")
    return page
