"""Author the scene study's SVG figures. Run from any directory; no dependencies."""
from pathlib import Path
from html import escape

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'Docs/Unreal/Assets/SceneRepresentation'

def text(x, y, s, cls='label', anchor='start'):
    return f'<text x="{x}" y="{y}" class="{cls}" text-anchor="{anchor}">{escape(s)}</text>'

def box(x, y, w, h, title, lines=(), kind='blue'):
    out = f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="8" class="{kind}"/>'
    out += text(x+16,y+28,title,'strong')
    for i, line in enumerate(lines): out += text(x+16,y+53+i*22,line)
    return out

def arrow(x1,y1,x2,y2,dashed=False):
    return f'<path d="M{x1} {y1} L{x2} {y2}" class="arrow" {"stroke-dasharray=\"6 5\"" if dashed else ""} marker-end="url(#head)"/>'

def save(name,title,desc,h,body):
    svg=f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 940 {h}" role="img" aria-labelledby="title desc">
<title id="title">{escape(title)}</title><desc id="desc">{escape(desc)}</desc>
<defs><marker id="head" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10Z" fill="#516576"/></marker></defs>
<style>text{{font-family:Arial,sans-serif;fill:#193044}}.label{{font-size:16px}}.strong{{font-size:18px;font-weight:700}}.small{{font-size:14px}}.kicker{{font-size:14px;letter-spacing:1.5px;fill:#4e6376}}.blue{{fill:#e6f0fa;stroke:#6394bd}}.green{{fill:#e1f2e9;stroke:#65a08a}}.amber{{fill:#fff0d7;stroke:#c79b4e}}.plain{{fill:#fff;stroke:#a3b4c2}}.arrow{{fill:none;stroke:#516576;stroke-width:2}}.muted{{fill:#556c7d}}</style>
<rect width="940" height="{h}" fill="#f8fafc" rx="12"/>{body}</svg>'''
    (OUT/f'{name}.svg').write_text(svg,encoding='utf-8')

def build():
    OUT.mkdir(parents=True,exist_ok=True)
    b=text(30,34,'ONE WORLD · DIFFERENT QUESTIONS','kicker')
    b+=box(30,66,260,125,'Authoring / gameplay',('What exists?','Assets + components','Transforms + animation'))
    b+=arrow(290,128,350,128)+box(350,66,250,125,'Retained render scene',('What may be rendered?','Proxies + primitive records','Versioned GPU tables'),'green')
    b+=arrow(600,128,660,128)+box(660,66,250,125,'Per-view work',('What matters this frame?','Cull → raster / trace','Shade → light → compose'),'amber')
    b+=arrow(475,191,475,240)+box(90,240,760,94,'Derived representations answer different visibility queries',('Camera: depth / visibility     Light: shadow pages     Rays: distance fields / BVH', 'Cached lighting: Lumen cards and radiance     None is the complete world.'),'plain')
    b+=text(30,375,'Solid arrows: data is converted or selected, not one shared mutable object.','small')
    save('many-representations','One world, several coordinated representations','Authoring data produces a retained render scene. Each view selects work. Camera depth, shadow maps, ray geometry and lighting caches are derived representations with different purposes.',405,b)

    b=text(30,32,'OWNERSHIP AND TIME · ILLUSTRATIVE OVERLAP','kicker')
    for y,t in [(87,'Game thread'),(195,'Render thread'),(303,'GPU')]:
        b+=text(22,y,t,'strong')+f'<path d="M175 {y+32}H910" class="arrow"/>'
    b+=box(184,52,212,69,'Edit component',('frame N: new transform',))
    b+=box(465,52,230,69,'Continue simulation',('frame N+1 can overlap',))
    b+=arrow(395,106,450,160)+box(385,160,263,70,'Consume ordered update',('render-owned copy / records',),'green')
    b+=arrow(648,225,702,268)+box(659,268,251,70,'Execute submitted work',('reads retained GPU resources',),'amber')
    b+=text(30,388,'Two different completion conditions','strong')
    b+=text(30,420,'Render-thread consumption: the CPU copy has been accepted.')
    b+=text(30,446,'GPU completion: old buffers / descriptors / IDs may finally be retired.')
    b+=text(30,477,'Do not reuse an ID or allocation while an older submission can still read it.','small')
    save('ownership-timeline','Game thread, render thread and GPU lifetimes','An update is consumed asynchronously. Render-thread processing does not imply GPU completion. Old GPU allocations must remain alive until all consuming submissions complete.',510,b)

    b=text(30,32,'IDENTITY IS NOT AN ARRAY POSITION','kicker')
    b+=box(30,62,260,100,'Component identity',('FPrimitiveComponentId','can survive re-registration'))
    b+=box(340,62,265,100,'Scene membership',('FPersistentPrimitiveIndex','valid while added to scene'),'green')
    b+=box(660,62,250,100,'CPU storage location',('PackedIndex','may move in packed arrays'),'amber')
    b+=arrow(290,110,340,110)+arrow(605,110,660,110)
    b+=box(30,213,400,122,'GPU primitive record P',('Shared primitive-level state','InstanceSceneDataOffset = B','NumInstanceSceneDataEntries = K'),'green')
    b+=arrow(430,274,482,274)+box(482,195,428,158,'GPU instance range [B, B + K)',('Instance B + 0 → primitive P, transform T0','Instance B + 1 → primitive P, transform T1','Instance B + 2 → primitive P, transform T2','Optional payload: custom data / history'))
    b+=text(30,395,'A mesh asset supplies geometry; the instance record supplies placement.','strong')
    b+=text(30,425,'Persistent here means stable during scene membership, not serialized or never reused.','small')
    save('gpu-tables','CPU identities and GPU Scene indirection','Component identity, persistent scene primitive index and packed CPU index have different lifetimes. One GPU primitive record points to multiple instance records; geometry assets are shared.',455,b)

    b=text(30,32,'CONVENTIONAL MESH PATH · DESCRIPTION BECOMES PASS WORK','kicker')
    b+=box(30,62,266,103,'FPrimitiveSceneProxy',('Static descriptions cached','or dynamic collection per view'))
    b+=arrow(296,113,336,113)+box(336,62,266,103,'FMeshBatch',('Geometry ranges + elements','Vertex factory + material proxy'),'green')
    b+=arrow(602,113,644,113)+box(644,62,266,103,'FMeshPassProcessor',('Select pass-compatible shaders','Set pipeline and bindings'),'amber')
    b+=box(30,221,266,104,'FVertexFactory',('How to fetch positions / UVs','and apply geometry-specific work'))
    b+=arrow(296,256,336,148,True)
    b+=box(336,221,266,104,'FMaterialRenderProxy',('Which surface program / values','Fallback when applicable'),'green')
    b+=arrow(470,221,470,165,True)
    b+=arrow(777,166,777,221)+box(644,221,266,104,'FMeshDrawCommand',('Pass-specific draw state','Cached when eligible'),'amber')
    b+=text(30,375,'Solid: transformation into work. Dashed: geometry / material dependencies.','small')
    b+=text(30,407,'One batch can participate in depth, shadow and base passes with different shader work.','small')
    save('mesh-draw-pipeline','Mesh description, vertex factory, material and pass processing','A proxy supplies mesh batches. A batch combines geometry access via a vertex factory with material state. A pass processor creates compatible mesh draw commands, possibly cached.',440,b)

    b=text(30,32,'SURFACE RESPONSE IS A FUNCTION, NOT THE FINAL PIXEL COLOR','kicker')
    b+=box(30,63,270,110,'Material evaluation',('Textures + parameters + UVs','→ normal, roughness, color…','Defines a surface response'))
    b+=box(335,63,270,110,'Incident illumination',('Light direction + intensity','Shadow / visibility','Indirect incoming radiance'),'green')
    b+='<path d="M165 173 V196 H775 V175" class="arrow" marker-end="url(#head)"/>'
    b+=arrow(605,116,640,116)+box(640,63,270,110,'Shading',('Evaluate scattering model','for view and light directions','→ outgoing radiance'),'amber')
    b+=box(30,222,880,107,'Example: the same rough wooden crate under two lights',('Its albedo / roughness stay fixed. A blue lamp changes incident light, so the shaded color changes.','Changing roughness changes the angular spread of reflected light; it does not move the crate.','Emissive adds outgoing energy; a separate light can approximate how that bulb illuminates others.'),'plain')
    b+=text(30,372,'The arrows describe the dependency, not the storage format or GPU scheduling order.','small')
    save('material-contract','Material response, incoming illumination and final shading','A material evaluates surface attributes. Lights and indirect illumination supply incoming radiance. A scattering model combines those values to produce outgoing radiance.',405,b)

    b=text(30,34,'SAME SURFACE · THREE DIFFERENT GRAPHS','kicker')
    for x,title,lines,k in [(30,'Attachment hierarchy',('Parent → child transforms','Child follows by default','Question: where?'),'blue'),(342,'Spatial hierarchy',('Bounds contain smaller bounds','Reject hidden / distant groups','Question: visible?'),'green'),(654,'Nanite hierarchy',('Alternative levels of detail','Choose a compatible cut','Question: how much detail?'),'amber')]:
        b+=box(x,68,256,104,title,lines[:2],k)
        b+=f'<circle cx="{x+128}" cy="220" r="16" class="{k}"/>'
        b+=arrow(x+120,235,x+65,282)+arrow(x+136,235,x+190,282)
        for xx in [x+64,x+192]: b+=f'<rect x="{xx-28}" y="283" width="56" height="31" rx="4" class="{k}"/>'
        b+=text(x+128,386 if x==654 else 352,lines[2],'strong','middle')
    b+=arrow(718,314,782,345)+arrow(846,314,782,345)
    b+='<rect x="762" y="345" width="40" height="22" rx="4" class="amber"/>'
    b+=text(30,407,'These structures are not interchangeable: parenthood, proximity and simplification differ.','small')
    save('three-hierarchies','Attachment, spatial and level-of-detail hierarchies','Three graph structures have distinct meanings. Attachment composes transforms, spatial bounds accelerate visibility, and Nanite selects levels of geometric detail.',438,b)

    b=text(30,33,'INDEXED TRIANGLES · ONE QUAD, TWO SURFACES OF REUSE','kicker')
    b+='<path d="M65 100 L285 100 L285 300 L65 300Z M65 100 L285 300" fill="#e6f0fa" stroke="#6394bd" stroke-width="3"/>'
    for x,y,t in [(65,88,'v0'),(285,88,'v1'),(285,326,'v2'),(65,326,'v3')]: b+=text(x,y,t,'strong','middle')
    b+=text(108,245,'triangle 1')+text(158,160,'triangle 0')
    b+=box(365,64,535,95,'Vertex streams',('positions: [v0, v1, v2, v3]','UVs, normals, tangents: attributes indexed with positions'))
    b+=box(365,180,535,87,'Index stream',('[0, 1, 2,  0, 2, 3] → six indices reference four vertices',),'green')
    b+=box(365,288,535,89,'Mesh section → material slot',('A triangle range selects a material binding.','An instance transform places shared local-space geometry.'),'amber')
    b+=text(30,417,'At a UV or normal seam, one position may require multiple attribute vertices.','small')
    save('indexed-mesh','Indexed geometry and material sections','A quad consists of two indexed triangles sharing vertices. Attribute seams may require duplicate vertices, while sections assign materials and instances place shared geometry.',449,b)

    b=text(30,32,'NANITE · SELECT DETAIL BEFORE EVALUATING SURFACES','kicker')
    b+=box(30,64,266,115,'Build / cook',('Triangle clusters','Simplified groups + hierarchy','Compressed streaming pages'))
    b+=arrow(296,120,336,120)+box(336,64,266,115,'Per view on GPU',('Instance / cluster culling','Choose resident detail','Request missing pages'),'green')
    b+=arrow(602,120,644,120)+box(644,64,266,115,'Visibility rasterization',('Small / large triangles','Compute + hardware paths','Depth + visible triangle IDs'),'amber')
    b+=box(30,224,572,88,'Streaming has latency',('A valid resident coarser representation covers missing fine detail.','The selected hierarchy cut changes with projected error.'),'plain')
    b+=arrow(777,180,777,224)+box(644,224,266,115,'Material evaluation',('Fetch triangle attributes','Evaluate surface program','Write deferred surface data'))
    b+=text(30,383,'GPU Scene supplies placements; Nanite owns specialized geometry and visibility.','strong')
    b+=text(30,414,'Conceptual baseline. Programmable deformation / masking complicates the clean split.','small')
    save('nanite-flow','Nanite build, stream, cull, raster and shade','Nanite builds clusters and a simplification hierarchy. Per-view selection uses resident detail, requests pages, rasterizes IDs and evaluates materials for visible surfaces. GPU Scene is separate.',445,b)

    b=text(30,32,'PLACEMENT, DEFORMATION AND TEMPORAL HISTORY','kicker')
    b+=box(30,65,260,100,'Bind-space vertex p',('Position + bone weights','UVs / normals / tangents'))
    b+=arrow(290,115,340,115)+box(340,65,260,100,'Deform in local space',('Skin / morph / cloth','May change local bounds'),'green')
    b+=arrow(600,115,650,115)+box(650,65,260,100,'Place in world',('Component / instance matrix','Then camera projection'),'amber')
    b+=box(30,218,410,118,'Current rendered state',('current pose + current transform','current camera view / projection','→ current clip / screen position'))
    b+=box(500,218,410,118,'Previous rendered state',('previous pose + previous transform','previous camera view / projection','→ previous clip / screen position'),'green')
    b+=text(470,277,'−','strong','middle')
    b+=text(30,369,'Velocity needs both states. A transform alone cannot describe a moving elbow.','strong')
    b+=text(30,402,'Bounds, shadow geometry and ray geometry must describe the deformation they consume.','small')
    save('deformation-history','Deformation before placement, with current and previous state','Skinning and other local deformation occur separately from object placement. Motion reconstruction requires previous deformation as well as transforms; all visibility bounds must be conservative.',433,b)

    b=text(30,32,'DEFERRED SHADING · SEPARATE THREE QUESTIONS','kicker')
    b+=box(30,64,263,101,'1 · Which surface wins?',('Raster depth / visibility','Triangle or fragment identity'))
    b+=arrow(293,112,338,112)+box(338,64,263,101,'2 · What is the surface?',('Material evaluation','Normal, albedo, roughness…'),'green')
    b+=arrow(601,112,647,112)+box(647,64,263,101,'3 · How much light?',('Shading model + lights','Visibility + indirect radiance'),'amber')
    b+=box(30,216,263,95,'Classic raster',('Base pass evaluates material','as accepted fragments run.'))
    b+=box(338,216,263,95,'Nanite raster',('Visibility IDs first; then','material evaluation / bins.'),'green')
    b+=arrow(162,312,230,354)+arrow(469,312,469,354)
    b+=box(188,354,570,94,'Deferred surface buffers → lighting → scene color',('GBuffer / configured Substrate encoding is surface data.','Lumen supplies indirect illumination; it is not a material compiler.'),'plain')
    b+=text(30,486,'Transparency, hair and volumes require additional paths; this is the opaque baseline.','small')
    save('deferred-contract','Visibility, material evaluation and illumination are distinct','Classic and Nanite paths create surface data differently but feed the deferred renderer. Lighting then combines that surface data with light visibility and indirect illumination.',517,b)

    b=text(30,32,'LUMEN · GEOMETRY ANSWERS WHERE; CACHES ANSWER WHAT LIGHT','kicker')
    b+=box(30,64,255,102,'Probe / reflection ray',('Screen trace, when enabled','Reuse available screen data'))
    b+=arrow(285,115,335,115)+box(335,64,575,102,'A miss / unresolved region needs scene geometry',('Software: mesh / global distance fields and supported terrain','Hardware: acceleration structures for supported geometry'),'green')
    b+=arrow(620,167,620,220)+box(335,220,575,110,'Lighting at the hit',('Surface Cache: material captures + cached lighting','Optional hardware hit lighting: evaluate more at the hit','Cards parameterize surfaces; they are not intersection geometry.'),'amber')
    b+=box(30,220,255,110,'Not every surface exists',('Screen: current view only','Fields: geometry limits','RT: residency / cost limits'),'plain')
    b+=arrow(620,330,620,378)+box(335,378,575,78,'Filter / accumulate / integrate indirect illumination',('Combine with the main camera’s material and direct lighting.',))
    b+=text(30,497,'Missing geometry, missing cache coverage and stale lighting are three different failures.','strong')
    save('lumen-representations','Lumen intersection geometry is distinct from cached lighting','Screen traces fall back to supported distance fields or hardware ray geometry. Surface Cache stores material and lighting information at hits, not a universal intersection representation.',529,b)

    b=text(30,32,'PROPOSED ARDA DESIGN · DEPENDENCIES POINT DOWN','kicker')
    b+=box(30,64,415,100,'Application / importer / animation',('Own gameplay and asset authoring','Emit typed scene changes; do not record renderer work'))
    b+=box(495,64,415,100,'Frame renderer + feature consumers',('Depth / GBuffer / shadows / optional GI','Read a stable scene snapshot; declare graph work'),'amber')
    b+=arrow(236,164,236,218)+arrow(702,164,702,218)
    b+=box(30,218,880,105,'Scene core + representation providers',('Core: stable handles, asset / instance records, versions, deltas, retirement','Providers: geometry uploads, deformation, visibility, optional ray / distance-field / GI outputs'),'green')
    b+=arrow(236,323,236,376)+arrow(702,323,702,376)
    b+=box(30,376,415,95,'Existing ArdaBackend',('Persistent GPU storage, commands, device','Formats, resource states, completion'))
    b+=box(495,376,415,95,'Existing ArdaDependencyGraph',('Pass/resource dependencies and execution','Bind retained scene resources into graph work'))
    b+=text(30,513,'A provider declares capabilities and invalidation rules; the core does not become a GI renderer.','small')
    save('arda-boundaries','Proposed Arda scene module boundaries','Applications emit changes and render features read scene snapshots. Scene core coordinates IDs and versions, providers create specialized representations, and existing backend and dependency graph manage GPU storage and work.',545,b)

if __name__=='__main__': build()
