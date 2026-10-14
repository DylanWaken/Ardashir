"""Build the standalone Arda graph essay from authored chapters and frozen evidence.

python Scripts/Docs/arda_graph_essay/build.py [--check] [--verify-source]
Source verification is optional because the essay explicitly studies a working-tree
snapshot newer than the repository's published source. CI checks the frozen article.
"""
from pathlib import Path
from html import escape
from html.parser import HTMLParser
import argparse
import hashlib
import json
import math
import re

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
OUT = ROOT / 'Docs/Essays/arda-graph-and-inductor.html'
TITLE = 'How Arda Turns a Graph into GPU Work'
DECK = 'A detailed journey through ArdaGraph, typed dependency graphs, Inductor compilation, offline model export, memory planning, scheduling, and the runtime that turns a declared program into completed GPU work.'


class Headings(HTMLParser):
    def __init__(self):
        super().__init__()
        self.chapter = None
        self.heading = None
        self.items = []
        self.words = []

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag == 'section' and 'chapter' in attrs.get('class', '').split():
            self.chapter = attrs['id']
        if tag == 'h2':
            self.heading = []

    def handle_data(self, data):
        self.words.append(data)
        if self.heading is not None:
            self.heading.append(data)

    def handle_endtag(self, tag):
        if tag == 'h2' and self.heading is not None:
            assert self.chapter
            self.items.append((self.chapter, ''.join(self.heading)))
            self.heading = None


def sources(verify=False):
    records = []
    for path in sorted((HERE / 'evidence').glob('*.json')):
        records.extend(json.loads(path.read_text(encoding='utf-8')))
    assert records, 'Source evidence is required'
    ids = [r['id'] for r in records]
    assert len(ids) == len(set(ids)), 'Duplicate source evidence IDs'
    if verify:
        for record in records:
            path = ROOT / record['path']
            lines = path.read_text(encoding='utf-8-sig').splitlines()
            assert 1 <= record['line'] <= record['end_line'] <= len(lines), record['id']
            digest = hashlib.sha256('\n'.join(lines[record['line']-1:record['end_line']]).encode()).hexdigest()
            assert digest == record['sha256'], 'Changed source: ' + record['id']
    return records


def render(verify=False):
    parts = sorted((HERE / 'chapters').glob('*.html'))
    assert len(parts) >= 4, 'Expected introduction and all three authored parts'
    body = '\n'.join(p.read_text(encoding='utf-8') for p in parts)
    figures = json.loads((HERE / 'figures.json').read_text(encoding='utf-8'))
    def insert_figure(match):
        name = match[1]
        item = figures[name]
        return f'<figure><img src="assets/arda-graph/{name}.svg" alt="{escape(item["alt"], quote=True)}" loading="lazy"><figcaption>{escape(item["caption"])}</figcaption></figure>'
    body = re.sub(r'<!-- DIAGRAM: ([\w-]+)\..*?-->', insert_figure, body, flags=re.S)
    assert '<!-- DIAGRAM' not in body, 'Unresolved diagram'
    def image_dimensions(match):
        tag, source = match[0], match[1]
        svg = (OUT.parent / source).read_text(encoding='utf-8')
        width, height = re.search(r'viewBox="0 0 ([\d.]+) ([\d.]+)"', svg).groups()
        return tag[:-1] + f' width="{width}" height="{height}">'
    body = re.sub(r'<img\b[^>]*src="([^"]+\.svg)"[^>]*>', image_dimensions, body)
    table_numbers = iter(range(1, body.count('<table>') + 1))
    def collapse_table(match):
        title_id = f'data-table-title-{next(table_numbers):02}'
        return (f'<details class="data-table-details"><summary id="{title_id}">{match[1]}</summary>'
                f'<div class="table-wrap"><table aria-labelledby="{title_id}">{match[2]}</table></div></details>')
    body = re.sub(r'<div class="table-wrap"><table><caption>(.*?)</caption>(.*?)</table></div>',
                  collapse_table, body, flags=re.S)
    body = re.sub(r'<div class="table-wrap">', '<div class="table-wrap" tabindex="0" role="region" aria-label="Scrollable data table">', body)
    body = body.replace('<th>', '<th scope="col">')
    evidence = sources(verify)
    snapshot = json.loads((HERE / 'snapshot.json').read_text(encoding='utf-8'))
    body += '''\n<section class="chapter" id="source-notes"><h2>Reading the source behind the diagrams</h2>
<p>This essay studies the local working tree captured on 14 October 2026. It includes APIs and compiler stages that were not yet present in the published source at the time of writing. The source ranges below identify the inspected files and record SHA-256 hashes of their normalized lines. They are evidence for this snapshot, not a claim that another checkout has identical declarations. The existing module API reference may describe an earlier version.</p>
<p>The article’s generators and evidence live under <code>Scripts/Docs/arda_graph_essay/</code>. Run <code>build.py --check --verify-source</code> against the matching working tree to verify both the authored page and its source ranges. The <a href="assets/arda-graph/source-snapshot.json">source snapshot manifest</a> records complete file hashes. All timeline costs, memory sizes and sample values in the original diagrams are teaching examples, not benchmark measurements.</p>
'''
    for record in evidence:
        body += f'''<div class="source-note" id="source-{escape(record['id'])}"><p><strong>{escape(record['id'])}.</strong> {escape(record['claim'])}<br><code>{escape(record['path'])}:{record['line']}–{record['end_line']}</code><br><small>Range SHA-256: <code>{escape(record['sha256'])}</code></small></p></div>\n'''
    body += '</section>'
    parsed = Headings()
    parsed.feed(body)
    assert len(parsed.items) >= 15, 'Expected a sustained chapter-based essay'
    assert len({id for id, _ in parsed.items}) == len(parsed.items)
    count = len(re.findall(r"\b[\w’'-]+\b", ' '.join(parsed.words)))
    nav = ''.join(f'<li><a href="#{escape(id)}"><span>{i:02}</span>{escape(label)}</a></li>'
                  for i, (id, label) in enumerate(parsed.items, 1))
    numbering = iter(range(1, len(parsed.items) + 1))
    body = re.sub(r'<h2>', lambda _: f'<h2><span class="chapter-number" aria-hidden="true">{next(numbering):02}</span>', body)
    # Authored figures and tables remain readable without JavaScript.
    body = re.sub(r'(<figure[^>]*>)\s*(<img\b[^>]*src="[^"]+\.svg"[^>]*>)',
                  lambda m: m[1] + '<div class="figure-scroll" tabindex="0" role="region" aria-label="Scrollable technical diagram">' + m[2] + '</div>', body)
    body = re.sub(r'(<figure[^>]*>\s*<div class="figure-scroll"[^>]*><img\b[^>]*src="([^"]+\.svg)"[^>]*></div>\s*<figcaption>)(.*?)(</figcaption>)',
                  lambda m: m[1] + m[3] + f' <a href="{m[2]}">Open diagram at full size</a>.' + m[4], body, flags=re.S)
    page = f'''<!doctype html>
<!-- Generated from authored chapters by Scripts/Docs/arda_graph_essay/build.py. -->
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="description" content="{escape(DECK)}"><title>{TITLE} · Ardashir</title>
<link rel="stylesheet" href="../Unreal/articles.css"><link rel="stylesheet" href="assets/arda-graph/essay.css">
<script src="../assets/glossary-data.js" defer></script><script src="../Unreal/articles.js" defer></script>
</head><body><a class="skip-link" href="#article">Skip to article</a>
<header class="masthead" id="top"><a class="wordmark" href="../index.html">Ardashir</a><span class="publication">Technical essays</span><a class="docs-link" href="../index.html">Documentation ↗</a></header>
<nav class="essay-links" aria-label="Related documentation"><a href="../ArdaRDG/index.html">ArdaDependencyGraph</a><a href="../ArdaBackend/index.html">GPU backend</a><a href="../Unreal/scene-representation.html">Scene representation essay</a></nav>
<header class="article-heading"><p class="kicker">Graph and compiler engineering</p><h1>{TITLE}</h1><p class="deck">{DECK}</p>
<p class="byline">Current working-tree source study · <time datetime="2026-10-14">14 October 2026</time><br>{count:,} words · approximately {math.ceil(count/200)} minutes</p></header>
<div class="reading-bar"><div class="reading-bar-inner"><details class="contents"><summary>Chapters <span aria-hidden="true">↓</span></summary><nav aria-label="Article chapters"><ol>{nav}</ol></nav></details><span class="reading-position">Arda graph and Inductor</span><a class="top-link" href="#top" aria-label="Back to the beginning">↑</a><button type="button" class="theme-button" aria-label="Switch to dark appearance" hidden>Dark</button></div><div class="progress" aria-hidden="true"><span></span></div></div>
<main id="article" tabindex="-1"><article class="prose">{body}</article></main>
<footer class="article-footer"><p>Source boundary: working-tree snapshot based on <code>{snapshot['base_commit'][:12]}</code>, with local graph/compiler changes recorded in the source manifest. Descriptions of behavior are source-backed; explanatory cost models are illustrative.</p><p><a href="#top">Back to the beginning</a> · <a href="../index.html">Documentation</a> · <a href="../glossary.html">Graphics glossary</a></p></footer></body></html>'''
    return '\n'.join(line.rstrip() for line in page.splitlines()) + '\n', count, len(parsed.items)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--verify-source', action='store_true')
    args = ap.parse_args()
    content, words, chapters = render(args.verify_source)
    if args.check:
        assert OUT.read_text(encoding='utf-8') == content, 'Stale graph essay'
    else:
        OUT.parent.mkdir(parents=True, exist_ok=True)
        OUT.write_text(content, encoding='utf-8')
        manifest = OUT.parent / 'assets/arda-graph/source-snapshot.json'
        manifest.write_text((HERE / 'snapshot.json').read_text(encoding='utf-8'), encoding='utf-8')
    print(f'{"Verified" if args.check else "Built"}: {words:,} words, {chapters} chapters')


if __name__ == '__main__':
    main()
