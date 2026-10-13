"""Assemble the single-page Unreal scene study from authored chapters and evidence.

python Scripts/Docs/scene_representation/build.py
python Scripts/Docs/scene_representation/build.py --check
python Scripts/Docs/scene_representation/build.py --verify-source D:/UnrealEngine

Source verification is local-only. Published documentation contains source locations,
claims and hashes, not a copy of Unreal source. The core article is static HTML.
"""
from pathlib import Path
from html import escape
import argparse
import hashlib
import json
import re
import subprocess
import sys
import build_diagrams

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
DOC=ROOT/'Docs/Unreal'
COMMIT='71fe36aac5a8df5ccd66c763ffc902b29b6a9c43'
SECTIONS=[
 ('mental-model','The mental model','01 · Foundations'),('ownership','Code & ownership',None),
 ('gpu-scene','GPU Scene & identity',None),('mesh-pipeline','From mesh to draw work',None),
 ('geometry','Geometry families','02 · Shape & motion'),('nanite','Nanite, step by step',None),('deformation','Transforms & animation',None),
 ('materials','Materials & shading','03 · Appearance & light'),('deferred','The deferred frame',None),('effects','Effects & transparency',None),('lumen','Lumen & GI',None),
 ('one-frame','Trace one complete frame','04 · Build & investigate'),('arda-design','A proposed Arda design',None),('practice','Unreal lab & self-test',None),
 ('sources','Sources & provenance','05 · Evidence')]
DIAGRAMS={
 'scene-representations':('many-representations','Different representations answer different questions about one world.'),
 'ownership-timeline':('ownership-timeline','Render-command consumption and GPU completion are distinct lifetime boundaries.'),
 'gpu-scene-layout':('gpu-tables','Stable identity, packed storage and per-instance data must not be conflated.'),
 'mesh-draw-pipeline':('mesh-draw-pipeline','The conventional draw path combines geometry access and materials for a particular pass.'),
 'indexed-mesh':('indexed-mesh','One indexed surface separates geometric connectivity, vertex attributes and material ranges.'),
 'three-hierarchies':('three-hierarchies','Attachment, spatial grouping and geometric detail require different hierarchies.'),
 'nanite-flow':('nanite-flow','Nanite separates detail selection, residency, visibility and surface evaluation.'),
 'deformation-history':('deformation-history','Deformation and placement must be evaluated consistently for current and previous state.'),
 'material-contract':('material-contract','Surface response and illumination are distinct inputs to shading.'),
 'deferred-products':('deferred-contract','The deferred renderer connects different visibility paths through surface-data contracts.'),
 'lumen-representations':('lumen-representations','Ray intersection, material capture and cached illumination solve different problems.'),
 'arda-boundaries':('arda-boundaries','Proposed Arda boundaries: shared scene lifecycle, replaceable representations, existing GPU infrastructure.')}

def figure(key):
    name,caption=DIAGRAMS[key]
    return f'<figure class="diagram"><div class="diagram-scroll" tabindex="0" role="region" aria-label="Diagram: {escape(caption)}"><img src="Assets/SceneRepresentation/{name}.svg" alt="{escape(caption)}" loading="lazy"></div><figcaption>{escape(caption)} <a href="Assets/SceneRepresentation/{name}.svg">Open full-size SVG</a>.</figcaption></figure>'

def reference_image(name,caption,images):
    item=next(x for x in images if x['id']==name)
    return f'<figure class="reference-image"><a href="Assets/SceneRepresentation/{item["file"]}"><img src="Assets/SceneRepresentation/{item["file"]}" alt="{escape(item["caption"])}" width="{item["width"]}" height="{item["height"]}" loading="lazy"></a><figcaption>{caption} <a href="{escape(item["source"])}">Reference: Epic Games</a>. Click for the original image.</figcaption></figure>'

def assemble(evidence, images):
    content='\n'.join(p.read_text(encoding='utf-8') for p in sorted((HERE/'chapters').glob('*.html')))
    # Normalize authored fragment markup while leaving wording in its source chapter.
    content=re.sub(r'<section (?!class=)(id="(?:geometry|nanite|deformation|materials|deferred|effects|lumen)"[^>]*)>',r'<section class="chapter" \1>',content)
    content=content.replace('href="#architecture"','href="#ownership"')
    content=content.replace('>Dynamic skeletal cards</a>', '>Dynamic skeletal cards</a> <a class="source-ref" href="#src-shade-skeletal-cards-default">Default-enabled capability</a>')
    # Chapter labels follow the assembled reading order, not independent draft numbering.
    for i,(sid,_,_) in enumerate(SECTIONS[:-1],1):
        pattern=r'(<section class="chapter" id="'+sid+r'"[^>]*>\s*)(?:<p class="eyebrow">[^<]*</p>\s*)?'
        content=re.sub(pattern,lambda m:m[1]+f'<p class="eyebrow">Chapter {i:02} · '+escape(SECTIONS[i-1][1])+'</p>\n',content,count=1)
    content=re.sub(r'<figure data-diagram="([^"]+)"></figure>',lambda m:figure(m[1]),content)
    content=re.sub(r'<!-- DIAGRAM: (material-contract|deferred-products|lumen-representations) .*?-->',lambda m:figure(m[1]),content,flags=re.S)
    content=content.replace('<h3 id="geometry-triangles">',figure('indexed-mesh')+'\n<h3 id="geometry-triangles">')
    content=content.replace('<h3 id="geometry-nonsurfaces">',figure('three-hierarchies')+'\n<h3 id="geometry-nonsurfaces">')
    content=content.replace('<h3 id="nanite-build">',figure('nanite-flow')+'\n<h3 id="nanite-build">')
    # Deformation diagram follows its introductory paragraphs, before first subheading.
    content=re.sub(r'(<section class="chapter" id="deformation"[^>]*>.*?)(<h3)',lambda m:m[1]+figure('deformation-history')+m[2],content,count=1,flags=re.S)
    compare='<h3 id="nanite-reference-images">Look at the same Unreal scene three ways</h3><p>The images below are historical UE5.1-era examples from Epic’s 2023 article. They demonstrate three distinct representations; the version-sensitive claims in this study are checked against the local 5.8.1 source.</p>'
    compare+=reference_image('nanite-lit','The lit image hides the structure that produced it: a forest scene in the Unreal Editor.',images)
    compare+='<div class="image-pair">'+reference_image('nanite-triangles','Nanite Triangles exposes the selected geometric detail. Color distinguishes triangles, not materials.',images)+reference_image('nanite-bins','Rasterizer bins group compatible visibility work. This view is different from both triangles and final shading.',images)+'</div>'
    content=content.replace('<h3 id="nanite-version-boundary">',compare+'\n<h3 id="nanite-version-boundary">')
    lumen=reference_image('lumen-overview','The Editor displays Lumen Scene, Reflection View and Surface Cache beside the final scene. Each diagnostic asks a different question about the same geometry.',images)
    lumen+=reference_image('lumen-surface-cache','Pink regions show missing Surface Cache coverage on a complex mesh. A visible triangle does not guarantee a captured lighting sample. This reference image comes from an earlier documentation revision.',images)
    # Insert before chapter's checkpoint, using the section boundary rather than a guessed inner ID.
    start=content.index('<section class="chapter" id="lumen"')
    end=content.index('</section>',start)
    content=content[:end]+lumen+content[end:]
    content=re.sub(r'<!--.*?-->', '', content,flags=re.S)
    # Link selected first definitions to the one global glossary, preserving no-JS behavior.
    terms={'render proxy':'render-proxy','scene primitive':'scene-primitive','asset':'scene-asset','placement':'scene-instance','view':'render-view','frame':'render-frame','section':'mesh-section','skeletal mesh':'skinning','Landscape':'heightfield','heightfield':'heightfield','clusters':'geometry-cluster','DAG':'dag','GBuffer':'gbuffer','BRDF':'brdf','BLAS':'blas','TLAS':'tlas','screen traces':'screen-trace','Surface Cache':'surface-cache','GPU Scene':'gpu-scene','vertex factory':'vertex-factory','global illumination':'global-illumination'}
    for label,slug in terms.items():
        pattern=re.compile(r'<strong>'+re.escape(label)+r'</strong>',re.I)
        content=pattern.sub(lambda m:f'<strong><a href="../glossary.html#term-{slug}" data-term="{slug}">'+re.sub('</?strong>','',m[0])+'</a></strong>',content,count=1)
    for i,(sid, label,_) in enumerate(SECTIONS[:-1]):
        nxt=SECTIONS[i+1]
        start=content.index(f'id="{sid}"')
        end=content.index('</section>',start)
        content=content[:end]+f'<p class="chapter-next"><a href="#{nxt[0]}">Next: {escape(nxt[1])} →</a></p>'+content[end:]
    words=len(re.sub('<[^>]+>',' ',content).split())
    nav=''
    for i,(sid,label,group) in enumerate(SECTIONS,1):
        if group: nav+=f'<li class="toc-group">{escape(group)}</li>'
        nav+=f'<li><a data-chapter="{sid}" href="#{sid}"><span>{i:02}</span><span>{escape(label)}</span></a></li>'
    sources='<section class="chapter" id="sources"><p class="eyebrow">15 · Audit trail</p><h2>Sources, version boundary, and image credits</h2>'
    sources+=f'<p>This study was researched on 13 October 2026 against <code>D:\\UnrealEngine</code>, whose <code>Engine/Build/Build.version</code> identifies 5.8.1 and whose Git HEAD is <code>{COMMIT}</code>. This pins the implementation evidence; it is not a claim about every installed Unreal release. Public documentation can contain older screenshots or inconsistent feature summaries. Where those conflict, this article narrows its claims to checked local code and explicit gates.</p>'
    sources+='<p>The 2021 <a href="https://advances.realtimerendering.com/s2021/Karis_Nanite_SIGGRAPH_Advances_2021_final.pdf">Nanite deep dive by Brian Karis, Rune Stubbe and Graham Wihlidal</a> supplies the problem-first teaching inspiration, especially its opening comparison of representations and retained GPU pipeline. The implementation here is independently traced through the newer local source. Source references below are evidence for particular claims, not a substitute for a complete engine API reference.</p>'
    sources+='<p>Unreal source links require an Epic-linked GitHub account with repository access. The file path and line range remain usable in the local checkout even if a browser cannot open those links. Arda references describe the inspected working tree; the proposed scene layer is not implemented. <a href="scene-representation-sources.json">Machine-readable evidence and source hashes</a>.</p>'
    for prefix,title in [('arch-','Architecture and GPU Scene'),('geo-','Geometry, Nanite and deformation'),('shade-','Materials, renderer and Lumen'),('arda-','Existing Arda integration boundaries')]:
        records=[e for e in evidence if e['id'].startswith(prefix)]
        sources+=f'<details class="source-group" open><summary>{title} <span class="source-count">({len(records)} source ranges)</span></summary>'
        for e in records:
            href=f'https://github.com/EpicGames/UnrealEngine/blob/{COMMIT}/{e["path"]}#L{e["line"]}' if e.get('repository')!='arda' else ''
            titlelink=f'<a href="{escape(href)}">{escape(e["symbol"])}</a>' if href else escape(e['symbol'])
            sources+=f'<div class="source-entry" id="src-{e["id"]}"><p><strong>{titlelink}</strong></p><p><code>{escape(e["path"])}:{e["line"]}–{e["end_line"]}</code></p><p>{escape(e["claim"])}</p></div>'
        sources+='</details>'
    sources+='<h3 id="image-credits">Reference images and original diagrams</h3><p>The SVG diagrams are original explanatory figures for this study. The five raster images are attributed third-party Epic Games references, included for technical commentary; they are not captures from the Arda project and no license transfer is asserted. Images remain unaltered. Their publication context and hashes are recorded in the <a href="Assets/SceneRepresentation/image-provenance.json">image provenance manifest</a>.</p><ul>'
    for e in images: sources+=f'<li><a href="{escape(e["source"])}">{escape(e["caption"])}</a> — {escape(e["credit"])}.</li>'
    sources+='</ul><h3 id="maintenance">Maintain this study</h3><p>Authored chapters and evidence are under <code>Scripts/Docs/scene_representation/</code>. Rebuild with <code>python Scripts/Docs/scene_representation/build.py</code>; check reproducibility with <code>--check</code>. The focused validator checks links, diagram metadata and image provenance. Use <code>build.py --verify-source D:/UnrealEngine</code> to compare evidence hashes after changing engine versions. Re-research changed claims before refreshing the evidence.</p><p><a href="#top">Back to the beginning ↑</a> · <a href="../index.html">Documentation home</a></p></section>'
    return f'''<!doctype html>
<!-- Generated by Scripts/Docs/scene_representation/build.py. Edit the chapter sources. -->
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="description" content="A source-checked, step-by-step study of Unreal scene representation: code ownership, GPU Scene, geometry, Nanite, materials, deferred shading, Lumen, and a proposed Arda design.">
<title>How Unreal Represents a Scene · Ardashir</title>
<link rel="stylesheet" href="../assets/site.css"><link rel="stylesheet" href="scene-representation.css">
<script src="../assets/glossary-data.js" defer></script><script src="../assets/site.js" defer></script><script src="scene-representation.js" defer></script></head>
<body><a class="skip-link" href="#main-content">Skip to study</a>
<header class="site-header"><div class="site-header__inner"><a class="site-brand" href="../index.html">Ardashir</a><button class="nav-toggle" type="button" data-nav-toggle aria-controls="study-primary-nav" aria-expanded="false">Menu</button><nav aria-label="Primary" class="site-nav" id="study-primary-nav" data-site-nav><ul><li><a href="../index.html">Docs</a></li><li><a href="scene-representation.html" aria-current="page">Scene representation</a></li><li><a href="../glossary.html">Glossary</a></li></ul></nav><button class="theme-toggle" type="button" data-theme-toggle>Dark theme</button></div></header>
<div class="study-layout"><aside class="study-toc"><details open><summary>Inside this study</summary><label for="chapter-search" class="toc-hint">Find a concept</label><input id="chapter-search" class="toc-search" type="search" placeholder="e.g. instances, skinning, GI"><p id="chapter-search-status" class="toc-hint" role="status">Search finds chapters; use Ctrl+F to search within the page.</p><div class="progress-track" aria-hidden="true"><span id="reading-progress"></span></div><nav aria-label="Study chapters"><ol>{nav}</ol></nav><p class="toc-hint">One page. Read in stages.<br>Each chapter has a checkpoint.</p></details></aside>
<main class="study" id="main-content"><header class="study-hero" id="top"><p class="eyebrow">Arda research notebook / scene systems</p><h1>How Unreal<br>represents a scene.</h1><p class="deck">From a shared mesh and a transform to GPU-driven visibility, materials, a deferred frame, and real-time global illumination. A step-by-step engineering study for the scene system we will build next.</p><div class="study-meta"><span><strong>Source baseline</strong> Unreal 5.8.1</span><span><strong>Researched</strong> 13 Oct 2026</span><span><strong>Depth</strong> {words:,} words · 14 chapters + evidence</span></div><p>The goal is to explain why each representation exists, what it stores, who owns it, and which other systems consume it. We move from an abstract model to concrete Unreal types and source paths, then derive an explicitly proposed Arda design.</p><p class="prereqs"><strong>Prerequisites:</strong> basic programming and the idea of a GPU. No Unreal experience required. Useful reminders: <a href="../glossary.html#term-resource" data-term="resource">resource</a>, <a href="../glossary.html#term-shader" data-term="shader">shader</a>, <a href="../glossary.html#term-transform" data-term="transform">transform</a>, <a href="../glossary.html#term-render-graph" data-term="render-graph">render graph</a>.</p><div class="reading-routes"><p><strong>First reading:</strong> follow chapters 01–04, pause, then continue with geometry and appearance. Read the complete frame in chapter 12 to reconnect the pieces.</p><p><strong>Returning engineer:</strong> jump to <a href="#gpu-scene">GPU identity</a>, <a href="#geometry">geometry forms</a>, <a href="#lumen">GI representations</a>, or <a href="#arda-design">Arda's proposed contracts</a>.</p><p><strong>Evidence key:</strong> inline source links resolve to exact local file ranges. Unreal facts, historical reference images, and future Arda recommendations are labeled separately.</p></div></header>
{content}
{sources}
<footer class="study-footer">Ardashir engineering documentation · Source-backed research; proposed scene architecture, not an implementation announcement.</footer></main></div></body></html>'''

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--check',action='store_true')
    ap.add_argument('--verify-source',type=Path)
    args=ap.parse_args()
    records=[]
    for p in sorted(HERE.glob('evidence-*.json')): records.extend(json.loads(p.read_text(encoding='utf-8')))
    assert len({e['id'] for e in records})==len(records),'duplicate source evidence IDs'
    manifest_path=DOC/'scene-representation-sources.json'
    old=json.loads(manifest_path.read_text(encoding='utf-8')) if manifest_path.exists() else {}
    prior={e['id']:e for e in old.get('evidence',[])}
    for e in records:
        if args.verify_source or not manifest_path.exists():
            source_root=ROOT if e.get('repository')=='arda' else (args.verify_source or Path('D:/UnrealEngine'))
            lines=(source_root/e['path']).read_text(encoding='utf-8-sig').splitlines()
            assert 1<=e['line']<=e['end_line']<=len(lines),e
            digest=hashlib.sha256('\n'.join(lines[e['line']-1:e['end_line']]).encode()).hexdigest()
            if e['id'] in prior and prior[e['id']].get('sha256')!=digest:
                raise ValueError(f'Source changed: {e["id"]} {e["path"]}. Re-research before refreshing evidence.')
            e['sha256']=digest
        elif e['id'] in prior: e['sha256']=prior[e['id']]['sha256']
        else: raise ValueError(f'New evidence {e["id"]}: run with --verify-source to inspect it.')
    manifest=dict(engine_version='5.8.1',unreal_commit=COMMIT,research_date='2026-10-13',unreal_root_hint='D:/UnrealEngine',evidence=records)
    images=json.loads((DOC/'Assets/SceneRepresentation/image-provenance.json').read_text(encoding='utf-8'))
    page='\n'.join(line.rstrip() for line in assemble(records,images).split('\n'))
    outputs={DOC/'scene-representation.html':page,manifest_path:json.dumps(manifest,indent=2,ensure_ascii=False)+'\n'}
    if args.check:
        for path,value in outputs.items():
            if not path.exists() or path.read_text(encoding='utf-8')!=value: raise ValueError(f'Out of date: {path}')
        print(f'PASS: reproducible article; {len(records)} source ranges.')
    else:
        build_diagrams.build()
        DOC.mkdir(parents=True,exist_ok=True)
        for path,value in outputs.items(): path.write_text(value,encoding='utf-8')
        print(f'Built {DOC / "scene-representation.html"}; {len(records)} source ranges.')
    if args.verify_source: print('PASS: all source evidence hashes match the inspected checkouts.')

if __name__=='__main__': main()
