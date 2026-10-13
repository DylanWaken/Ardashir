"""Fetch the specifically cited Epic documentation figures used for technical commentary.

These are attributed third-party reference images, not Arda renders or CC-licensed art.
The reading page never fetches the network; checked-in images have SHA-256 provenance.
"""
from pathlib import Path
import hashlib
import json
import urllib.request

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'Docs/Unreal/Assets/SceneRepresentation'
BASE = 'https://dev.epicgames.com/documentation/en-us/unreal-engine/'
FORTNITE='https://www.unrealengine.com/en-US/tech-blog/bringing-nanite-to-fortnite-battle-royale-in-chapter-4'
LUMEN=BASE+'lumen-technical-details-in-unreal-engine'
# URLs exposed by Epic's public article and image-search index. The documentation
# API image endpoint can require browser challenges; use these published assets.
FIGURES = [
    ('nanite-lit', 'https://cdn2.unrealengine.com/nanite-in-fortnite-chapter-4-nanite-medieval-game-shot-1920x1080-4e85c33ebc11.jpg?resize=1&w=1920', FORTNITE, 'Medieval Game test scene rendered with Nanite (Epic, 2023 / UE5.1)', 1920, 1080),
    ('nanite-triangles', 'https://cdn2.unrealengine.com/nanite-in-fortnite-chapter-4-nanite-triangle-visualization-1-1920x1080-ffaf80e05527.jpg?resize=1&w=1920', FORTNITE, 'Nanite triangle visualization of the Medieval Game test scene', 1920, 1080),
    ('nanite-bins', 'https://cdn2.unrealengine.com/nanite-in-fortnite-chapter-4-unique-rasterizer-bins-1920x1080-51d203b1e64d.jpg?resize=1&w=1920', FORTNITE, 'Unique programmable rasterizer bins in the same test scene', 1920, 1080),
    ('lumen-overview', 'https://img.edc-cdn.net/imgproxy/cJKiH9k-YKkF98u7nrIGOLGgEcuOrjkigX8pAWkC5KM/filename%3Atroubleshooting-topic-1.png/resizing_type%3Afit/width%3A0/height%3A0/aHR0cHM6Ly9kMWl2N2RiNDR5aGd4bi5jbG91ZGZyb250Lm5ldC9kb2N1bWVudGF0aW9uL2ltYWdlcy8wNDY3ZjVlMC02NGU3LTRkNzktYTBkZC05YWU4M2MzYTMyMjgvdHJvdWJsZXNob290aW5nLXRvcGljLTEucG5n', LUMEN, 'Unreal Editor Lumen overview with Lumen Scene, Reflection View and Surface Cache', 1600, 1087),
    ('lumen-surface-cache', 'https://img.edc-cdn.net/imgproxy/iAAimnX8ZZK0YkEISBBtw25BTA_Eb1c5AmD5dzglx5w/filename%3Alumen-surface-cache-visualization.png/resizing_type%3Afit/width%3A0/height%3A0/aHR0cHM6Ly9kMWl2N2RiNDR5aGd4bi5jbG91ZGZyb250Lm5ldC9kb2N1bWVudGF0aW9uL2ltYWdlcy84MjkwNTY4ZS1iODdmLTRlZjktYTY3ZC0zYmE1Mzg1OTQ1NjEvbHVtZW4tc3VyZmFjZS1jYWNoZS12aXN1YWxpemF0aW9uLnBuZw', LUMEN, 'Complex mesh with pink regions lacking Surface Cache coverage (Epic documentation, indexed UE5.7 image)', 600, 463),
]

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    manifest=[]
    for name, url, page, caption, width, height in FIGURES:
        req=urllib.request.Request(url,headers={'User-Agent':'Mozilla/5.0'})
        with urllib.request.urlopen(req,timeout=60) as response:
            data=response.read()
            mime=response.headers.get_content_type()
        ext={'image/png':'.png','image/jpeg':'.jpg','image/webp':'.webp'}.get(mime)
        if not ext: raise ValueError(f'Unexpected image format: {mime}')
        filename=name+ext
        (OUT/filename).write_bytes(data)
        manifest.append(dict(id=name,file=filename,url=url,source=page,caption=caption,
            width=width,height=height,credit='Epic Games — Unreal Engine documentation',
            rights='Third-party copyrighted reference, reproduced with attribution for technical commentary. No license transfer asserted.',
            retrieved='2026-10-13',sha256=hashlib.sha256(data).hexdigest()))
        print(filename,len(data),mime)
    (OUT/'image-provenance.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')

if __name__=='__main__': main()
