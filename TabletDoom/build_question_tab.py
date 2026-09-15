"""Build the left '?' tab from the known Tools assets; never edits game files."""
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
DOOM=P.sym('echovr_doom_arm_page_v1')
BUTTON=P.sym('doom_question_button_v1')

def build():
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
    tabrect=(30,662,122,751)
    nav.label(donor.elements[1],'doom_question_label_v1','?',tabrect,44)
    doom=empty_canvas(donor.serialize(),942,528)
    struct.pack_into('<Q',doom.header,0,DOOM)
    panel=doom.append(donor.elements[0],'doom_page_background_v1',(0,0,942,528))
    struct.pack_into('<4f',doom.elements[panel],0x78,.025,.055,.09,1)
    doom.label(donor.elements[1],'doom_page_heading_v1','DOOM',(15,30,142,90),32)
    doom.label(donor.elements[1],'doom_page_description_v1','SHAREWARE',(8,85,145,130),18)
    doom.label(donor.elements[1],'doom_page_footer_v1','ARROWS | CTRL | SPACE | ENTER | ESC',(151,501,791,528),16)
    controls=[('up','UP',(47,170,105,225)),('down','DOWN',(47,290,105,345)),
              ('left','LEFT',(8,230,66,285)),('right','RIGHT',(86,230,144,285)),
              ('fire','FIRE',(807,150,934,220)),('use','USE',(807,240,934,310)),
              ('enter','ENTER',(807,330,934,400)),('escape','MENU',(807,420,934,490))]
    for key,label,rect in controls:
        panel=doom.append(donor.elements[0],f'doom_{key}_background_v1',rect)
        struct.pack_into('<4f',doom.elements[panel],0x78,.1,.22,.32,1)
        doom.label(donor.elements[1],f'doom_{key}_label_v1',label,rect,18 if key in ('left','right','down') else 24)
    child=root.append(root.elements[7],'doom_page_child_v1',(28,112,970,640),hidden=True)
    struct.pack_into('<Q',root.elements[child],0x78,DOOM)
    # Native RenderMT reserves from these local vertex/index budgets, then
    # includes each child's budget. Account for the newly authored controls.
    struct.pack_into('<2I',doom.header,0x2c,3072,4608)
    struct.pack_into('<2I',root.header,0x2c,264,396)
    struct.pack_into('<2I',nav.header,0x2c,548,822)
    for canvas in (root,nav,doom):canvas.validate()
    assert [bytes(r) for r in root.elements[:10]]==old_root
    assert [bytes(r) for r in nav.elements[:10]]==old_nav
    original=resource(bt,LEVEL);stride,rows=P.parse_cr(original);assert stride==296
    source=next(r for r in rows if u64(r,8)==ACTOR and u64(r,0)==0x275876572b742791)
    for row in rows:
        if u64(row,8)!=ACTOR or not 0x275876572b742791<=u64(row,0)<=0x275876572b742794:continue
        x,y=struct.unpack_from('<2f',row,0x80);w,h=struct.unpack_from('<2f',row,0x9c);s=1024/.3
        rect=((x-w)*s,(-y-h)*s,(x+w)*s,(-y+h)*s)
        assert tabrect[2]<=rect[0] or tabrect[0]>=rect[2] or tabrect[3]<=rect[1] or tabrect[1]>=rect[3],('Overlapping stock tab',rect)
    row=bytearray(source);struct.pack_into('<Q',row,0,BUTTON)
    x0,y0,x1,y1=tabrect;s=.3/1024
    struct.pack_into('<2f',row,0x80,(x0+x1)/2*s,-(y0+y1)/2*s)
    struct.pack_into('<2f',row,0x9c,(x1-x0)/2*s,(y1-y0)/2*s)
    rows.append(bytes(row))
    for key,_,rect in controls:
        row=bytearray(source);struct.pack_into('<Q',row,0,P.sym(f'doom_{key}_button_v1'))
        x0,y0,x1,y1=rect;x0+=28;x1+=28;y0+=112;y1+=112
        struct.pack_into('<2f',row,0x80,(x0+x1)/2*s,-(y0+y1)/2*s)
        struct.pack_into('<2f',row,0x9c,(x1-x0)/2*s,(y1-y0)/2*s)
        rows.append(bytes(row))
    header=bytearray(original[:56]);descriptor(header,0,len(rows),stride)
    buttons=bytes(header)+b''.join(rows);assert buttons[56:len(original)]==original[56:]
    resources={(cv,ROOT_CANVAS):root.serialize(),(cv,NAV_CANVAS):nav.serialize(),
               (cv,PAGE):resource(cv,PAGE),(bt,LEVEL):buttons,(cv,DOOM):doom.serialize()}
    patch=bytearray(b'TTP2'+struct.pack('<I',len(resources)));metadata=[]
    for (t,n),data in resources.items():
        accepted=[]
        for mf in (base,tools):
            value=tuple(mf.B[mf.index[t,n]][2:4]) if (t,n) in mf.index else (0,0)
            if value not in accepted:accepted.append(value)
        for entry in json.loads((HERE/'baselines/landing_v1.json').read_text()):
            if int(entry['type'],16)==t and int(entry['name'],16)==n:
                value=tuple(entry['hash'])
                if value not in accepted:accepted.append(value)
        patch+=struct.pack('<I',len(accepted))+b''.join(struct.pack('<2Q',*h) for h in accepted)
        lo,hi=struct.unpack('<2Q',hashlib.blake2b(data,digest_size=16).digest())
        frame=zstandard.ZstdCompressor(level=3).compress(data)
        patch+=struct.pack('<QQQII',t,n,0,len(data),16)+struct.pack('<5Q',t,n,lo,hi,tools.B[0][4])+struct.pack('<I',len(frame))+frame
        metadata.append(dict(type=f'{t:016x}',name=f'{n:016x}',size=len(data)))
    OUT.mkdir(exist_ok=True)
    (OUT/'tablet.patch').write_bytes(patch)
    (OUT/'patch_items.json').write_text(json.dumps(metadata,indent=2))
    (OUT/'question_tab.json').write_text(json.dumps(dict(button=f'{BUTTON:016x}',rect=tabrect,root_child=child,root_count=11,nav_count=11,doom_count=len(doom.elements),doom_marker=f'{P.sym("doom_page_heading_v1"):016x}'),indent=2))
    for name,canvas in [('root',root),('navigation',nav),('doom',doom)]: (OUT/(name+'.canvas')).write_bytes(canvas.serialize())
    generated=HERE.parent/'PersonalDiscTrainer/native/generated'
    (generated/'question_tab.h').write_text(f'// Generated by TabletDoom/build_question_tab.py\nconstexpr U QUESTION=0x{BUTTON:016x}, DOOM_MARKER=0x{P.sym("doom_page_heading_v1"):016x};\n')
    with (generated/'question_tab.h').open('a') as f:
        f.write(f'constexpr unsigned DOOM_ELEMENTS={len(doom.elements)};\n')
        f.write('constexpr U DOOM_KEYS[]={'+','.join(f'0x{P.sym("doom_"+key+"_button_v1"):016x}' for key,_,_ in controls)+'};\n')
    print('Built ? tab: five-resource patch, original tabs/buttons preserved, no overlapping nav hit boxes.')

if __name__=='__main__':build()
