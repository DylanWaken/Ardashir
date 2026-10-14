"""Original, reproducible explanatory figures for the Arda graph essay."""
from pathlib import Path
from html import escape
import argparse
import json

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'Docs/Essays/assets/arda-graph'
INK = '#28352c'
GREEN = '#e4edda'
BLUE = '#e1edf0'
AMBER = '#f5e8cf'
RED = '#f5dfd8'
GRAY = '#ecece6'
FIGURES = {}


class Drawing:
    def __init__(self, name, title, description, height=470):
        self.name, self.title, self.description, self.height = name, title, description, height
        self.parts = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1000 {height}" role="img" aria-labelledby="title desc">',
                      f'<title id="title">{escape(title)}</title><desc id="desc">{escape(description)}</desc>',
                      '<defs><marker id="arrow" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0 0L8 4L0 8" fill="#52664d"/></marker></defs>',
                      f'<rect width="1000" height="{height}" fill="#fffdf8"/>',
                      '<style>text{font-family:Segoe UI,Arial,sans-serif;fill:#28352c} .small{font-size:17px}.label{font-size:19px}.title{font-size:26px;font-weight:600}.muted{fill:#637060}</style>']
        self.text(36, 44, title, 'title')

    def text(self, x, y, value, cls='label', anchor=None, fill=None):
        attrs = f' class="{cls}"' + (f' text-anchor="{anchor}"' if anchor else '') + (f' style="fill:{fill}"' if fill else '')
        self.parts.append(f'<text x="{x}" y="{y}"{attrs}>{escape(str(value))}</text>')

    def rect(self, x, y, w, h, color=GRAY, dashed=False):
        self.parts.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="7" fill="{color}" stroke="#829079" stroke-width="1.5"' + (' stroke-dasharray="6 5"' if dashed else '') + '/>')

    def box(self, x, y, w, h, lines, color=GREEN):
        self.rect(x, y, w, h, color)
        if isinstance(lines, str):
            lines = lines.split('|')
        first = y + h/2 - (len(lines)-1)*12 + 6
        for i, line in enumerate(lines):
            self.text(x+w/2, first+i*24, line, 'label' if i == 0 else 'small', 'middle')

    def line(self, x1, y1, x2, y2, arrow=True, dashed=False, color='#52664d'):
        self.parts.append(f'<path d="M{x1} {y1}L{x2} {y2}" fill="none" stroke="{color}" stroke-width="2"' + (' marker-end="url(#arrow)"' if arrow else '') + (' stroke-dasharray="6 5"' if dashed else '') + '/>')

    def route(self, points, dashed=False):
        d = 'M' + ' L'.join(f'{x} {y}' for x, y in points)
        self.parts.append(f'<path d="{d}" fill="none" stroke="#52664d" stroke-width="2" marker-end="url(#arrow)"' + (' stroke-dasharray="6 5"' if dashed else '') + '/>')

    def circle(self, x, y, label, r=30):
        self.parts.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{BLUE}" stroke="#62838b" stroke-width="1.5"/>')
        self.text(x, y+6, label, anchor='middle')

    def note(self, value, y=None):
        self.text(36, y or self.height-27, value, 'small muted')

    def finish(self, caption):
        content = '\n'.join(self.parts + ['</svg>']) + '\n'
        FIGURES[self.name] = {'svg': content, 'alt': self.description, 'caption': caption}


def create():
    d = Drawing('values-to-work', 'One result, several kinds of work', 'Input values 0, 1, 2, 3 are uploaded, transformed by B equals 2A plus 1, and read back as 1, 3, 5, 7.', 400)
    d.text(36, 95, 'The application asks for a value')
    for i, v in enumerate([0, 1, 2, 3]): d.box(40+i*74, 125, 62, 56, str(v), BLUE)
    d.line(345, 153, 414, 153)
    d.box(430, 115, 190, 76, 'B = 2A + 1')
    d.line(635, 153, 692, 153)
    for i, v in enumerate([1, 3, 5, 7]): d.box(710+i*66, 125, 56, 56, str(v), GREEN)
    d.text(36, 237, 'The machine needs an ordered protocol')
    for i, (a,b) in enumerate([('Upload', 'CPU → logical A'), ('Transform', 'read A; write B'), ('Readback', 'B → CPU result')]):
        d.box(40+i*330, 263, 260, 75, [a,b], [BLUE,GREEN,AMBER][i])
        if i<2: d.line(307+i*330,300,360+i*330,300)
    d.finish('The running example asks for four output numbers. Its implementation needs transfers, declared effects, ordering, physical storage and a completion boundary. The transform is supplied code; Arda does not derive a kernel from this equation.')

    d = Drawing('model-workload', 'Keep operations and resources separate', 'Upload produces A. Transform reads A and writes B. Readback observes B. A debug branch writes D; D is observable only if a consumer or output declaration requires it.', 430)
    d.box(35,140,135,70,'Upload',BLUE); d.circle(245,175,'A'); d.box(325,140,160,70,'Transform'); d.circle(560,175,'B'); d.box(640,140,150,70,'Readback',AMBER); d.box(850,140,110,70,'CPU result',AMBER)
    for a,b in [(170,215),(275,325),(485,530),(590,640),(790,850)]: d.line(a,175,b,175)
    d.route([(245,205),(245,305),(325,305)],True)
    d.box(325,270,160,70,'Debug',GRAY); d.circle(560,305,'D')
    d.line(485,305,530,305, dashed=True)
    d.text(620,292,'Keep this branch if D is observed.','small')
    d.text(620,319,'Cull it if D has no observable use.','small')
    d.note('Rectangles = operations. Circles = logical resources. Arrows here show data use, before hazard analysis.')
    d.finish('A resource is not an operation and an allocation declaration is not yet native storage. The optional diagnostic branch gives us a concrete dead-work example without changing the main result.')

    d = Drawing('model-handles', 'A handle names an identity, not an address', 'A graph handle contains graph identity, slot index and generation. Deleting a node and reusing its slot increments the generation, rejecting the old handle. Another graph also rejects the handle.', 430)
    d.box(40,100,300,90,['Old handle','graph 7 · slot 3 · generation 12'],BLUE)
    d.box(540,100,400,90,['Before removal: Transform','graph 7 · slot 3 · generation 12'])
    d.line(350,145,530,145); d.text(412,126,'valid','small')
    d.box(540,235,400,90,['After reuse: Debug','graph 7 · slot 3 · generation 13'],AMBER)
    d.route([(340,160),(425,160),(425,280),(530,280)],True)
    d.text(40,263,'Same slot, different occupant.','label')
    d.text(40,298,'Generation mismatch → reject.','label')
    d.note('Graph 9 also rejects this handle, even if its slot 3 has generation 12. Lookup checks all three parts.')
    d.finish('Slot reuse must not resurrect a deleted operation. Graph identity and generation preserve meaning even when compact storage and iteration order change.')

    d = Drawing('model-resource-views', 'Views add interpretation; accesses add effects', 'One 64-byte logical buffer can have a float32 tensor view of shape 4 by 4. Accesses to its first and second 32-byte regions are disjoint; an overwrite of the first region conflicts with a read of that region.', 470)
    d.box(40,90,280,90,['FArdaGraphBuffer A','logical identity + descriptor'],BLUE)
    d.box(600,90,350,90,['FArdaGraphTensor(A)','float32 · shape [4, 4]'],GREEN)
    d.line(590,135,330,135); d.text(370,115,'same storage identity','small')
    d.text(40,229,'Byte ranges on A (64 bytes total)')
    d.box(230,250,350,50,'[0, 32)',BLUE); d.box(580,250,350,50,'[32, 64)',GREEN)
    d.text(40,334,'Transform read','small'); d.rect(230,314,350,29,BLUE)
    d.text(40,376,'Debug read','small'); d.rect(580,356,350,29,GREEN)
    d.text(40,418,'Overwrite','small'); d.rect(230,398,350,29,AMBER)
    d.finish('The tensor descriptor does not allocate or copy data. Dependency analysis compares access regions: the overwrite conflicts with the first read, while the other half can remain independent.')

    d = Drawing('model-node-contract', 'One declaration feeds analysis and execution', 'Parameters supply effects, outputs and semantic keys. Compilation prepares shared immutable device state and private instance state. Frame recording resolves logical resources and invokes the implementation.', 520)
    d.box(35,95,250,95,['Typed parameters','A, B, multiplier, offset'],BLUE)
    d.box(380,95,250,95,['Declaration','effects · outputs · key'])
    d.box(715,95,250,95,['Frozen node','semantic description'])
    d.line(295,143,370,143);d.line(640,143,705,143)
    d.box(170,265,290,95,['Prepare(Device)','shared immutable setup'],BLUE)
    d.box(540,265,290,95,['CreateInstance','private operation state'],AMBER)
    d.text(165,249,'May be obtained during declaration','small')
    d.text(550,249,'Created during compilation','small')
    d.route([(170,312),(140,312),(140,215),(505,215),(505,200)])
    d.route([(840,190),(900,215),(900,312),(840,312)])
    d.line(470,312,530,312)
    d.box(265,410,480,60,'Record: resolve A/B and emit the operation')
    d.line(315,370,400,400); d.line(685,370,610,400)
    d.note('Authoring and node preparation describe or prepare work; they do not submit the graph to the GPU.')
    d.finish('The compiler needs explicit effects because executable C++ or shader code alone is not a dependency declaration. Device preparation and per-operation state have different ownership and reuse boundaries.')

    d = Drawing('model-revision-state', 'Construction publication and execution are separate', 'Private edits commit to a queued construction. Compile selects and locks a revision. An execution attempt unlocks it. Failed compilation keeps the construction queued. Equivalent later constructions can select an existing revision.', 540)
    for x,label,color in [(40,'Private edit|author a construction',BLUE),(370,'Queued edits|Commit publishes',GREEN),(700,'Locked revision|Compile succeeds',AMBER)]: d.box(x,95,260,85,label,color)
    d.line(310,138,360,138);d.line(640,138,690,138)
    d.box(700,270,260,85,'Unlocked revision|after execution attempt',GREEN)
    d.line(830,190,830,260);d.text(525,225,'Submit / Execute attempt','small')
    d.route([(705,182),(665,220),(500,220),(500,190)],True);d.text(340,250,'Compile failure: keep queued edits','small')
    d.route([(700,313),(170,313),(170,190)])
    d.text(45,351,'Next ordinary construction starts empty. Compatible revisions remain cached.','small')
    d.text(40,405,'Later: describe Normal → describe Debug → describe Normal')
    for i,label in enumerate(['select revision 1','select revision 2','reuse revision 1']):
        d.box(40+i*330,430,260,58,label,[GREEN,BLUE,GREEN][i])
        if i<2: d.line(310+i*330,459,360+i*330,459)
    d.finish('Commit queues meaning; Compile stages an executable revision; an execution attempt unlocks authoring. Persistent revision reuse does not mean every new ordinary edit appends to the previous frame forever.')

    d = Drawing('model-composition', 'Composition stays inside the same semantic model', 'A Transform subgraph expands to Normalize, Scale and Clamp with declared interfaces. A serialized graph loads external slots that must be bound before compilation.', 460)
    d.box(40,90,230,90,'Transform(A) → B',BLUE)
    d.line(280,135,340,135)
    for i,label in enumerate(['Normalize|A → T','Scale|T → U','Clamp|U → B']):
        d.box(355+i*210,90,180,90,label)
        if i<2: d.line(540+i*210,135,560+i*210,135)
    d.text(40,225,'Expand the body, remap logical handles, preserve the interface.','small')
    for i,label in enumerate(['Graph archive|semantic declarations','Load + Bind|named external slots','Compile|execution plan']):
        d.box(40+i*330,285,260,90,label,[BLUE,AMBER,GREEN][i])
        if i<2:d.line(310+i*330,330,360+i*330,330)
    d.note('A graph archive and a specialized .ardagpu kernel package are different serialization boundaries.')
    d.finish('Subgraphs compose through typed resources and stage interfaces. Serialization preserves the semantic program; externally owned values are rebound deliberately at load/use boundaries.')

    d = Drawing('compiler-stages', 'Two transformations with different outputs', 'Preprocessing creates a graph with explicit effects and dependencies. Compilation consumes the preprocessed graph and creates a separate execution plan. Standalone calls operate on private copies.', 460)
    d.box(40,90,265,100,['Authored graph','Upload · Transform · Readback','optional Debug'],BLUE)
    d.box(370,90,265,100,['Preprocessed graph','edges · stage assembly','live / culled annotations'])
    d.box(700,90,265,100,['Compiled plan','order · memory · queues','dispatches · fences'],AMBER)
    d.line(315,140,360,140); d.line(645,140,690,140)
    d.text(40,234,'Standalone Preprocess returns a graph. Standalone Compile returns a plan.','small')
    for x,label in [(40,'Original authored graph'),(700,'Retained compile snapshot')]:d.box(x,280,265,70,label,GRAY)
    d.text(352,322,'private copies preserve the input','small')
    d.note('Graph.Compile() orchestrates preprocessing, compilation and persistent-revision selection.')
    d.finish('Preprocessing makes the program explicit without choosing a complete device schedule. Compilation chooses an internally consistent execution plan and retains the input needed to use it.')

    d = Drawing('compiler-hazards', 'Which contents does a reader observe?', 'With one writer, dependency resolution orders the writer before the reader even if authored later. With repeated overlapping writes, attachment order establishes each read version; readers precede the next overwrite.', 550)
    d.text(40,98,'ONE WRITER: author Read A first, Upload A second','small')
    d.box(70,122,250,70,'Upload A',BLUE);d.box(570,122,250,70,'Read A')
    d.line(330,157,560,157);d.text(356,139,'execution dependency','small')
    d.text(40,255,'REPEATED WRITES: successful attachment order defines the observed contents','small')
    d.box(40,340,170,70,'Write B₀',BLUE)
    d.box(300,285,170,60,'Read B₀ / R₁');d.box(300,395,170,60,'Read B₀ / R₂')
    d.box(560,340,170,70,'Write B₁',AMBER);d.box(810,340,150,70,'Read B₁')
    d.line(210,360,290,315);d.line(210,390,290,425)
    d.line(480,315,550,360,dashed=True);d.line(480,425,550,390,dashed=True)
    d.line(740,375,800,375)
    d.route([(125,340),(125,274),(645,274),(645,330)],True)
    d.note('Solid = produced value. Dashed = overwrite hazard. No reader-to-reader dependency is required.')
    d.finish('B₀ and B₁ label successive contents of the same logical resource for explanation; they are not immutable SSA handles. Hazard edges preserve safe overwrite order without inventing value consumption.')

    d = Drawing('compiler-range-coverage', 'Coverage is about regions, not resource names', 'Two adjacent writes cover a 1024-byte read. A 12-byte gap leaves an undefined region. Texture mip and slice ranges distinguish independent subresources.', 530)
    d.text(40,98,'Complete coverage of a 1024-byte read')
    d.box(80,120,420,60,'P writes [0, 512)',BLUE);d.box(500,120,420,60,'Q writes [512, 1024)',GREEN)
    d.box(310,225,380,55,'Reader needs both P and Q',AMBER)
    d.line(290,190,390,215);d.line(710,190,610,215)
    d.text(40,328,'A gap is not initialization')
    d.rect(80,350,400,48,BLUE);d.rect(480,350,20,48,RED);d.rect(500,350,420,48,GREEN)
    d.text(280,381,'P ends at byte 500',anchor='middle');d.text(710,381,'Q starts at byte 512',anchor='middle')
    d.text(450,429,'12 bytes undefined','small')
    d.note('Textures use mip / slice / plane ranges. A write to mip 0 does not supply the contents of mip 1.')
    d.finish('The compiler must prove that required regions have producers or valid initialized/external contents. Matching a buffer name is insufficient if the declared byte intervals leave a hole.')

    d = Drawing('compiler-culling', 'Observability traces backward through dependencies', 'Readback is an observable root. Its ancestors Upload and Transform remain live. An unused Debug branch is marked culled. Writes to external or persistent resources are also roots.', 460)
    d.box(40,150,190,70,'Upload A',BLUE);d.box(350,95,220,70,'Transform A → B');d.box(710,95,240,70,'Readback B: root',AMBER)
    d.box(350,250,220,70,'Debug A → D',GRAY)
    d.line(240,175,340,130);d.line(580,130,700,130)
    d.line(240,195,340,285,dashed=True)
    d.text(620,285,'D has no observable use → culled','small')
    d.text(40,370,'Other roots: side effects, writes to output / persistent / external resources.','small')
    d.note('Validation precedes culling. An unused invalid declaration is not permission to skip correctness checks.')
    d.finish('Culling keeps the incoming ancestor set of observable roots, including ordering predecessors. It is conservative liveness analysis, not a general optimizer that proves earlier overwrites irrelevant.')

    d = Drawing('compiler-pipeline-composition', 'Stage composition is distinct from shader compilation', 'Graphics stage contributions assemble into one node-local pipeline description. Inductor validates bindings and contracts. Native pipeline objects are created for device execution. Shader code must already be supplied or compiled through its own toolchain.', 500)
    for i,label in enumerate(['Vertex stage','Raster stage|fixed state','Pixel stage|terminal draw']):
        d.box(40+i*330,90,260,78,label,BLUE)
        if i<2:d.line(310+i*330,129,360+i*330,129)
    d.box(270,245,460,80,'Assembled operation-local description|shader stages + bindings + render state')
    d.line(170,178,350,235);d.line(500,178,500,235);d.line(830,178,650,235)
    d.box(70,385,350,65,'Infer and validate pipeline',GREEN);d.box(590,385,350,65,'Prepare native pipeline',AMBER)
    d.line(430,418,580,418);d.route([(500,335),(500,355),(245,355),(245,375)])
    d.note('Combining compatible stage declarations does not synthesize or fuse arbitrary shader instructions.')
    d.finish('Pipeline inference composes the stages and contracts of an operation. It neither invents the shader arithmetic nor treats every edge between GPU operations as a request for shader fusion.')

    d = Drawing('compiler-dispatch-dag', 'Declaration nodes can disappear while ordering survives', 'Upload, declaration gate, compute and readback form semantic order. The gate has no dispatch. Dispatch dependencies retain upload before compute before readback; cross-domain edges require fences.', 500)
    for i,label in enumerate(['Upload','Declaration gate','Compute','Readback']):
        d.box(35+i*245,95,195,65,label,GRAY if i==1 else GREEN)
        if i<3:d.line(235+i*245,128,270+i*245,128)
    d.text(40,220,'Illustrative placement: copy queue and graphics queue','small')
    d.text(40,290,'Copy','small');d.text(40,406,'Graphics','small')
    d.box(180,250,220,65,'D0: Upload',BLUE);d.box(730,250,220,65,'D2: Readback',BLUE)
    d.box(455,365,220,65,'D1: Compute',GREEN)
    d.line(410,283,720,283);d.text(472,267,'queue order','small')
    d.line(400,315,455,365);d.line(675,365,730,315)
    d.text(257,360,'fence','small');d.text(768,361,'fence','small')
    d.note('Logical fence = producer/consumer dispatch IDs. Native timeline values are assigned at submission.')
    d.finish('The declaration gate contributes ordering without executable work. Same-queue dependencies and cross-domain fences are related but different products of the compiled dispatch graph.')

    d = Drawing('exec-queue-timeline', 'Scheduling trades serial time for eligible overlap', 'An independent graphics branch costs 12 units and four compute steps cost 8 total. Serialized cost is 20 units; an eligible second queue reduces the idealized critical path to 12. Numbers ignore contention and are illustrative.', 480)
    scale=36;origin=190
    d.text(40,103,'One queue','small')
    d.box(origin,80,12*scale,65,'Graphics G · 12',GREEN)
    for i in range(4):d.box(origin+(12+i*2)*scale,80,2*scale,65,f'C{i+1}',BLUE)
    d.text(40,230,'Graphics queue','small');d.box(origin,200,12*scale,65,'G · 12',GREEN)
    d.text(40,330,'Compute queue','small')
    for i in range(4):d.box(origin+i*2*scale,300,2*scale,65,f'C{i+1}',BLUE)
    d.line(origin,400,origin+20*scale,400,False)
    for v in [0,8,12,20]:d.text(origin+v*scale,430,str(v),'small','middle')
    d.note('Illustrative cost units: 20 serialized; max(12, 8) = 12 with eligible overlap. Shared start and final join.')
    d.finish('A scheduler optimizes among legal choices. The idealized diagram exposes the opportunity; real queue eligibility, memory constraints, synchronization overhead and contention decide whether it is worthwhile.')

    d = Drawing('exec-alias-proof', 'A linear list alone cannot prove safe memory reuse', 'Independent graphics and compute work can overlap despite an apparent first-last order. Two 64 MiB resources need 128 MiB during overlap. A dependency from last use of X to first use of Y allows one compatible 64 MiB region to be reused.', 510)
    d.text(40,102,'Parallel schedule: X and Y overlap on different queues','small')
    d.text(40,166,'Graphics','small');d.box(210,130,430,62,'X · 64 MiB',GREEN)
    d.text(40,245,'Compute','small');d.box(340,210,430,62,'Y · 64 MiB',BLUE)
    d.text(800,198,'128 MiB live','small')
    d.text(40,323,'Add X.last → Y.first: less overlap, but storage can be reused','small')
    d.box(120,355,360,65,'X occupies region R',GREEN);d.box(565,355,360,65,'Y occupies region R',BLUE)
    d.line(490,388,555,388);d.text(430,457,'one 64 MiB region','small')
    d.note('Illustrative compatible allocation requirements, one frame. Alignment and resource-class rules still apply.')
    d.finish('Aliasing is a happens-before proof plus compatible physical requirements. The compiler emits the ordering and activation boundaries required by its packing decision; memory reuse can reduce parallelism.')

    d = Drawing('exec-batch-domains', 'Several operations can share one executable group', 'Native N0, CUDA C0 and C1, declaration Sync, CUDA C2, native N1 and CUDA C3 become four groups. Sync does not split CUDA work. Draw instance ranges merge only when consecutive and otherwise compatible.', 510)
    labels=['N0','C0','C1','Sync','C2','N1','C3']
    for i,label in enumerate(labels):d.box(40+i*135,95,110,60,label,GRAY if label=='Sync' else (BLUE if label.startswith('C') else GREEN))
    d.text(40,205,'A declaration contracts into dependencies; executable native work splits the CUDA run.','small')
    for x,w,label,c in [(40,160,'Native N0',GREEN),(245,330,'CUDA C0, C1, C2',BLUE),(620,160,'Native N1',GREEN),(825,135,'CUDA C3',BLUE)]:d.box(x,235,w,65,label,c)
    d.text(40,364,'A stronger optimization: compatible contiguous instance ranges','small')
    d.box(40,390,260,65,'[0, 4) + [4, 7)',GREEN);d.line(310,423,380,423);d.box(390,390,210,65,'one [0, 7) draw',GREEN)
    d.box(660,390,300,65,'[0, 4) + [0, 3): separate',AMBER)
    d.note('Batching reduces recording/submission overhead. Instancing may replace draws. Neither implies shader fusion.')
    d.finish('The grouping level determines what changes. A pipeline batch still records its member operations; an eligible instancing group can become one actual draw after physical state checks.')

    d = Drawing('exec-lowering', 'Four identities describe four different boundaries', 'Semantic node handles map to compiled dispatch IDs, then to commands bound to frame-slot resources, then to accepted native submission tokens. Entry and exit commands establish reusable resource states.', 560)
    rows=[('Semantic', ['Upload(A)','Transform(A,B)','Readback(B)'],BLUE),('Dispatches',['D0','D1','D2'],GREEN),('Bound commands',['C0: A#slot0','C1: A#0 → B#0','C2: B#slot0'],AMBER),('Native receipts',['token U','token C','token R'],GRAY)]
    for j,(title,items,color) in enumerate(rows):
        y=95+j*110;d.text(30,y+36,title,'small')
        for i,label in enumerate(items):
            x=200+i*260;d.box(x,y,220,65,label,color)
            if i<2:d.line(x+225,y+33,x+250,y+33)
            if j<3:d.line(x+110,y+70,x+110,y+100)
    d.note('Entry/exit commands bracket physical state transitions. The private program consumes an already chosen plan.')
    d.finish('A node handle, dispatch ID, command ID and native token are not interchangeable. Lowering binds the selected plan to one frame’s physical objects and emits the barrier/submission protocol.')

    d = Drawing('exec-frame-receipts', 'Frame slots are reusable storage; tickets are receipts', 'Frames 0 and 1 occupy two slots and return separate tickets. Frame 2 must retire frame 0 before reusing slot 0. IsComplete polls readiness; Wait drains readbacks and publishes CPU data.', 520)
    d.text(40,97,'CPU submission order →','small')
    for i,label in enumerate(['F0 → slot 0','F1 → slot 1','F2 → slot 0']):d.box(40+i*330,120,260,65,label,BLUE)
    for i,label in enumerate(['Ticket T0','Ticket T1','Ticket T2']):d.box(40+i*330,245,260,65,label,GREEN);d.line(170+i*330,195,170+i*330,235)
    d.text(690,222,'first retire F0','small')
    d.text(40,360,'T0 can remain a completed receipt after slot 0 is reused.','label')
    d.box(40,395,420,65,'IsComplete: query readiness',GRAY)
    d.box(540,395,420,65,'Wait: completion + drain results',AMBER)
    d.note('Submit may block on occupied slots. Failed Submit can still return a valid ticket for already accepted GPU work.')
    d.finish('A completed GPU token and a published host readback are different observations. Retain and wait a valid ticket even when a later submission step fails, so accepted work and its resources retire safely.')

    d = Drawing('exec-adaptive-loop', 'Measurements propose a new schedule, not a new program', 'Collected GPU timings feed smoothed cost estimates. An immutable snapshot is searched under fixed resource constraints. Revision, epoch, readiness and improvement gates control adoption; rejected proposals preserve the incumbent.', 520)
    for i,label in enumerate(['GPU samples|collected regions','Smooth costs|EMA / valid identity','Candidate search|fixed pool constraints']):
        d.box(40+i*330,90,260,90,label,[BLUE,GREEN,AMBER][i])
        if i<2:d.line(310+i*330,135,360+i*330,135)
    d.box(700,270,260,90,'Adoption gate|revision · epoch · ready|modeled improvement',AMBER)
    d.line(830,190,830,260)
    d.box(40,270,260,90,'Current schedule|keep or replace',GREEN)
    d.line(690,315,310,315);d.text(384,297,'validated proposal only','small')
    d.route([(170,270),(170,190)],True)
    d.text(40,424,'Illustrative EMA: 10 + 0.2 × (14 − 10) = 10.8 ms','label')
    d.note('Measurements refine costs. Semantic dependencies, alias constraints and output values stay invariant.')
    d.finish('The tuning worker evaluates a retained snapshot. Adoption must still match the active revision and timing epoch, and it must preserve the memory plan’s ordering proof. A modeled win is not a measured wall-clock speedup.')

    d = Drawing('package-offline-compiler', 'Offline code generation meets runtime graph planning', 'Strict torch.export capture produces ONNX. Pinned IREE compilation and bounded static HAL evaluation produce a package with PTX and explicit recipe data. Runtime imports the recipe as Arda graph operations. This route does not call TorchInductor.', 620)
    d.text(40,92,'OFFLINE: specialize the computation and emit device code','small')
    for i,label in enumerate(['Module + examples|shapes · dtypes · scalars','strict torch.export|then ONNX','IREE 3.11.0|HAL compilation']):
        d.box(40+i*330,115,260,90,label,[BLUE,GREEN,GREEN][i])
        if i<2:d.line(310+i*330,160,360+i*330,160)
    d.box(700,270,260,80,'Static HAL evaluation|bounded supported subset',AMBER)
    d.line(830,215,830,260)
    d.box(40,260,540,100,'.ardagpu|PTX + kernel ABI + constants + tensor ports|ordered dispatch / fill / copy recipe',BLUE)
    d.line(690,310,590,310)
    d.text(40,420,'RUNTIME: combine compiled computation with the rest of the frame','small')
    for i,label in enumerate(['Load + Invoke|caller tensor bindings','Ordinary graph ops|declared reads / writes','ArdaInductor|schedule + memory plan']):
        d.box(40+i*330,445,260,90,label,[BLUE,GREEN,AMBER][i])
        if i<2:d.line(310+i*330,490,360+i*330,490)
    d.note('ArdaInductor plans graph execution. This offline export route does not call PyTorch TorchInductor.')
    d.finish('The offline compiler supplies kernel implementations and a closed recipe. The runtime graph planner integrates that recipe with transfers, rendering and other compute work; it does not recompile the model’s arithmetic.')

    d = Drawing('package-runtime', 'Bind the backing storage contract, not only visible elements', 'Package invocation emits ordered graph commands. A caller buffer can hold two disjoint 64-byte package storage ranges, even if each exposed tensor has only 16 visible bytes. Moving the second origin to byte 32 overlaps the package backing extents and is rejected.', 610)
    for i,label in enumerate(['Input tensors','Fill workspace','Kernel dispatch','Prediction → reader']):
        d.box(35+i*245,95,195,70,label,[BLUE,AMBER,GREEN,BLUE][i])
        if i<3:d.line(235+i*245,130,270+i*245,130)
    d.text(40,209,'Recipe neighbors receive explicit ordering; constants use initialized persistent storage.','small')
    d.text(40,272,'Caller allocation: 128 bytes. Exposed Float32[4] tensors each contain 16 bytes.','small')
    d.box(80,300,420,65,'First backing extent [0, 64)',BLUE)
    d.box(500,300,420,65,'Second backing extent [64, 128)',GREEN)
    d.rect(80,375,105,22,BLUE);d.rect(500,375,105,22,GREEN)
    d.text(80,426,'visible [0, 16)','small');d.text(500,426,'visible [64, 80)','small')
    d.text(40,474,'Rejected: second origin at byte 32 overlaps the first backing extent.','small')
    d.rect(80,495,420,44,BLUE);d.rect(290,547,420,34,RED)
    d.text(290,524,'first [0, 64)','small','middle');d.text(500,571,'second [32, 96)','small','middle')
    d.finish('A tensor’s visible element count may be smaller than the compiled storage extent it binds. Package validation protects the latter, so apparently disjoint visible tensors cannot hide overlapping backing storage.')


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');args=ap.parse_args()
    create();OUT.mkdir(parents=True,exist_ok=True)
    for name,data in FIGURES.items():
        target=OUT/(name+'.svg')
        if args.check: assert target.read_text(encoding='utf-8')==data['svg'],str(target)
        else:target.write_text(data['svg'],encoding='utf-8')
    meta=json.dumps({key:{k:v for k,v in value.items() if k!='svg'} for key,value in FIGURES.items()},indent=2,ensure_ascii=False)+'\n'
    target=Path(__file__).with_name('figures.json')
    if args.check:assert target.read_text(encoding='utf-8')==meta,'Stale figure metadata'
    else:target.write_text(meta,encoding='utf-8')
    print(f'{"Verified" if args.check else "Generated"} {len(FIGURES)} original SVG diagrams')


if __name__=='__main__':main()
