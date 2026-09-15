"""Build an experimental fifth tab for the shared multiplayer ARM tablet.

Offline only. The separate runtime handles native button events and page state.
Does not install anything or connect either toggle to gameplay.
"""
import hashlib
import json
import struct
from pathlib import Path
import echovr_pkg as P
from canvas_edit import Canvas, descriptor, u64
from echovr_patch import Patcher, ManifestFile, MID, GAME_MANIFEST, GAME_PACKAGES, verify
from pe_image import PEImage

HERE = Path(__file__).resolve().parent
OUT = HERE / 'build/tools_tab'
LEVEL = P.sym('r14_glb_global_mp')
ACTOR = 0x6c1f6ff04e070923
ROOT_CANVAS = 0x30b4d30bbcb8d444
NAV_CANVAS = 0xfdea8ae53c32407e
PAGE_NAME = 'echovr_tools_arm_page_v1'
PAGE = P.sym(PAGE_NAME)
EXE_SHA = '3dae0cdab2eb298f9b04fc6baac83f8dd304a8f1d9fea057ab30438fe271df9a'
ADDRESSES = dict(canvas_loaded=0x71fc90, canvas_unload=0x7287b0,
                  show=0x71c820, set_text=0x727f10, set_alpha=0x726f00, button_update=0x92f3f0,
                  button_disable=0x92b9e0, button_enable=0x92bd10,
                  dispatch=0x510060)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def empty_canvas(donor, w, h):
    c = Canvas(donor)
    c.elements, c.behaviors, c.behavior_data = [], [], []
    c.atlas, c.string_offsets, c.text, c.bindings = bytearray(), bytearray(), bytearray(), bytearray()
    for off, stride in ((0x50,224),(0x88,88),(0xc0,24),(0x100,4),(0x138,1),(0x170,24)):
        descriptor(c.header, off, 0, stride)
    struct.pack_into('<4I', c.header, 0xc, w, h, w, h)
    return c


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    patcher = Patcher()
    m = patcher.stock
    original_manifest = GAME_MANIFEST.read_bytes()
    before = ManifestFile(original_manifest)
    cvtype = P.typesym('CUICanvasResource')
    root = Canvas(m.get(cvtype, ROOT_CANVAS))
    nav = Canvas(m.get(cvtype, NAV_CANVAS))
    title_donor = m.get(cvtype, 0xfdea8aeb3a0f4862)
    donor = Canvas(title_donor)
    if len(root.elements) != 7 or len(nav.elements) != 9:
        raise ValueError('Unexpected tablet layout; refusing to patch')
    if m.get(cvtype, PAGE) is not None:
        raise ValueError('Tools page already exists; restore the previous build first')
    # Preserve all four stock navigation icons and their native hit boxes.
    tab = nav.label(donor.elements[1], 'tools_tab_label_v1', 'TOOLS', (900,662,1004,751), size=27)
    page = empty_canvas(title_donor, 942, 528)
    struct.pack_into('<Q', page.header, 0, PAGE)

    def panel(name, rect, color):
        i = page.append(donor.elements[0], name, rect)
        struct.pack_into('<4f', page.elements[i], 0x78, *color)
        return i

    panel('tools_page_background_v1', (0,0,942,528), (.025,.055,.09,1))
    heading = page.label(donor.elements[1], 'tools_page_heading_v1', 'TOOLS', (32,20,910,90), 44)
    page.label(donor.elements[1], 'tools_page_description_v1', 'TRAINING CONTROLS', (32,91,910,140), 24)
    panel('tools_goalie_background_v1', (32,174,910,284), (.08,.18,.26,1))
    goalie = page.label(donor.elements[1], 'tools_goalie_label_v1', 'GOALIE TRAINER: OFF', (45,174,897,284), 34)
    panel('tools_disc_background_v1', (32,312,910,422), (.08,.18,.26,1))
    disc = page.label(donor.elements[1], 'tools_disc_label_v1', 'PERSONAL DISC: OFF', (45,312,897,422), 34)
    page.label(donor.elements[1], 'tools_page_footer_v1', 'UI PREVIEW - GAMEPLAY CONNECTION COMING LATER', (20,453,922,505), 20)
    child = root.append(root.elements[1], 'tools_page_child_v1', (28,112,970,640), hidden=True)
    struct.pack_into('<Q', root.elements[child], 0x78, PAGE)
    # Root child list already contains the original resources; the loader discovers
    # this appended child through its element record, just like stock children.
    root_header = root.append(donor.elements[0], 'tools_header_background_v1', (54,22,870,112), hidden=True)
    struct.pack_into('<4f', root.elements[root_header], 0x78, .025,.055,.09,1)
    root_title = root.label(donor.elements[1], 'tools_root_title_v1', 'TOOLS', (130,22,822,112), 40, hidden=True)
    for c in (root, nav, page):
        c.validate()

    cr = m.get(P.typesym('CR15ButtonInteractCR'), LEVEL)
    stride, rows = P.parse_cr(cr)
    if stride != 296:
        raise ValueError('Unexpected native button record size')
    source = next(r for r in rows if u64(r,8)==ACTOR and u64(r,0)==0x275876572b742791)
    stock_nav = [f'{u64(r,0):016x}' for r in rows if u64(r,8)==ACTOR and 0x275876572b742791 <= u64(r,0) <= 0x275876572b742794]
    if len(stock_nav) != 4:
        raise ValueError('Expected four stock navigation buttons')
    buttons = {}
    # The native tablet's canvas is 0.3m / 1024px. Use the same Z, rotation,
    # press travel and feedback as the existing navigation button. XY bounds
    # are inferred from the four shipped tab positions and need VR validation.
    for key, rect in dict(tab=(900,662,1004,751), goalie=(60,286,938,396), disc=(60,424,938,534)).items():
        row = bytearray(source)
        name = f'tools_{key}_button_v1'
        struct.pack_into('<Q', row, 0, P.sym(name))
        x0,y0,x1,y1 = rect
        scale = .3/1024
        struct.pack_into('<2f', row, 0x80, (x0+x1)/2*scale, -(y0+y1)/2*scale)
        struct.pack_into('<2f', row, 0x9c, (x1-x0)/2*scale, (y1-y0)/2*scale)
        # Runtime gates page controls before the native input update. Keep the
        # serialized component flags unchanged: they are NOT runtime reason bits.
        rows.append(bytes(row))
        buttons[key] = dict(name=name, symbol=f'{P.sym(name):016x}', rect=rect)
    hdr = bytearray(cr[:56])
    descriptor(hdr, 0, len(rows), stride)
    new_cr = bytes(hdr)+b''.join(rows)
    # Existing rows are byte-identical, including every native navigation action.
    assert new_cr[56:len(cr)] == cr[56:]
    patcher.add_item('CUICanvasResource', False, f'0x{ROOT_CANVAS:016x}', root.serialize())
    patcher.add_item('CUICanvasResource', False, f'0x{NAV_CANVAS:016x}', nav.serialize())
    patcher.add_item('CUICanvasResource', False, PAGE_NAME, page.serialize())
    patcher.add_item('CR15ButtonInteractCR', False, f'0x{LEVEL:016x}', new_cr)
    # An inert texture-editor package exists in this install. Never overwrite it.
    while (GAME_PACKAGES/f'{MID}_{patcher.mf.npkg}').exists():
        i = patcher.mf.npkg
        patcher.mf.C.append([i, (GAME_PACKAGES/f'{MID}_{i}').stat().st_size, 0, 0])
        patcher.mf.npkg += 1
    pkg_index = patcher.mf.npkg
    patcher.build(OUT)
    after = ManifestFile((OUT/'manifests'/MID).read_bytes())
    overrides = {(t,n) for t,n,_ in patcher.items}
    after_rows = {(a[0],a[1]):(a,b) for a,b in zip(after.A,after.B)}
    for a,b in zip(before.A,before.B):
        if (a[0],a[1]) not in overrides:
            assert after_rows[(a[0],a[1])] == (a,b), 'Unrelated resource changed'
    assert after.C[:len(before.C)] == before.C, 'Existing frame references changed'
    if not verify(OUT):
        raise ValueError('Package verification failed')
    pe = PEImage(P.ROOT.parents[3]/'bin/win10/echovr.exe')
    if pe.sha256 != EXE_SHA:
        raise ValueError('Unresearched executable: native addresses must be revalidated')
    metadata = dict(schema=1, status='experimental; see TOOLS_TAB.md for validation results', target='hand/arm social tablet',
        data_root=str(P.ROOT), base_manifest_sha256=digest(original_manifest),
        manifest_sha256=digest((OUT/'manifests'/MID).read_bytes()),
        package=f'{MID}_{pkg_index}', package_sha256=digest((OUT/'packages'/f'{MID}_{pkg_index}').read_bytes()),
        executable_sha256=pe.sha256, module_size=pe.size,
        addresses={k:dict(rva=v,bytes=pe.read(v,16).hex()) for k,v in ADDRESSES.items()},
        actor=f'{ACTOR:016x}', level=f'{LEVEL:016x}', stock_nav=stock_nav, buttons=buttons,
        content_buttons=[f'{u64(r,0):016x}' for r in P.parse_cr(cr)[1]
                         if (u64(r,8)==ACTOR and r in P.parse_cr(cr)[1][5:10])
                         or u64(r,8) in (0x7d3d02f79af5d321,0xbedd89349a5c23f1)],
        root=dict(marker=f'{P.sym("tools_page_child_v1"):016x}', marker_index=child, count=len(root.elements), page=child, title=root_title, header=root_header, hide=[0,1,3]),
        nav=dict(marker=f'{P.sym("tools_tab_label_v1"):016x}', marker_index=tab, count=len(nav.elements), label=tab),
        page=dict(marker=f'{P.sym("tools_page_heading_v1"):016x}', marker_index=heading, count=len(page.elements), goalie=goalie, disc=disc),
        unchanged_resources=len(before.A)-3)
    (OUT/'tools_tab.json').write_text(json.dumps(metadata,indent=2))
    for name,c in [('root',root),('navigation',nav),('page',page)]:
        (OUT/f'{name}.canvas').write_bytes(c.serialize())
    print(f'Built experimental full-navigation tab. Preserved {metadata["unchanged_resources"]} other resource entries.')
    print('No game files changed. Metadata:', OUT/'tools_tab.json')
    return metadata


if __name__ == '__main__':
    build()
