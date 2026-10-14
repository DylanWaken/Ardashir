"""Reproduce the scene article's original mathematical illustrations, using only Python's standard library.

These are deliberately small teaching examples, not Unreal captures or Unreal performance measurements.
Run from any directory. SVG geometry, pixel rasterization and PNG encoding are computed here.
"""
from pathlib import Path
from html import escape
import base64
import json
import math
import struct
import zlib

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
OUT = ROOT / 'Docs/Unreal/assets/walkthrough'
OUT.mkdir(parents=True, exist_ok=True)
FIGURES = []
ANCHORS = {
    'indexed-slab': 'A vertex record usually contains more than position.',
    'coordinate-projection': 'Now place the slab three meters from the courtyard origin.',
    'ownership-and-draw-counts': 'Now count the illustrative draw submissions.',
    'thread-snapshots': 'The add path illustrates the handoff precisely.',
    'removal-fence': 'Identity has to survive these transitions.',
    'gpu-byte-address': 'The physical buffer layout is an implementation decision',
    'raster-depth-winners': 'Before giving Unreal',
    'alpha-and-volume': 'A ray marcher approximates this integral',
    'gate-attachment': 'For the gate, imagine an actor containing',
    'weighted-skinning': 'Skinning introduces transforms inside the object.',
    'deformation-bounds': 'Bounds must follow the same physical reality.',
    'material-response': 'Material instances reuse a parent material',
    'one-pixel-through-the-pipeline': 'Rasterization first answers',
    'courtyard-gbuffer': "Unreal's <code>FGBufferData</code>",
    'off-camera-shadow': 'Shadows add another visibility question.',
    'intersection-versus-cache': 'A ray intersection is only part of a lighting answer.',
    'point-provider-revisions': 'Now edit one scanned point.',
}
INK, MUTED, PAPER = '#263a36', '#53665f', '#fcfaf5'
GREEN, BLUE, RUST, GOLD = '#237665', '#326f96', '#a74834', '#ac7420'
VERT = ['#bd4939', '#287c6b', '#3d72b0', '#aa7925']
CH = ['a-triangle-becomes-an-object', 'the-world-is-not-one-tree', 'crossing-the-thread-boundary',
      'changes-have-lifetimes', 'the-scene-becomes-gpu-data', 'from-a-proxy-to-a-draw',
      'the-courtyard-needs-more-than-static-meshes', 'motion-is-more-than-a-matrix',
      'a-material-is-a-surface-program', 'a-visible-surface-becomes-a-lit-pixel',
      'the-camera-does-not-see-the-whole-lighting-problem', 'deriving-an-arda-scene-from-the-courtyard']


class SVG:
    def __init__(self, title, desc, height=620):
        self.height = height
        self.data = [f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" viewBox="0 0 1040 {height}" role="img" aria-labelledby="title desc"><title id="title">{escape(title)}</title><desc id="desc">{escape(desc)}</desc>',
                     f'<defs><marker id="arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0L10 5L0 10Z" fill="{GREEN}"/></marker></defs>',
                     f'<rect width="1040" height="{height}" fill="{PAPER}"/>']
        self.text(30, 40, title, 26, weight='bold')

    def text(self, x, y, value, size=22, color=INK, anchor='start', weight='normal'):
        self.data.append(f'<text x="{x}" y="{y}" font-family="Arial, sans-serif" font-size="{size}" fill="{color}" text-anchor="{anchor}" font-weight="{weight}">{escape(str(value))}</text>')

    def lines(self, x, y, values, size=22, color=INK, anchor='start', gap=29):
        for i, value in enumerate(values): self.text(x, y+i*gap, value, size, color, anchor)

    def rect(self, x, y, width, height, fill='none', stroke=MUTED, sw=2, rx=0, dash=''):
        self.data.append(f'<rect x="{x}" y="{y}" width="{width}" height="{height}" rx="{rx}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"' + (f' stroke-dasharray="{dash}"' if dash else '') + '/>')

    def line(self, x1, y1, x2, y2, color=GREEN, sw=2, arrow=False, dash=''):
        self.data.append(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{color}" stroke-width="{sw}"' + (' marker-end="url(#arrow)"' if arrow else '') + (f' stroke-dasharray="{dash}"' if dash else '') + '/>')

    def path(self, path, stroke=GREEN, fill='none', sw=2, arrow=False):
        self.data.append(f'<path d="{path}" stroke="{stroke}" fill="{fill}" stroke-width="{sw}"' + (' marker-end="url(#arrow)"' if arrow else '') + '/>')

    def poly(self, points, fill, stroke=INK, sw=2):
        self.data.append('<polygon points="'+' '.join(f'{x:.2f},{y:.2f}' for x,y in points)+f'" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"/>')

    def circle(self, x, y, r, fill=GREEN, stroke=None, sw=2):
        self.data.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{fill}" stroke="{stroke or fill}" stroke-width="{sw}"/>')

    def image(self, x, y, width, height, png):
        encoded=base64.b64encode(png).decode()
        self.data.append(f'<image x="{x}" y="{y}" width="{width}" height="{height}" href="data:image/png;base64,{encoded}"/>')

    def save(self, name, chapter, alt, caption):
        (OUT/(name+'.svg')).write_text('\n'.join(self.data)+'\n</svg>\n', encoding='utf-8')
        FIGURES.append({'chapter': CH[chapter-1], 'after_contains': ANCHORS[name], 'html': f'<figure><img src="assets/walkthrough/{name}.svg" alt="{escape(alt, quote=True)}" width="1040" height="{self.height}" loading="lazy"><figcaption>{caption}</figcaption></figure>'})
        (HERE/'walkthrough.json').write_text(json.dumps(FIGURES, indent=2, ensure_ascii=False)+'\n', encoding='utf-8')


def slab(s,x,y,w=90,h=24,fill='#d8d5bf'):
    s.poly([(x,y),(x+w,y),(x+w+17,y-h/2),(x+17,y-h/2)], fill, MUTED, 1)
    s.poly([(x,y),(x+w,y),(x+w,y+12),(x,y+12)], '#a9ac99', MUTED, 1)
    s.poly([(x+w,y),(x+w+17,y-h/2),(x+w+17,y),(x+w,y+12)], '#bec0aa', MUTED, 1)


def indexed_slab():
    s=SVG('Four vertices, two triangles, one shared slab', 'Colored vertex positions match colored rows in the vertex buffer. Two index triples reuse vertices zero and two. Texture coordinates give the surface a separate two-dimensional address.',620)
    pts=[(80,280),(295,350),(392,167),(180,100)]
    s.poly([pts[i] for i in [0,1,2]],'#e4ede2',GREEN)
    s.poly([pts[i] for i in [0,2,3]],'#e6dcc5',GOLD)
    for i,(x,y) in enumerate(pts):
        s.circle(x,y,9,VERT[i]);s.text(x-12,y-17,f'v{i}',24,VERT[i])
    s.text(214,296,'triangle 0',21,GREEN,'middle');s.text(235,173,'triangle 1',21,GOLD,'middle')
    s.text(52,417,'Index buffer',24,weight='bold')
    for i,indices in enumerate([[0,1,2],[0,2,3]]):
        s.text(53,461+i*50,f't{i}',23)
        for j,n in enumerate(indices):
            s.rect(100+j*76,433+i*50,65,38,'#f4f0e6',VERT[n],2)
            s.text(132+j*76,460+i*50,n,24,VERT[n],'middle')
    s.text(510,95,'Vertex buffer — teaching layout',24,weight='bold')
    s.text(514,144,'id',21);s.text(578,144,'position (x,y,z)',21);s.text(842,144,'UV (u,v)',21)
    rows=[('0','(0, 0, 0)','(0, 0)'),('1','(100, 0, 0)','(1, 0)'),('2','(100, 100, 0)','(1, 1)'),('3','(0, 100, 0)','(0, 1)')]
    for i,(a,b,c) in enumerate(rows):
        y=190+i*53;s.line(505,y+17,996,y+17,'#ccd2c7',1)
        s.circle(522,y-7,6,VERT[i]);s.text(542,y,a,22,VERT[i]);s.text(579,y,b,22);s.text(851,y,c,22)
    s.lines(510,428,['Each row also supplies a surface normal.','Here all four normals are (0, 0, 1).','The two triangles share v0 and v2.'],21)
    s.lines(31,572,['A vertex index selects a whole attribute row; a UV selects a place in a texture.'],22)
    s.save('indexed-slab',1,'A two-triangle slab with four colored vertices, matching position and UV rows, and the index triples 0,1,2 and 0,2,3.','Read the colors across the geometry and the arrays: the green row is always vertex 1, whether a triangle references it once or many times. The diagonal is shared through indices 0 and 2. Position values use the chapter’s 100-centimeter square slab. This is an illustrative mesh layout, not the binary layout of a particular Unreal vertex factory.')


def transforms():
    s=SVG('Compute the same triangle in local, world, and image space', 'The slab triangle is translated three meters, then a pinhole camera computes screen position by dividing by camera depth. All plotted points are computed from the printed coordinates.',650)
    local=[(0,0,0),(100,0,0),(100,100,0)]
    world=[(x+300,y,z) for x,y,z in local]
    camera=[(x-250,y,400-z) for x,y,z in world]
    pixel=[(160+240*x/z,120-240*y/z) for x,y,z in camera]
    s.lines(173,86,['Local coordinates','asset-centered axes'],22,anchor='middle')
    lp=[(80+1.7*x,321-1.7*y) for x,y,z in local]
    s.line(55,321,300,321,MUTED,2,True);s.line(80,347,80,135,MUTED,2,True)
    s.poly(lp,'#e3eadd',GREEN)
    s.lines(509,86,['World coordinates','translation + (300,0,0) cm'],22,anchor='middle')
    wp=[(389+0.55*x,321-0.55*y) for x,y,z in world]
    s.line(389,321,665,321,MUTED,2,True);s.line(389,347,389,164,MUTED,2,True);s.poly(wp,'#e4dcc5',GOLD)
    s.text(389,374,'(0,0)',18,anchor='middle');s.text(554,356,'300',18,anchor='middle');s.text(609,356,'400',18,anchor='middle')
    s.lines(853,86,['Image coordinates','x = cx + f X / Z'],22,anchor='middle')
    s.rect(711,158,301,231,'#e7ece7',MUTED,1)
    pp=[(711+px*301/320,158+py*231/240) for px,py in pixel];s.poly(pp,'#d5e4ee',BLUE)
    for points in [lp,wp,pp]:
        for i,(x,y) in enumerate(points):s.circle(x,y,7,VERT[i])
    s.lines(32,441,['Track the red vertex v0:'],24)
    s.lines(50,481,['Local: (0,0,0)','World: (300,0,0) cm'],22)
    s.lines(362,467,['Camera coordinates: (50,0,400)','positive forward depth = 400 cm','f = 240 px; center = (160,120)'],20)
    s.lines(763,481,['Image: (190,120)','240 × 50 / 400 = 30 px'],20)
    s.lines(32,582,['Computed mathematical projection. Same vertex identity; three different coordinate systems.','Camera at (250,0,400) cm, looking along −Z; image Y increases downward.'],21)
    s.save('coordinate-projection',1,'Computed plots of a colored slab triangle before transformation, after translation by 300 centimeters, and after perspective projection to image coordinates.','Follow the red vertex: local (0,0,0) becomes world (300,0,0) cm. The camera at (250,0,400) looks along world −Z; this illustration defines positive camera depth along that forward direction, giving camera coordinates (50,0,400). It lands 30 pixels right of the image center. The green vertex is the prose’s local (100,0,0), now world (400,0,0). The triangles are computed from these coordinates. This is an original mathematical rendering, not an Unreal screenshot or an assertion about Unreal matrix storage order.')


def actors_instances():
    s=SVG('One courtyard-kit Actor: 3 primitives, 12 placements, 12 submissions', 'Two ordinary mesh components and one instanced component share one two-section mesh asset. The instanced component has ten placements. Two passes produce eight ordinary submissions plus four instanced submissions.',660)
    s.text(260,90,'Two ordinary components',24,anchor='middle')
    s.circle(92,164,29,'#e8dfcb',GOLD);s.text(92,172,'A',24,anchor='middle')
    s.line(122,164,209,140,arrow=True);s.line(122,164,209,231,arrow=True)
    slab(s,222,146,95);slab(s,222,237,95)
    s.text(365,149,'placement 1',20);s.text(365,240,'placement 2',20)
    s.text(758,90,'One ISM component: ten placements',22,anchor='middle')
    for i in range(10):slab(s,570+(i%5)*80,153+(i//5)*86,57)
    s.lines(260,323,['2 render primitives','2 placements'],22,anchor='middle')
    s.lines(754,323,['1 render primitive','10 placements'],22,anchor='middle')
    s.line(30,384,1010,384,'#c6d0c5',2)
    s.text(31,429,'Same shared mesh: 2 sections; depth + base pass = 2 passes',23,weight='bold')
    s.text(40,482,'Ordinary components',22);s.text(552,482,'2 × 2 × 2 = 8 submissions',23,RUST)
    s.text(40,528,'Instanced group (10 placements)',22);s.text(552,528,'1 × 2 × 2 = 4 submissions',23,GREEN)
    s.lines(31,595,['Total: 12 submissions cover 12 × 2 × 2 = 48 placement-section-pass combinations.','Explicit example: all visible, conventional opaque path, preserve sections, no draw merging.'],20)
    s.save('ownership-and-draw-counts',6,'One Actor has two ordinary mesh components and one ten-instance component. Their three primitives describe twelve placements. Two sections and two passes produce eight ordinary and four instanced submissions.','Count the visible placements first: two ordinary placements plus ten instanced placements share the mesh asset. Then apply the prose’s two-section, two-pass assumptions: 8 ordinary submissions plus 4 instanced submissions, each instanced submission processing ten placements. The total is 12 submissions for 48 placement-section-pass combinations. These are the chapter’s controlled conventional-path assumptions, not an empirical Unreal draw count.')


def thread_snapshots():
    s=SVG('A transform reaches three timelines at different moments', 'Game, render, and GPU lanes show the same gate at successively newer angles. A submitted frame retains its snapshot while later updates are prepared.',590)
    times=[286,496,706,916]
    for x,n in zip(times,['t0','t1','t2','t3']):s.text(x,97,n,22,anchor='middle');s.line(x,113,x,473,'#d4dad0',1)
    for y,name in [(169,'Game state'),(306,'Render snapshot'),(443,'GPU in flight')]:
        s.text(30,y,name,22);s.line(239,y+17,985,y+17,MUTED,2,True)
    def gate(x,y,angle,label,color):
        r=math.radians(angle);s.circle(x,y,5,color);s.line(x,y,x+57*math.cos(r),y-57*math.sin(r),color,6);s.text(x+28,y+42,label,18,color,'middle')
    for x,a in zip(times,[0,30,60,90]):gate(x-26,154,a,str(a)+'°',RUST)
    for x,a in zip(times,[0,0,30,60]):gate(x-26,291,a,str(a)+'°',GREEN)
    for x,a in zip(times,[0,0,0,30]):gate(x-26,428,a,str(a)+'°',BLUE)
    s.path('M496 195 L496 213 L671 256',GREEN,'none',2,True)
    s.path('M706 332 L706 350 L881 393',GREEN,'none',2,True)
    s.lines(31,529,['An update publishes a value; it does not mutate a buffer already being read.','This timing sketch is illustrative. Unreal frame overlap varies by configuration and workload.'],21)
    s.save('thread-snapshots',3,'Three timeline lanes show the gate angle advancing first on the game side, then in the render snapshot, and later in a submitted GPU frame.','Read vertically at t2: gameplay has reached 60°, the prepared render snapshot shows 30°, and the illustrated GPU work still reads 0°. The angles visualize separate snapshots of the same object, not three gameplay gates. Publishing a change preserves the data already referenced by an in-flight frame; the exact amount of frame overlap is configuration-dependent.')


def fence_lifetime():
    s=SVG('Removal stops new references before old GPU readers finish', 'A buffer allocation remains occupied after object removal while an older frame can still read it. A completion fence makes reclamation safe; a new generation identifies later reuse.',600)
    s.text(35,90,'CPU object',22);s.text(35,197,'Render record',22);s.text(35,308,'GPU allocation',22)
    start,end=249,988
    for y in [111,219,330]:s.line(start,y,end,y,MUTED,2,True)
    s.rect(252,78,291,40,'#e6d5c9',RUST,2);s.text(396,106,'Lantern exists',21,anchor='middle')
    s.rect(252,186,389,40,'#dce8db',GREEN,2);s.text(447,214,'Record accepts draws',21,anchor='middle')
    s.rect(252,297,560,54,'#d9e5ee',BLUE,2);s.text(527,333,'Occupied: old frame still reads this range',21,anchor='middle')
    s.line(545,69,545,360,RUST,2,False,'7 5');s.text(545,392,'Remove',21,RUST,'middle')
    s.line(643,158,643,360,GREEN,2,False,'7 5');s.lines(643,430,['No new','references'],20,GREEN,'middle')
    s.line(815,260,815,360,BLUE,3);s.lines(815,392,['Fence completes','safe to reclaim'],20,BLUE,'middle')
    s.rect(828,297,155,54,'#eee5cc',GOLD,2);s.text(905,333,'Reuse: gen 8',19,anchor='middle')
    s.rect(44,433,418,83,'#f4f0e5',MUTED,1);s.lines(62,465,['Old handle = (slot 42, generation 7)','New handle = (slot 42, generation 8)'],21)
    s.text(31,565,'A removed object and a reusable byte range are not the same lifetime event.',23)
    s.save('removal-fence',4,'A removal timeline shows CPU object removal, cessation of new render references, continued GPU buffer occupancy, a completion fence, and later slot reuse with an incremented generation.','The blue allocation remains occupied after the red removal line because previously submitted GPU work may still read it. The fence, rather than the gameplay deletion time, permits physical reuse. Reusing slot 42 with generation 8 prevents an old logical handle for generation 7 from naming the new occupant. This is an illustrative lifetime contract, not a claim that Unreal uses this exact handle encoding.')


def gpu_addresses():
    s=SVG('A GPU record is bytes plus an indexing rule', 'Four 64-byte instance records occupy a contiguous buffer. Record three has three 16-byte transform rows followed by a 16-byte tint field. Arithmetic computes the exact tint address.',650)
    s.lines(31,88,['Illustrative packed instance layout: stride = 64 bytes, base address = 0x1000'],22)
    for i in range(4):
        x=50+i*242;s.rect(x,141,230,86,'#dbe7de' if i!=3 else '#e5d5bd',GREEN if i!=3 else GOLD,2)
        s.text(x+115,177,f'instance[{i}]',23,anchor='middle');s.text(x+115,210,hex(0x1000+i*64),21,anchor='middle')
    s.text(50,277,'Zoom into instance[3]',23,weight='bold')
    names=['transform row 0','transform row 1','transform row 2','tint RGBA']
    for i,n in enumerate(names):
        x=50+i*242;s.rect(x,309,230,99,['#cddfd9','#d7e3ce','#d4e1eb','#eed598'][i],MUTED,1)
        s.lines(x+115,348,[n,f'offset +{i*16} … +{i*16+15}'],20,anchor='middle')
    s.path('M891 230 L891 263 L899 300',GREEN,'none',3,True)
    s.lines(50,467,['address(tint of instance 3)','= base + instanceID × stride + fieldOffset','= 0x1000 + 3 × 64 + 48 = 0x10F0'],25,gap=39)
    s.text(31,610,'The shader and uploader must agree on stride, field offsets, alignment, and meaning.',22)
    s.save('gpu-byte-address',5,'A contiguous instance buffer is expanded into a 64-byte teaching record with three transform rows and tint; arithmetic obtains address 0x10F0 for instance 3 tint.','Follow the highlighted fourth record, then its yellow final field. A logical instance ID becomes an address only after applying the stride and field offset. The CPU packing code and GPU decoding code must share that contract. This 64-byte example is deliberately invented for arithmetic clarity; Unreal GPU Scene has its own versioned layouts and additional tables.')


def raster_pixels():
    s=SVG('Rasterization tests samples; depth selects the visible one', 'Two triangles overlap a ten-by-eight sample grid. Green and blue cells mark covered sample centers; blue wins overlap because it has a smaller depth in this explicitly forward-depth toy example.',660)
    A=[(0.6,0.8),(8.8,2.1),(3.2,7.6)];B=[(4.0,0.5),(9.3,6.6),(1.9,5.1)]
    def hit(p,tri):
        e=[]
        for i in range(3):
            a,b=tri[i],tri[(i+1)%3];e.append((b[0]-a[0])*(p[1]-a[1])-(b[1]-a[1])*(p[0]-a[0]))
        return all(v>=0 for v in e) or all(v<=0 for v in e)
    x0,y0,c=45,132,46
    for y in range(8):
        for x in range(10):
            a,b=hit((x+.5,y+.5),A),hit((x+.5,y+.5),B)
            col='#c8dce9' if b else '#d6e4ce' if a else '#f3f1e9'
            s.rect(x0+x*c,y0+y*c,c,c,col,'#b6c1b5',1);s.circle(x0+(x+.5)*c,y0+(y+.5)*c,2.5,MUTED)
    s.poly([(x0+x*c,y0+y*c) for x,y in A],'none',GREEN,3)
    s.poly([(x0+x*c,y0+y*c) for x,y in B],'none',BLUE,3)
    s.text(45,96,'Actual sample-center coverage',23,weight='bold')
    s.lines(576,143,['Triangle A: depth 0.60','Triangle B: depth 0.35','Here, smaller depth is nearer.'],23,gap=36)
    s.rect(576,289,395,122,'#f2eee3',MUTED,1)
    s.lines(595,324,['Sample (4.5, 3.5): inside A and B','0.35 < 0.60 → B wins','Shade B; A is hidden at this sample.'],21,gap=33)
    s.rect(x0+4*c,y0+3*c,c,c,'none',RUST,4)
    s.lines(32,558,['Coverage and depth are separate tests. A triangle may touch a pixel square','without containing its sample center. Unreal commonly uses reversed depth;','the printed comparison belongs only to this forward-depth illustration.'],21)
    s.save('raster-depth-winners',6,'A numerically rasterized sample grid shows two overlapping triangles, one highlighted shared sample, and the depth comparison that selects the blue triangle.','The colored cells are computed from triangle edge tests at their centers; the outlines show the continuous triangles. At sample (4.5,3.5), both cover the sample, then 0.35 wins over 0.60 in this explicitly forward-depth example. The depth convention is stated because copying that inequality into Unreal’s reversed-depth paths would be wrong.')


def alpha_volume():
    s=SVG('Two surface layers and a participating volume accumulate differently', 'Two transparent panes apply alpha compositing in order. A fog volume uses exponential transmittance along distance, with plotted attenuation and discrete segment values.',650)
    s.text(263,91,'Surface compositing',24,anchor='middle',weight='bold')
    for x,swap in [(45,False),(265,True)]:
        s.rect(x,139,197,195,'#ffffff',MUTED,1)
        colors=['#0000ff','#ff0000'] if not swap else ['#ff0000','#0000ff']
        s.data.append(f'<rect x="{x+20}" y="158" width="124" height="124" fill="{colors[0]}" fill-opacity="0.5"/>')
        s.data.append(f'<rect x="{x+66}" y="198" width="114" height="115" fill="{colors[1]}" fill-opacity="0.5"/>')
        s.text(x+99,366,'Red in front' if not swap else 'Blue in front',20,anchor='middle')
    s.lines(41,412,['Both α = 0.5, over white (1,1,1)','Red over blue: (0.75, 0.25, 0.50)','Blue over red: (0.50, 0.25, 0.75)'],20,gap=34)
    s.text(795,91,'Volume transmittance',24,anchor='middle',weight='bold')
    x0,y0,w,h=603,142,367,205
    s.line(x0,y0+h,x0+w,y0+h,MUTED,2,True);s.line(x0,y0+h,x0,y0,MUTED,2,True)
    pts=[(x0+w*i/100,y0+h*(1-math.exp(-2*i/100))) for i in range(101)]
    s.path('M'+' L'.join(f'{x:.2f} {y:.2f}' for x,y in pts),GREEN,'none',3)
    for d in [0,1,2,3,4]:
        x=x0+d*w/4;y=y0+h*(1-math.exp(-.5*d));s.circle(x,y,5,GREEN);s.text(x,y0+h+31,d,19,anchor='middle')
    for v in [0,0.5,1]:
        y=y0+h*(1-v);s.line(x0-5,y,x0,y,MUTED,1);s.text(x0-11,y+6,str(v),18,anchor='end')
    s.text(979,399,'distance (m)',18,anchor='end');s.text(577,123,'T',21)
    s.lines(594,445,['T(d) = exp(−σt d), σt = 0.5 /m','1 m: 0.607    2 m: 0.368    4 m: 0.135'],20,gap=34)
    s.lines(32,574,['Fog also adds in-scattered and emitted light along the ray; transmittance alone is only extinction.','Illustrative equations and colors, not Unreal’s complete translucency or volumetric pipeline.'],20)
    s.save('alpha-and-volume',7,'Red and blue half-transparent panes are composited in both orders over white, yielding different overlap colors. A computed fog transmittance curve falls from one toward zero as distance increases.','Read the overlap patches: red over blue gives (0.75,0.25,0.50), while swapping their order gives (0.50,0.25,0.75), using the shown unit-RGB compositing arithmetic over white. A pane is a surface event with an order. Fog instead attenuates continuously along distance: the plotted curve is T(d)=exp(−0.5d). Full volume rendering additionally integrates light scattered and emitted into the ray. Displayed colors illustrate the arithmetic; this is not a complete linear-light display-management example.')


def gate_attachment():
    s=SVG('The lamp follows the door because its offset is transformed too', 'A top-down gate rotates ninety degrees around a hinge at world 300,200 centimeters. The door endpoint moves from 400,200 to 300,300. A lamp sixty centimeters from the hinge moves from 360,200 to 300,260.',620)
    ox,oy,scale=112,450,2.25
    def xy(x,y):return ox+(x-260)*scale,oy-(y-170)*scale
    for x in range(260,441,20):
        a,b=xy(x,170),xy(x,320);s.line(*a,*b,'#dce1d5',1)
    for y in range(180,321,20):
        a,b=xy(260,y),xy(440,y);s.line(*a,*b,'#dce1d5',1)
    H=xy(300,200);E0=xy(400,200);E1=xy(300,300);L0=xy(360,200);L1=xy(300,260)
    s.line(*H,*E0,RUST,10);s.line(*H,*E1,GREEN,10);s.circle(*H,10,INK)
    s.circle(*L0,13,'#edcf83',GOLD,2);s.circle(*L1,13,'#edcf83',GOLD,2)
    s.path(f'M{H[0]+130} {H[1]} A130 130 0 0 0 {H[0]} {H[1]-130}',GREEN,'none',2,True)
    s.text(H[0]+80,H[1]-88,'90°',23,GREEN)
    s.text(E0[0],E0[1]+35,'(400,200)',20,RUST,'middle')
    s.text(E1[0],E1[1]-22,'(300,300)',20,GREEN,'middle')
    s.text(H[0]-20,H[1]+39,'hinge (300,200)',20,INK,'middle')
    s.lines(593,139,['Door endpoint, local (100,0):','Rz(90°) × (100,0) = (0,100)','world = (300,200) + (0,100)','= (300,300) cm'],22,gap=34)
    s.lines(593,337,['Lamp offset, local (60,0):','Rz(90°) × (60,0) = (0,60)','world lamp = (300,260) cm'],22,gap=34)
    s.lines(32,551,['Top-down illustration: all shown Z coordinates are zero.','The GPU can read resolved world transforms; it need not traverse this attachment chain.'],21)
    s.save('gate-attachment',2,'A top-down gate swings from zero to ninety degrees about a hinge at 300,200 centimeters. Its lamp is marked at a sixty-centimeter local offset before and after rotation.','The brown door becomes the green door while the hinge stays fixed. Both yellow lamp positions are computed from the same parent rotation applied to the lamp’s 60-centimeter local offset. The lamp shares motion ancestry with the door; it does not have to share its mesh asset, material, or draw submission.')


def weighted_skinning():
    s=SVG('One sleeve vertex keeps its weights while its bone results move', 'A small triangulated strip is skinned with two bone transforms. For the tracked vertex, weights are 0.75 and 0.25. The current bone results 2,0 and 2,4 produce 2,1; the previous second-bone result 2,2 produced 2,0.5.',670)
    def weight(x):return min(1,max(0,x/8))
    def deform(x,y,shift):return x,y+weight(x)*shift
    for col,(shift,label) in enumerate([(0,'Rest strip'),(2,'Previous pose'),(4,'Current pose')]):
        x0=48+col*339;y0=338;scale=60
        s.text(x0+128,97,label,23,anchor='middle',weight='bold')
        for i in range(4):
            points=[(i,-.25),(i+1,-.25),(i+1,.25),(i,.25)]
            screen=[(x0+scale*deform(x,y,shift)[0],y0-scale*deform(x,y,shift)[1]) for x,y in points]
            s.poly([screen[j] for j in [0,1,2]],'#d6e4dc',GREEN,1);s.poly([screen[j] for j in [0,2,3]],'#d1dfe7',BLUE,1)
        p=deform(2,0,shift);s.circle(x0+scale*p[0],y0-scale*p[1],8,RUST)
        s.text(x0+128,397,'v = (2, '+str(p[1])+')',21,RUST,'middle')
        s.line(x0,y0+28,x0+263,y0+28,MUTED,1,True)
    s.line(45,430,995,430,'#ccd5c9',2)
    s.lines(44,475,['Current: 0.75 × (2,0) + 0.25 × (2,4) = (2,1)','Previous: 0.75 × (2,0) + 0.25 × (2,2) = (2,0.5)','Local motion of tracked vertex: (0,0.5) before component and camera transforms'],22,gap=37)
    s.lines(32,614,['Original computed skinning example; translations make the weighted arithmetic visible.','Weights vary along the strip. Current and previous shape share vertex identity and connectivity.'],20)
    s.save('weighted-skinning',8,'Three computed triangulated sleeve strips show rest, previous, and current deformation. The tracked red vertex moves from 2,0 to 2,0.5 to 2,1 under the displayed two-bone weighted calculation.','The red vertex is the prose’s 0.75/0.25 example. Its two current transformed positions are (2,0) and (2,4), so the result is (2,1); the previous pose instead used (2,2) for the second result, producing (2,0.5). The strip is actually computed with weights varying along its length. This is an illustrative software deformation using simple bone translations, not an Unreal character capture. Its retained previous result is the information motion reconstruction needs.')


def deformation_bounds():
    s=SVG('An unchanged rest bound can reject a visible moving hand', 'A character stands outside the illustrated view region, but the animated arm reaches into it. The small rest bound misses the view; an updated bound intersects the view and preserves visibility.',620)
    s.rect(30,118,968,336,'#f4f0e5',MUTED,1)
    s.rect(568,119,429,334,'#dce9df',GREEN,2);s.text(784,151,'Visible region of this view',22,GREEN,'middle')
    s.rect(237,200,186,235,'none',RUST,3,dash='8 6');s.text(250,187,'stale rest bound',21,RUST)
    s.circle(329,239,27,'#c7d9e4',BLUE,2)
    s.path('M329 268 L329 359 M329 292 L277 338 M329 294 L418 255 L642 226 M329 357 L291 426 M329 357 L364 426',BLUE,'none',17)
    s.circle(644,226,14,'#d9af83',RUST,2)
    s.rect(235,198,435,239,'none',GREEN,4);s.text(596,418,'current bound',20,GREEN,'middle')
    s.line(568,108,568,471,GREEN,3)
    s.lines(37,508,['Old bound: no overlap with the view → object incorrectly discarded.','Updated bound: overlaps the view → animated hand can be drawn.','The bound must enclose the actual pose, including material displacement.'],22,gap=33)
    s.save('deformation-bounds',8,'A stick figure outside the camera region extends a hand into it. A stale red dashed bound excludes the hand and fails the visibility test; a larger green current-pose bound intersects the region.','The red dashed rest bound lies entirely left of the view boundary, so culling it is internally consistent but visually wrong: the hand has moved into the image. The current green bound includes the hand and survives. A larger correct bound may cost more culling work; a smaller incorrect bound loses visible geometry.')


def png_rgb(width,height,data):
    def chunk(kind,value):return struct.pack('!I',len(value))+kind+value+struct.pack('!I',zlib.crc32(kind+value)&0xffffffff)
    rows=b''.join(b'\x00'+bytes(data[y*width*3:(y+1)*width*3]) for y in range(height))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('!IIBBBBB',width,height,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(rows,8))+chunk(b'IEND',b'')


def dot(a,b):return sum(x*y for x,y in zip(a,b))
def add(a,b):return tuple(x+y for x,y in zip(a,b))
def sub(a,b):return tuple(x-y for x,y in zip(a,b))
def mul(a,k):return tuple(x*k for x in a)
def unit(a):return mul(a,1/math.sqrt(dot(a,a)))
def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def rgb(v,gamma=False):return [round(255*max(0,min(1,x))**(1/2.2 if gamma else 1)) for x in v]


def material_response():
    s=SVG('Hold geometry and color fixed; change only the surface response', 'Three software-rendered spheres use identical shape, color, view and light. Increasing the illustrative roughness parameter broadens and weakens the specular lobe. A graph shows the corresponding angular lobe shapes.',650)
    light=unit((-.4,.6,.85));half=unit(add(light,(0,0,1)));roughness=[.15,.45,.85]
    for col,r in enumerate(roughness):
        w=h=220;pixels=[];exponent=max(.3,2/(r*r)-2)
        for py in range(h):
            for px in range(w):
                x=(px+.5-w/2)/(w*.43);y=-(py+.5-h/2)/(h*.43)
                if x*x+y*y>1:pixels.extend([252,250,245]);continue
                normal=(x,y,math.sqrt(1-x*x-y*y));diff=.10+.58*max(0,dot(normal,light));spec=.85*(1-r)*max(0,dot(normal,half))**exponent
                pixels.extend(rgb(tuple(v*diff+spec for v in (.34,.48,.31)),True))
        png=png_rgb(w,h,pixels);(OUT/f'material-roughness-{str(r).replace(".","")}.png').write_bytes(png)
        s.image(38+col*343,105,280,280,png);s.text(178+col*343,408,f'roughness control = {r}',21,anchor='middle')
    s.lines(32,464,['All three renders use the same sphere, base color, light direction, and camera.','Only the width/intensity control of this illustrative specular term changes.'],22)
    s.lines(32,552,['Software teaching model: exponent = max(0.3, 2 / r² − 2), not Unreal’s BRDF.','A normal map changes the shading normal; a roughness map changes the response around it.'],20)
    s.text(31,621,'Material parameters describe a response to illumination; they do not contain the light itself.',22)
    s.save('material-response',9,'Three original software-rendered spheres show the same green material and lighting with narrow, medium, and broad specular responses as one roughness-like parameter changes.','Compare the highlight, not the sphere silhouette: the geometry, green base color, and illumination are identical. The pixels are computed by the included software renderer. Its simple exponent model is printed to make the demonstration reproducible; it is not Unreal’s microfacet BRDF or a measured comparison of Unreal roughness settings. The visual isolates the meaning of changing a surface response without moving a light or rebuilding a mesh.')


def scene_objects():
    # Coordinates are meters with Z upward. This is a small software example, not an Unreal scene export.
    def box(name,mn,mx,color,rough,yaw=0,pivot=(0,0,0)):
        return dict(kind='box',name=name,mn=mn,mx=mx,color=color,rough=rough,yaw=yaw,pivot=pivot)
    return [box('paving',(-3,-2,-.18),(3,3,0),(.48,.50,.43),.85),
            box('left wall',(-3,2.5,0),(-.7,2.7,2.5),(.63,.48,.36),.9),
            box('right wall',(.7,2.5,0),(3,2.7,2.5),(.63,.48,.36),.9),
            box('lintel',(-.7,2.5,1.9),(.7,2.7,2.5),(.63,.48,.36),.9),
            box('gate',(-.65,2.25,.05),(.65,2.34,1.85),(.24,.34,.22),.35,math.radians(-28),(-.65,2.25,0)),
            box('crate',(-1.9,-.3,0),(-1.05,.55,.85),(.43,.23,.11),.55),
            box('character body',(1.03,.04,.52),(1.40,.37,1.28),(.16,.29,.46),.65),
            box('left leg',(1.04,.08,0),(1.18,.28,.57),(.12,.18,.27),.6),
            box('right leg',(1.28,.08,0),(1.41,.28,.57),(.12,.18,.27),.6),
            dict(kind='sphere',name='character head',center=(1.21,.2,1.49),radius=.23,color=(.69,.42,.26),rough=.7),
            dict(kind='sphere',name='lamp',center=(.92,2.10,2.10),radius=.105,color=(1,.66,.17),rough=.15,emission=True)]


def object_hit(origin,direction,obj):
    if obj['kind']=='sphere':
        oc=sub(origin,obj['center']);b=dot(oc,direction);c=dot(oc,oc)-obj['radius']**2;disc=b*b-c
        if disc<0:return None
        t=-b-math.sqrt(disc)
        if t<.0001:t=-b+math.sqrt(disc)
        if t<.0001:return None
        p=add(origin,mul(direction,t));return t,p,unit(sub(p,obj['center']))
    a=obj['yaw'];ca,sa=math.cos(a),math.sin(a);pivot=obj['pivot']
    o=sub(origin,pivot);o=(ca*o[0]+sa*o[1]+pivot[0],-sa*o[0]+ca*o[1]+pivot[1],o[2]+pivot[2])
    d=(ca*direction[0]+sa*direction[1],-sa*direction[0]+ca*direction[1],direction[2])
    near,far=-1e30,1e30;normal=(0,0,1)
    for axis in range(3):
        lo,hi=obj['mn'][axis],obj['mx'][axis]
        if abs(d[axis])<1e-12:
            if o[axis]<lo or o[axis]>hi:return None
            continue
        t0,t1=(lo-o[axis])/d[axis],(hi-o[axis])/d[axis];sign=-1
        if t0>t1:t0,t1=t1,t0;sign=1
        if t0>near:near=t0;normal=tuple(sign if j==axis else 0 for j in range(3))
        far=min(far,t1)
        if near>far:return None
    if far<.0001 or near<.0001:return None
    n=(ca*normal[0]-sa*normal[1],sa*normal[0]+ca*normal[1],normal[2]);return near,add(origin,mul(direction,near)),n


def trace(origin,direction,objects,max_t=1e30):
    best=None
    for obj in objects:
        hit=object_hit(origin,direction,obj)
        if hit and hit[0]<max_t:max_t=hit[0];best=(*hit,obj)
    return best


def courtyard_render(width=480,height=360):
    objects=scene_objects();camera=(5.0,-7.0,4.8);target=(0,.65,1.0)
    forward=unit(sub(target,camera));right=unit(cross(forward,(0,0,1)));up=cross(right,forward)
    scale=math.tan(math.radians(43)/2);light=(.92,1.92,2.10);sun=unit((-2,-3,5))
    buffers={name:bytearray() for name in ['albedo','normals','depth','roughness','shadow','lit']}
    for y in range(height):
        for x in range(width):
            sx=(2*(x+.5)/width-1)*width/height*scale;sy=(1-2*(y+.5)/height)*scale
            ray=unit(add(forward,add(mul(right,sx),mul(up,sy))));hit=trace(camera,ray,objects)
            if not hit:
                for name in buffers:buffers[name].extend([223,231,229] if name in ['lit','albedo'] else [24,31,31])
                continue
            t,p,n,obj=hit;color=obj['color']
            if obj['name']=='paving':
                checker=(math.floor(p[0]/.5)+math.floor(p[1]/.5))%2;color=mul(color,.88 if checker else 1.06)
                edge=min(p[0]/.5%1,p[1]/.5%1,1-p[0]/.5%1,1-p[1]/.5%1)
                if edge<.025:color=mul(color,.6)
            ldelta=sub(light,p);dist=math.sqrt(dot(ldelta,ldelta));ldir=mul(ldelta,1/max(.001,dist));start=add(p,mul(n,.002))
            shadow=0 if trace(start,ldir,objects,max(.001,dist-.12)) else 1
            sundot=max(0,dot(n,sun));sunshadow=0 if trace(start,sun,objects,20) else 1
            shade=.14+.65*sundot*sunshadow+1.2*max(0,dot(n,ldir))*shadow/(1+.12*dist*dist)
            h=unit(add(ldir,mul(ray,-1)));spec=.10*(1-obj['rough'])*max(0,dot(n,h))**max(1,2/obj['rough']**2-2)*shadow
            lit=tuple(v*shade+spec for v in color)
            if obj.get('emission'):lit=(1,.79,.32)
            values={'albedo':rgb(color,True),'normals':rgb(tuple((v+1)/2 for v in n)),
                    'depth':rgb((max(0,1-t/15),)*3),'roughness':rgb((obj['rough'],)*3),
                    'shadow':rgb((.1+.9*shadow,)*3),'lit':rgb(lit,True)}
            for name in buffers:buffers[name].extend(values[name])
    return {name:png_rgb(width,height,data) for name,data in buffers.items()}


def pixel_pipeline():
    s=SVG('Follow one slab pixel from triangle to logical GBuffer', 'The exact slab triangle from chapter one is projected to 190,120; 250,120; and 250,60. Pixel center 230.5,100.5 has barycentric weights 0.325,0.35,0.325. Its interpolated position and UV feed a depth comparison and material sample, producing explicit logical GBuffer values.',980)
    s.text(30,77,'One continuous numeric example; lengths in cm, image coordinates in pixels.',21)
    vertices=[(190,120),(250,120),(250,60)]
    point=(230.5,100.5)
    weights=(.325,.35,.325)
    # Every vertex has the same depth, so perspective-correct and affine weights coincide.
    assert abs(sum(weights)-1)<1e-12
    assert all(abs(sum(weights[i]*vertices[i][j] for i in range(3))-point[j])<1e-12 for j in range(2))
    loc=(100*(weights[1]+weights[2]),100*weights[2],0)
    world=(loc[0]+300,loc[1],0)
    camera=(world[0]-250,world[1],400)
    assert loc==(67.5,32.5,0) and world==(367.5,32.5,0) and camera==(117.5,32.5,400)
    assert (160+240*camera[0]/camera[2],120-240*camera[1]/camera[2])==point
    project=lambda p:(90+(p[0]-190)*4.4,130+(p[1]-60)*4.4)
    pts=[project(p) for p in vertices]
    s.poly(pts,'#e1eadf',GREEN,2)
    for k in range(1,6):
        yy=130+k*44;xx=90+k*44
        s.line(354-k*44,yy,354,yy,'#bccabb',1)
        s.line(xx,394-k*44,xx,394,'#bccabb',1)
    for i,(x,y) in enumerate(pts):s.circle(x,y,7,VERT[i])
    s.text(52,424,'v0 (190,120)',20,VERT[0]);s.text(304,424,'v1 (250,120)',20,VERT[1])
    s.text(286,111,'v2 (250,60)',20,VERT[2])
    px,py=project(point);s.circle(px,py,6,INK)
    s.line(px-14,py,px+14,py,INK,1);s.line(px,py-14,px,py+14,INK,1)
    s.text(119,290,'P',24,weight='bold');s.line(142,287,px-11,py-4,INK,1)
    s.text(75,464,'P = (230.5,100.5), enlarged view',20)
    s.text(529,122,'Barycentric interpolation',24,weight='bold')
    for y,i,weight,position in [(163,0,'.325','(0,0,0)'),(201,1,'.350','(100,0,0)'),(239,2,'.325','(100,100,0)')]:
        s.circle(540,y-7,6,VERT[i]);s.text(555,y,f'v{i}: {weight} × {position}',22,VERT[i])
    s.text(529,288,'Local = (67.5,32.5,0)',22,weight='bold')
    s.text(529,323,'World = (367.5,32.5,0)',22)
    s.text(529,358,'Camera = (117.5,32.5,400)',22)
    s.text(529,389,'400 is positive forward depth.',20)
    s.text(529,429,'UV = (.675,.325)',22,BLUE)
    s.text(529,462,'World normal = (0,0,1) at all vertices',20)
    s.line(30,491,1010,491,'#c5cec0',2)
    s.text(30,532,'The slab wins the depth comparison',23,weight='bold')
    s.text(30,569,'Compare positive view depth in cm:',20)
    s.line(72,642,441,642,MUTED,2,True)
    s.line(72,642,272,642,GREEN,7)
    s.circle(72,642,9,BLUE);s.text(46,610,'Camera',19,BLUE)
    s.line(272,597,272,688,GREEN,6)
    s.line(372,597,372,688,RUST,6)
    s.text(272,721,'400 cm',21,GREEN,'middle');s.text(372,721,'600 cm',21,RUST,'middle')
    s.text(272,758,'slab',20,GREEN,'middle');s.text(372,758,'behind it',20,RUST,'middle')
    s.lines(30,812,['400 < 600: keep the slab’s sample.','This uses view depth, not hardware Z.','Depth-test encoding can differ by renderer.'],20,gap=30)
    s.text(529,532,'Sample material, then write surface data',22,weight='bold')
    s.lines(529,573,['Texture at UV: (.4,.3,.2)','Tint: (.8,1,.8)','Multiply → (.32,.30,.16)'],21,gap=31)
    s.rect(529,675,80,80,'#99956f','none',0)
    s.lines(631,700,['Base color patch','sRGB encoded for display'],19,gap=27)
    s.text(529,799,'Logical GBuffer fields',22,weight='bold')
    s.lines(529,835,['BaseColor = (.32,.30,.16)','WorldNormal = (0,0,1); Roughness = .8','Illustrative slab: Metallic = 0'],20,gap=30)
    s.lines(30,936,['All triangle vertices share 400 cm depth, so perspective-correct weights equal screen weights.'],20)
    s.save('one-pixel-through-the-pipeline',10,'An enlarged projected triangle marks pixel center 230.5,100.5. Three colored barycentric weights interpolate local and world positions, UV and normal. The slab at 400 cm defeats a competing surface at 600 cm; its sampled and tinted material writes base color 0.32,0.30,0.16, world normal 0,0,1 and roughness 0.8.','Return to <a href="#a-triangle-becomes-an-object">the original 100 cm slab and camera</a>: the same vertices now produce one complete surface sample. The chosen pixel is separate from the earlier small raster-grid exercise. Since all three vertices have the same camera depth, the displayed barycentric weights also give the perspective-correct attributes. Positive view depth selects the nearer sample; this does not describe a hardware depth-buffer encoding. The base-color arithmetic uses linear values, while the color swatch is encoded for display. These are illustrative logical GBuffer fields, before lighting, rather than Unreal’s packed texture layout; depth is a separate visibility result.')


def gbuffer():
    panels=courtyard_render()
    for name,png in panels.items():(OUT/f'courtyard-{name}.png').write_bytes(png)
    s=SVG('One courtyard, six views of the same visible samples', 'An original software render produces albedo, world normal, ray depth, roughness, lamp visibility and final direct-lit images for the same camera and geometry.',795)
    specs=[('albedo','Base color','material color, without lighting'),('normals','World normal','RGB = (normal + 1) / 2'),('depth','Ray depth','white is near; d maps over 15 m'),('roughness','Roughness','white is rough; dark is smooth'),('shadow','Lamp visibility','white = visible; dark = blocked'),('lit','Lit result','surface + direct light + shadows')]
    for i,(name,title,subtitle) in enumerate(specs):
        x=29+(i%3)*343;y=96+(i//3)*309
        s.text(x+154,y,title,24,anchor='middle',weight='bold');s.image(x,y+15,309,232,panels[name]);s.text(x+154,y+278,subtitle,18,anchor='middle')
    s.lines(31,747,['Original software render: paving, gate, walls, crate, a block character, and a small lamp.','Logical teaching channels, not Unreal’s packed GBuffer or a capture from Unreal Editor.'],20)
    s.save('courtyard-gbuffer',10,'Six computed images of the same courtyard show base color, world normals, ray depth, roughness, lamp visibility, and the final direct-lit result.','Choose a surface and locate it in all six panels. The checker pattern belongs to the paving’s base color; its upward normal has a different encoding; its shadow can change while those properties remain fixed. All panels use the same ray intersections. This original software renderer uses simplified direct lighting and a constant ambient term, with no GI; the panels illustrate logical values rather than Unreal’s physical GBuffer packing. The separate <a href="assets/walkthrough/courtyard-lit.png">lit frame</a> is also available.')


def shadow_view():
    s=SVG('The gate can leave the camera while its shadow remains visible', 'A top-down plan shows the camera frustum, a visible paving sample, a lamp, and a gate outside the camera frustum. The lamp-to-sample segment crosses the gate, so the sample is shadowed.',630)
    camera=(149,407);P=(623,306);lamp=(775,107)
    s.poly([camera,(885,255),(885,474)],'#e3eee4',GREEN,2)
    s.text(717,466,'Camera-visible region',21,GREEN,'middle')
    s.circle(*camera,15,BLUE);s.text(80,448,'Camera',22,BLUE)
    for i in range(5):slab(s,405+i*104,364,73,21)
    s.circle(*P,9,RUST);s.text(595,287,'P: visible slab',22,RUST,'middle')
    s.circle(*lamp,19,'#eacf78',GOLD);s.text(777,76,'Lamp',23,GOLD,'middle')
    # The gate lies above the top edge of the camera frustum, yet crosses the lamp ray.
    s.line(653,201,752,201,RUST,13);s.text(899,202,'Off-camera gate',21,RUST,'middle')
    s.line(*lamp,*P,GOLD,3);s.line(*camera,*P,BLUE,2,True)
    s.circle(703,201,10,'none',RUST,3)
    s.text(829,245,'Light ray blocked',20,RUST,'middle')
    s.lines(32,525,['Camera visibility: P is visible; gate is outside this camera region.','Light visibility: the segment from lamp to P crosses the gate → P is in shadow.','The persistent scene must retain the gate for the light’s query.'],22,gap=32)
    s.save('off-camera-shadow',10,'A plan-view camera cone includes a paving sample but excludes the gate above it. The lamp-to-sample ray intersects that off-camera gate, creating a shadow on the visible sample.','Trace the blue camera ray to P, then the gold lamp ray to the same P. Only the gold segment crosses the gate. Culling the gate from the main camera’s work is valid; deleting it from the persistent scene because of that culling would incorrectly remove the shadow. The drawing is a geometric example, not a measured Unreal shadow-map view.')


def ray_cache():
    s=SVG('A geometric hit identifies a place; the cache must still cover it', 'A ray hits a triangular wall at coordinates one,one,zero. A card projection maps the hit into quarter,quarter of a four-by-four capture region. A highlighted atlas texel supplies a lighting lookup independently of the triangle intersection.',650)
    pts=[(63,382),(410,382),(63,121)];s.poly(pts,'#dcd4bf',GOLD,2)
    p=(63+347/4,382-261/4);s.circle(*p,9,RUST);s.line(353,148,*p,BLUE,3,True)
    s.text(328,121,'incoming ray',22,BLUE,'middle');s.text(187,423,'hit P = (1,1,0)',22,RUST,'middle')
    s.lines(35,477,['Intersection reports P and surface identity.','It does not report the final light value.'],21)
    x0,y0,size=605,129,55
    for y in range(5):
        for x in range(6):
            col=['#c9bd9c','#a8b6a0','#e5d7b6','#9eb8a6'][(x+2*y)%4]
            if x==5 and y in [1,2,3]:col='#db39c8'
            s.rect(x0+x*size,y0+y*size,size,size,col,PAPER,2)
    s.rect(x0+size,y0+size,3*size,3*size,'none',GREEN,4)
    s.circle(x0+size+3*size*.25,y0+size+3*size*.25,9,RUST)
    s.text(770,99,'Paged capture atlas',24,anchor='middle',weight='bold')
    s.lines(591,450,['Card bounds: x,y in [0,4]','Card coordinate = (1/4, 1/4)','Map to resident atlas page; sample lighting.'],20,gap=31)
    s.path('M415 321 L512 321 L512 225 L633 225',GREEN,'none',3,True)
    s.lines(32,582,['Pink cells illustrate missing cache coverage: a ray can hit geometry and still lack good cached data.','Card coordinates come from capture projection; they are not automatically the material’s UVs.'],20)
    s.save('intersection-versus-cache',11,'A ray hits a triangle at 1,1,0; a separately parameterized four-unit card maps that point to 0.25,0.25 and then to a resident atlas region, while pink cells mark absent coverage.','The red point is a geometric hit. The green outlined atlas region is a separate parameterization used to retrieve cached information. A correct intersection does not guarantee that the selected card covers that surface with valid lighting. The printed card projection is illustrative; it deliberately distinguishes capture coordinates from the material’s authored UV coordinates.')


def point_provider():
    s=SVG('One point edit exposes the provider’s real dependency contract', 'A scanned point changes position while its color remains fixed. The raster point buffer and conservative bounds advance to revision eight, an existing reconstructed triangle product becomes stale, and no ray product exists to update. Missing normal and UV attributes require a declared material fallback.',710)
    for x,y,c in [(71,169,VERT[0]),(119,153,VERT[1]),(160,206,VERT[2]),(200,180,VERT[3]),(83,254,VERT[2]),(135,298,VERT[0]),(238,269,VERT[1]),(284,228,VERT[3])]:s.circle(x,y,9,c)
    s.circle(185,311,9,RUST);s.circle(307,325,11,RUST);s.line(197,312,292,323,RUST,3,True)
    s.rect(51,134,214,194,'none',RUST,2,dash='6 5');s.rect(45,128,281,217,'none',GREEN,3)
    s.text(156,99,'Scanned samples',24,anchor='middle',weight='bold')
    s.lines(45,395,['Point 17: (2,1,0) → (3,1,0)','Stored: position + color','Missing: normal, tangent, UV'],21,gap=34)
    s.text(658,100,'Selected material asks for UV + tangent',22,anchor='middle')
    s.lines(435,153,['Those attributes cannot be fetched from this record.','Declared fallback: display the supplied point color.','Do not silently claim a normal-mapped mesh exists.'],20,gap=34)
    s.line(432,255,984,255,'#c6d0c3',2)
    for i,(name,status,col) in enumerate([('Raster buffer','revision 8',GREEN),('Conservative bounds','revision 8',GREEN),('Derived triangles, if built','revision 7 → stale',RUST),('Ray representation','absent; no update invented',MUTED)]):
        y=305+i*49;s.text(430,y,name,20);s.text(736,y,status,20,col);s.line(431,y+13,995,y+13,'#d6dccf',1)
    s.lines(31,566,['GPU frame N keeps the revision-7 allocation alive until its completion fence.','New draws can publish revision 8 without rewriting memory still being read.','A color-only edit can invalidate less work if no derived product depends on color.'],21,gap=34)
    s.save('point-provider-revisions',12,'A point cloud sample moves beyond its old bound. A revision table advances the point buffer and bounds, marks optional derived triangles stale, and leaves an absent ray product absent; missing UV and tangent data force an explicit material fallback.','The red point moves, so both the point positions and the enclosing bound change. The right-hand entries distinguish an existing stale derived product from a representation that never existed. A material requiring UVs and a tangent basis cannot be satisfied by position-and-color records alone; a declared point-color fallback is honest and testable. The earlier lifetime rule still applies to the old allocation used by an in-flight frame.')


def main():
    indexed_slab();transforms();actors_instances();thread_snapshots();fence_lifetime();gpu_addresses();raster_pixels();alpha_volume()
    gate_attachment();weighted_skinning();deformation_bounds();material_response();pixel_pipeline();gbuffer();shadow_view();ray_cache();point_provider()
    provenance=ROOT/'Docs/Unreal/assets/reference/image-provenance.json'
    records=json.loads(provenance.read_text(encoding='utf-8'))
    for record in records:
        if record['id']=='lumen-surface-cache':record.update(width=1200,height=926)
    provenance.write_text(json.dumps(records,indent=2)+'\n',encoding='utf-8')
    print(f'Wrote {len(FIGURES)} scene walkthrough figures.')


if __name__ == '__main__':
    main()
