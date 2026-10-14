"""Validate the essay's frozen artifacts, local links and accessible figures."""
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import urlsplit, unquote
import argparse
import hashlib
import json
import re
import xml.etree.ElementTree as ET
from build import ROOT, HERE, OUT, render, sources
from diagrams import create, FIGURES


class Page(HTMLParser):
    def __init__(self):
        super().__init__();self.ids=set();self.links=[];self.images=[];self.headings=[];self.tables=0
    def handle_starttag(self,tag,attrs):
        a=dict(attrs)
        if 'id' in a:
            assert a['id'] not in self.ids, f'Duplicate ID: {a["id"]}'
            self.ids.add(a['id'])
        if tag in ['h1','h2','h3','h4']:self.headings.append(int(tag[1]))
        if tag=='table':self.tables+=1
        if tag=='img':self.images.append(a);assert a.get('alt'), 'Missing image alternative text'
        for key in ['href','src']:
            if key in a:self.links.append(a[key])


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--verify-source',action='store_true');args=ap.parse_args()
    content, words, chapters=render(args.verify_source)
    assert OUT.read_text(encoding='utf-8')==content,'Stale generated essay'
    assert not re.search(r'<!-- DIAGRAM|\bTODO\b|\bTBD\b',content),'Unresolved authoring marker'
    assert not re.search(r'class="[^"]*(?:api-entry|card-grid)',content),'API catalog structure in essay'
    p=Page();p.feed(content);assert p.headings.count(1)==1
    assert all(b<=a+1 for a,b in zip(p.headings,p.headings[1:])), 'Heading level jump'
    parsed={OUT:p}
    for link in p.links:
        u=urlsplit(link)
        assert not u.scheme and not u.netloc, f'Unexpected external dependency {link}'
        assert not u.path.startswith('/'),f'Root-relative URL: {link}'
        target=(OUT.parent/unquote(u.path)).resolve() if u.path else OUT
        assert target.exists(),f'Missing link {link}'
        if u.fragment and target.suffix=='.html':
            if target not in parsed:
                q=Page();q.feed(target.read_text(encoding='utf-8-sig'));parsed[target]=q
            assert unquote(u.fragment) in parsed[target].ids,f'Missing anchor {link}'
    create()
    for name,item in FIGURES.items():
        path=OUT.parent/'assets/arda-graph'/(name+'.svg')
        assert path.read_text(encoding='utf-8')==item['svg'],f'Stale SVG {name}'
        svg=ET.parse(path).getroot();ns='{http://www.w3.org/2000/svg}'
        assert svg.get('viewBox') and svg.find(ns+'title') is not None and svg.find(ns+'desc') is not None,name
    frozen=(HERE/'snapshot.json').read_text(encoding='utf-8')
    assert frozen==(OUT.parent/'assets/arda-graph/source-snapshot.json').read_text(encoding='utf-8'),'Manifest mismatch'
    snapshot=json.loads(frozen);records=sources(args.verify_source)
    assert snapshot['evidence_ranges']==len(records),'Evidence count mismatch'
    assert {r['path'] for r in records}=={f['path'] for f in snapshot['files']},'Missing snapshot files'
    if args.verify_source:
        for f in snapshot['files']:
            assert hashlib.sha256((ROOT/f['path']).read_bytes()).hexdigest()==f['sha256'],f'Changed source file: {f["path"]}'
    print(f'PASS: {words:,} words, {chapters} chapters, {len(p.images)} figures, {p.tables} tables, {len(records)} source ranges; links, SVG metadata and reproducibility verified')


if __name__=='__main__':main()
