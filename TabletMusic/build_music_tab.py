"""Build the left tablet tab as a music remote; never edits game files.

Produces the same five-resource patch shape as the Doom builder and occupies the
same free navigation slot, so the two are alternatives: build whichever tab you
want installed. Every dynamic label reserves capacity for the longest string the
native runtime can write into it.
"""
import hashlib,json,struct,sys
from pathlib import Path
import zstandard
HERE=Path(__file__).resolve().parent
TABLET=HERE.parent/'EchoVr-Tablet-Probing'
sys.path.insert(0,str(TABLET))
import echovr_pkg as P
from echovr_patch import ManifestFile
from canvas_edit import Canvas,descriptor,u64
from build_tools_tab import empty_canvas,ROOT_CANVAS,NAV_CANVAS,LEVEL,ACTOR,PAGE

OUT=HERE/'build'
SOURCE=TABLET/'build/tools_tab'
MID='48037dc70b0ecab2'
MUSIC=P.sym('echovr_music_arm_page_v1')
BUTTON=P.sym('music_tab_button_v1')

# Page is 942 x 528 and sits at (28,112) inside the 1024 x 768 root canvas.
# A finer bar reads as continuous and ticks every few seconds on a normal track.
BAR_COUNT=60
BAR_LEFT,BAR_RIGHT,BAR_TOP,BAR_BOTTOM=24,918,220,238
VOL_COUNT=12
VOL_LEFT,VOL_RIGHT,VOL_TOP,VOL_BOTTOM=286,656,464,492
TITLE_CAP,ARTIST_CAP,LINE_CAP=128,128,64
# Longest literal the runtime writes, so the reserved capacity is auditable here.
TITLE_SEED='NOTHING PLAYING'

# The tablet's own title bar already reads MUSIC, so the page carries no heading
# of its own; the source chip takes the top-left instead.
CHIP_RECT=(24,10,330,76)
# (key, label, page-local rect, theme colour role, font size)
CONTROLS=[('prev',   'PREV', (24,292,286,428),  'transport',30),
          ('play',   'PLAY', (306,292,636,428), 'primary',  40),
          ('next',   'NEXT', (656,292,918,428), 'transport',30),
          ('voldown','VOL -',(24,442,262,514),  'volume',   26),
          ('volup',  'VOL +',(680,442,918,514), 'volume',   26)]

# Each bar segment is its own panel, so a gradient across them is free.
THEMES={
 'neon':   dict(background=(.030,.020,.060), chip=(.180,.120,.320),
                transport=(.130,.100,.240), primary=(.550,.150,.750),
                volume=(.110,.085,.200),    track=(.095,.075,.155),
                dim=(.720,.680,.860),
                bar=((.130,.850,.950),(.950,.250,.700)),
                vol=((.300,.550,.950),(.780,.350,.950))),
 'sunset': dict(background=(.060,.028,.045), chip=(.290,.130,.160),
                transport=(.220,.100,.140), primary=(.900,.350,.200),
                volume=(.180,.085,.115),    track=(.135,.065,.090),
                dim=(.870,.740,.720),
                bar=((1.00,.780,.250),(.950,.250,.450)),
                vol=((1.00,.700,.300),(.950,.350,.350))),
 'echo':   dict(background=(.018,.055,.075), chip=(.080,.280,.340),
                transport=(.060,.215,.275), primary=(.095,.620,.700),
                volume=(.050,.170,.220),    track=(.038,.125,.160),
                dim=(.640,.830,.870),
                bar=((.200,.950,.850),(.350,.650,1.00)),
                vol=((.250,.900,.700),(.300,.700,.950))),
}

def mix(a,b,t):
    """Linear blend, returned as the RGBA the canvas stores at +0x78."""
    return tuple(a[i]+(b[i]-a[i])*t for i in range(3))+(1.,)

def build(theme='neon',patch_out=True):
    T=THEMES[theme]
    tools=ManifestFile((SOURCE/'manifests'/MID).read_bytes())
    base=ManifestFile((TABLET/'backups/20260914T235837383130Z/manifest.before').read_bytes())
    def resource(t,n):
        a=tools.A[tools.index[t,n]];pkg,offset,size,_=tools.C[a[2]&0xffffffff]
        frame=(SOURCE/'packages'/f'{MID}_{pkg}').read_bytes()[offset:offset+size]
        return zstandard.ZstdDecompressor().decompress(frame)[a[2]>>32:][:a[3]]
    cv=P.typesym('CUICanvasResource');bt=P.typesym('CR15ButtonInteractCR')
    root=Canvas(resource(cv,ROOT_CANVAS));nav=Canvas(resource(cv,NAV_CANVAS));donor=Canvas(resource(cv,PAGE))
    assert len(root.elements)==10 and len(nav.elements)==10
    old_root=[bytes(r) for r in root.elements];old_nav=[bytes(r) for r in nav.elements]
    # Wider than the '?' slot so a five-letter label fits, still clear of the
    # leftmost stock icon (sprite at x=160.3, hit box at x=170).
    tabrect=(18,662,142,751)
    nav.label(donor.elements[1],'music_tab_label_v1','MUSIC',tabrect,26)

    page=empty_canvas(donor.serialize(),942,528)
    struct.pack_into('<Q',page.header,0,MUSIC)
    def panel(name,rect,color):
        i=page.append(donor.elements[0],name,rect)
        struct.pack_into('<4f',page.elements[i],0x78,*color)
        return i
    def label(name,text,rect,size,capacity=0,color=None,anchor=.5):
        i=page.label(donor.elements[1],name,text,rect,size,capacity=capacity)
        if color: struct.pack_into('<4f',page.elements[i],0x78,*color)
        # Text anchor fractions, X then Y, matching the anchor pair at +0x24.
        # The donor inherits (0.0, 0.5) = left, vertically centred -- which is why
        # the stock Tools page insets its labels 13px horizontally but matches its
        # panel rect exactly in Y. Labels sharing a control's rect must centre;
        # the times flush to the ends of the progress bar instead.
        struct.pack_into('<f',page.elements[i],0x9c,anchor)
        return i
    def meter(prefix,left,right,top,bottom,count,ramp,gap=2,hidden=True):
        """Segments shaded along the theme's ramp; the runtime shows/hides them."""
        first=len(page.elements)
        pitch=(right-left)/count
        for i in range(count):
            x=left+i*pitch
            index=panel(f'{prefix}_{i:02}_v1',(x,top,x+pitch-gap,bottom),
                        mix(ramp[0],ramp[1],i/max(1,count-1)))
            if hidden: struct.pack_into('<I',page.elements[index],0x10,1)
        return first

    # Element indices are captured as they are authored, never hand-counted: the
    # native runtime addresses these by index through the generated header.
    at={}
    panel('music_page_background_v1',(0,0,942,528),T['background']+(1.,))
    # Always-on accent strip across the top edge, the same ramp as the bar.
    meter('music_accent',0,942,0,7,48,T['bar'],gap=0,hidden=False)
    panel('music_source_background_v1',CHIP_RECT,T['chip']+(1.,))
    at['SOURCE_TEXT']=label('music_source_label_v1','NO PLAYER',CHIP_RECT,26,LINE_CAP)
    at['TITLE_TEXT']=label('music_title_label_v1',TITLE_SEED,(24,94,918,160),46,TITLE_CAP)
    at['ARTIST_TEXT']=label('music_artist_label_v1','',(24,164,918,204),28,ARTIST_CAP,T['dim']+(1.,))
    panel('music_progress_track_v1',(BAR_LEFT,BAR_TOP,BAR_RIGHT,BAR_BOTTOM),T['track']+(1.,))
    at['ELAPSED_TEXT']=label('music_elapsed_label_v1','',(24,246,320,278),22,LINE_CAP,T['dim']+(1.,),anchor=0.)
    at['TOTAL_TEXT']=label('music_total_label_v1','',(622,246,918,278),22,LINE_CAP,T['dim']+(1.,),anchor=1.)
    panel('music_volume_track_v1',(VOL_LEFT,VOL_TOP,VOL_RIGHT,VOL_BOTTOM),T['track']+(1.,))
    for key,text,rect,role,size in CONTROLS:
        panel(f'music_{key}_background_v1',rect,T[role]+(1.,))
        index=label(f'music_{key}_label_v1',text,rect,size,LINE_CAP)
        if key=='play': at['PLAY_TEXT']=index
    at['BAR_FIRST']=meter('music_progress',BAR_LEFT,BAR_RIGHT,BAR_TOP,BAR_BOTTOM,BAR_COUNT,T['bar'],gap=1)
    at['VOL_FIRST']=meter('music_volume',VOL_LEFT,VOL_RIGHT,VOL_TOP,VOL_BOTTOM,VOL_COUNT,T['vol'])

    child=root.append(root.elements[7],'music_page_child_v1',(28,112,970,640),hidden=True)
    struct.pack_into('<Q',root.elements[child],0x78,MUSIC)
    # Native RenderMT reserves from these local vertex/index budgets, then
    # includes each child's budget. Matches the Doom page's proven reservation;
    # the worst case here (32 panels plus clamped text) stays well inside it.
    struct.pack_into('<2I',page.header,0x2c,3072,4608)
    struct.pack_into('<2I',root.header,0x2c,264,396)
    struct.pack_into('<2I',nav.header,0x2c,576,864)
    for canvas in (root,nav,page):canvas.validate()
    assert [bytes(r) for r in root.elements[:10]]==old_root
    assert [bytes(r) for r in nav.elements[:10]]==old_nav

    original=resource(bt,LEVEL);stride,rows=P.parse_cr(original);assert stride==296
    source=next(r for r in rows if u64(r,8)==ACTOR and u64(r,0)==0x275876572b742791)
    for row in rows:
        if u64(row,8)!=ACTOR or not 0x275876572b742791<=u64(row,0)<=0x275876572b742794:continue
        x,y=struct.unpack_from('<2f',row,0x80);w,h=struct.unpack_from('<2f',row,0x9c);s=1024/.3
        rect=((x-w)*s,(-y-h)*s,(x+w)*s,(-y+h)*s)
        assert tabrect[2]<=rect[0] or tabrect[0]>=rect[2] or tabrect[3]<=rect[1] or tabrect[1]>=rect[3],('Overlapping stock tab',rect)
    s=.3/1024
    def hitbox(name,rect,offset):
        row=bytearray(source);struct.pack_into('<Q',row,0,P.sym(name))
        x0,y0,x1,y1=rect
        if offset:x0+=28;x1+=28;y0+=112;y1+=112
        struct.pack_into('<2f',row,0x80,(x0+x1)/2*s,-(y0+y1)/2*s)
        struct.pack_into('<2f',row,0x9c,(x1-x0)/2*s,(y1-y0)/2*s)
        rows.append(bytes(row))
    hitbox('music_tab_button_v1',tabrect,False)
    for key,_,rect,_,_ in CONTROLS: hitbox(f'music_{key}_button_v1',rect,True)
    hitbox('music_source_button_v1',CHIP_RECT,True)
    header=bytearray(original[:56]);descriptor(header,0,len(rows),stride)
    buttons=bytes(header)+b''.join(rows);assert buttons[56:len(original)]==original[56:]

    resources={(cv,ROOT_CANVAS):root.serialize(),(cv,NAV_CANVAS):nav.serialize(),
               (cv,PAGE):resource(cv,PAGE),(bt,LEVEL):buttons,(cv,MUSIC):page.serialize()}
    patch=bytearray(b'TTP2'+struct.pack('<I',len(resources)));metadata=[]
    for (t,n),data in resources.items():
        accepted=[]
        for mf in (base,tools):
            value=tuple(mf.B[mf.index[t,n]][2:4]) if (t,n) in mf.index else (0,0)
            if value not in accepted:accepted.append(value)
        for entry in json.loads((HERE.parent/'TabletDoom/baselines/landing_v1.json').read_text()):
            if int(entry['type'],16)==t and int(entry['name'],16)==n:
                value=tuple(entry['hash'])
                if value not in accepted:accepted.append(value)
        patch+=struct.pack('<I',len(accepted))+b''.join(struct.pack('<2Q',*h) for h in accepted)
        lo,hi=struct.unpack('<2Q',hashlib.blake2b(data,digest_size=16).digest())
        frame=zstandard.ZstdCompressor(level=3).compress(data)
        patch+=struct.pack('<QQQII',t,n,0,len(data),16)+struct.pack('<5Q',t,n,lo,hi,tools.B[0][4])+struct.pack('<I',len(frame))+frame
        metadata.append(dict(type=f'{t:016x}',name=f'{n:016x}',size=len(data)))
    OUT.mkdir(exist_ok=True)
    if not patch_out:
        (OUT/f'music-{theme}.canvas').write_bytes(page.serialize())
        print(f'Rendered {theme} theme canvas only (no patch written).')
        return
    (OUT/'tablet.patch').write_bytes(patch)
    (OUT/'patch_items.json').write_text(json.dumps(metadata,indent=2))
    (OUT/'music_tab.json').write_text(json.dumps(dict(button=f'{BUTTON:016x}',rect=tabrect,root_child=child,
        root_count=11,nav_count=11,music_count=len(page.elements),
        marker=f'{P.sym("music_page_background_v1"):016x}'),indent=2))
    for name,canvas in [('root',root),('navigation',nav),('music',page)]:
        (OUT/(name+'.canvas')).write_bytes(canvas.serialize())

    generated=HERE.parent/'PersonalDiscTrainer/native/generated'
    lines=['// Generated by TabletMusic/build_music_tab.py',
           f'constexpr U MUSIC_TAB=0x{BUTTON:016x}, MUSIC_MARKER=0x{P.sym("music_page_background_v1"):016x};',
           f'constexpr unsigned MUSIC_ELEMENTS={len(page.elements)};',
           'constexpr unsigned '+','.join(f'MUSIC_{k}={v}' for k,v in sorted(at.items()))+';',
           f'constexpr unsigned MUSIC_BAR_COUNT={BAR_COUNT},MUSIC_VOL_COUNT={VOL_COUNT};',
           f'constexpr unsigned MUSIC_TITLE_CAP={TITLE_CAP},MUSIC_ARTIST_CAP={ARTIST_CAP},MUSIC_LINE_CAP={LINE_CAP};',
           '// Order matches MUSIC_COMMANDS in runtime.cpp.',
           'constexpr U MUSIC_BUTTONS[]={'+','.join(f'0x{P.sym(f"music_{key}_button_v1"):016x}' for key,_,_,_,_ in CONTROLS)
               +f',0x{P.sym("music_source_button_v1"):016x}'+'};']
    (generated/'music_tab.h').write_text('\n'.join(lines)+'\n',newline='\n')
    (OUT/f'music-{theme}.canvas').write_bytes(page.serialize())
    print(f'Built MUSIC tab ({theme} theme): {len(page.elements)} page elements, '
          f'{len(CONTROLS)+1} touch controls, original tabs/buttons preserved, '
          'no overlapping nav hit boxes.')

if __name__=='__main__':
    import argparse
    ap=argparse.ArgumentParser()
    ap.add_argument('--theme',default='neon',choices=sorted(THEMES))
    ap.add_argument('--preview-only',action='store_true',
                    help="render this theme's canvas without writing the patch")
    a=ap.parse_args()
    build(a.theme,not a.preview_only)
