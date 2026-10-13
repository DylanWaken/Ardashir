"""Focused structural and provenance checks for the scene representation study."""
from pathlib import Path
from html.parser import HTMLParser
from urllib.parse import urlsplit, unquote
import hashlib
import json
import re
import xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parents[3]
DOCS=ROOT/'Docs'
PAGE=DOCS/'Unreal/scene-representation.html'
VOID={'area','base','br','col','embed','hr','img','input','link','meta','param','source','track','wbr'}

class Page(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.ids=set(); self.links=[]; self.images=[]; self.stack=[]; self.errors=[]; self.headings=[]
        self.main=0; self.lang=False; self.title=False; self.meta=set(); self.chapters=[]
    def handle_starttag(self, tag, pairs):
        a=dict(pairs)
        if tag not in VOID: self.stack.append(tag)
        if 'id' in a:
            if a['id'] in self.ids: self.errors.append('Duplicate ID '+a['id'])
            self.ids.add(a['id'])
        if tag=='html': self.lang=a.get('lang')=='en'
        if tag=='title': self.title=True
        if tag=='main': self.main+=1
        if tag=='meta': self.meta.add(a.get('name',a.get('charset','')))
        if tag=='section' and 'chapter' in a.get('class','').split(): self.chapters.append(a['id'])
        if tag in ('h1','h2','h3','h4','h5','h6'): self.headings.append(int(tag[1]))
        if tag=='img':
            self.images.append(a)
            if not a.get('alt'): self.errors.append('Image without meaningful alt: '+a.get('src',''))
        for key in ('href','src'):
            if key in a: self.links.append(a[key])
    def handle_endtag(self,tag):
        if tag in VOID: return
        if not self.stack or self.stack[-1]!=tag: self.errors.append('Misnested closing tag '+tag+' at '+str(self.getpos()))
        else: self.stack.pop()

def main():
    p=Page(); s=PAGE.read_text(encoding='utf-8'); p.feed(s)
    errors=list(p.errors)
    if p.stack: errors.append('Unclosed tags: '+str(p.stack))
    if not(p.lang and p.title and p.main==1 and {'viewport','description'}<=p.meta): errors.append('Missing page metadata/landmark')
    if p.headings.count(1)!=1: errors.append('Expected one h1')
    for prev,now in zip(p.headings,p.headings[1:]):
        if now>prev+1: errors.append(f'Heading skips level: h{prev} to h{now}')
    if len(p.chapters)!=15: errors.append(f'Expected 15 chapter/evidence sections, got {len(p.chapters)}')
    if 'data-diagram=' in s or '<figure></figure>' in s: errors.append('Unresolved figure placeholder')
    targets={PAGE:p}
    for value in p.links:
        u=urlsplit(value)
        if u.scheme or u.netloc: continue
        if value.startswith('/'): errors.append('Root-relative link '+value); continue
        path=(PAGE.parent/unquote(u.path)).resolve() if u.path else PAGE
        if not path.is_relative_to(DOCS): errors.append('Link leaves Docs: '+value); continue
        if not path.is_file(): errors.append('Missing relative target: '+value); continue
        if u.fragment and path.suffix=='.html':
            if path not in targets:
                q=Page(); q.feed(path.read_text(encoding='utf-8')); targets[path]=q
            if unquote(u.fragment) not in targets[path].ids: errors.append('Missing anchor: '+value)
    svgs=list((PAGE.parent/'Assets/SceneRepresentation').glob('*.svg'))
    for svg in svgs:
        tree=ET.fromstring(svg.read_text(encoding='utf-8'))
        ns={'s':'http://www.w3.org/2000/svg'}
        if not tree.get('viewBox') or tree.find('s:title',ns) is None or tree.find('s:desc',ns) is None: errors.append('SVG accessibility metadata missing: '+svg.name)
    manifest=json.loads((PAGE.parent/'scene-representation-sources.json').read_text(encoding='utf-8'))
    evidence=manifest['evidence']
    for e in evidence:
        if 'src-'+e['id'] not in p.ids: errors.append('Missing source target '+e['id'])
        if not re.fullmatch('[a-f0-9]{64}',e.get('sha256','')): errors.append('Missing source hash '+e['id'])
    images=json.loads((PAGE.parent/'Assets/SceneRepresentation/image-provenance.json').read_text(encoding='utf-8'))
    for im in images:
        path=PAGE.parent/'Assets/SceneRepresentation'/im['file']
        if hashlib.sha256(path.read_bytes()).hexdigest()!=im['sha256']: errors.append('Image hash changed: '+im['id'])
        if not im.get('source') or not im.get('credit'): errors.append('Missing image attribution: '+im['id'])
    if errors: raise SystemExit('\n'.join('FAIL: '+e for e in errors))
    print(f'PASS: {len(p.chapters)} sections, {len(p.ids)} unique anchors, {len(p.links)} links, {len(svgs)} accessible SVGs, {len(images)} attributed images, {len(evidence)} hashed source ranges.')

if __name__=='__main__': main()
