"""Prepare exact native bindings and preserved script payloads; workspace only."""
from pathlib import Path
import hashlib
import json
import struct
import sys

HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE.parent/'EchoVr-Tablet-Probing'))
from pe_image import PEImage
from disc_binding import SPECS
import echovr_pkg as P
from echovr_patch import ManifestFile

OUT=HERE/'native/generated'
DIST=HERE/'native/dist'

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    (DIST/'scripts').mkdir(parents=True,exist_ok=True)
    game=P.ROOT.parents[3]/'bin/win10'
    exe=PEImage(game/'echovr.exe')
    timestamp=struct.unpack_from('<I',exe.data,struct.unpack_from('<I',exe.data,60)[0]+8)[0]
    rvas=[0x71fc90,0x7287b0,0x71c820,0x727f10,0x726f00,0x92f3f0,0x92b9e0,0x92bd10,0x510060,
          0x4f37a0,0x44cc40,0x638830,0x105510,0x75fd80,0x6320e0,
          0x724ff0,0x725730,0x5657e0]
    def array(b):return '{'+','.join(f'0x{x:02x}' for x in b)+'}'
    lines=[f'static constexpr unsigned EXE_TIMESTAMP={timestamp};',
           'struct Signature {unsigned rva; unsigned char bytes[16];};',
           'static const Signature signatures[]={']
    lines += ['{'+hex(rva)+','+array(exe.read(rva,16))+'},' for rva in rvas]
    lines+=['};','static const unsigned char guardSignatures[2][16]={']
    manifest=dict(schema=1,exe_size=exe.size,exe_timestamp=timestamp,scripts={},files={})
    for kind,name in enumerate([*SPECS,'156208a7bf6bcfec.dll']):
        original=name.removesuffix('.dll')+'.trainer-original.dll'
        source=original if (game/'EchoTabletTrainer.install.json').exists() else name
        pe=PEImage(game/'scripts'/source)
        if name in SPECS:
            if pe.sha256!=SPECS[name]['sha256']:raise ValueError('Unexpected personal-disc script '+name)
            lines += [array(pe.read(SPECS[name]['allowed'],16))+',']
        elif pe.sha256!='b239c96c50499119221ce2e3ad3b4960649fd594de1583537bfca3a3a336fc4e':
            raise ValueError('Unexpected original tablet script')
        (DIST/'scripts'/original).write_bytes(pe.data)
        manifest['scripts'][name]=dict(kind=kind,original=original,sha256=pe.sha256)
    lines+=['};']
    (OUT/'bindings.h').write_text('\n'.join(lines)+'\n')
    (OUT/'install_inputs.json').write_text(json.dumps(manifest,indent=2))
    tablet=HERE.parent/'EchoVr-Tablet-Probing'
    base=ManifestFile((tablet/'backups/20260914T235837383130Z/manifest.before').read_bytes())
    current=ManifestFile((tablet/'build/tools_tab/manifests/48037dc70b0ecab2').read_bytes())
    items=json.loads((tablet/'build/tools_tab/patch_items.json').read_text())
    patch=bytearray(b'TTP1'+struct.pack('<I',len(items)))
    for item in items:
        key=(int(item['type'],16),int(item['name'],16))
        before=base.B[base.index[key]][2:4] if key in base.index else (0,0)
        index=current.index[key];a,b=current.A[index],current.B[index]
        pkg,offset,size,_=current.C[a[2]&0xffffffff]
        frame=(tablet/f'build/tools_tab/packages/48037dc70b0ecab2_{pkg}').read_bytes()[offset:offset+size]
        patch+=struct.pack('<2Q',*before)+struct.pack('<QQQII',*a)+struct.pack('<5Q',*b)+struct.pack('<I',len(frame))+frame
    (DIST/'tablet.patch').write_bytes(patch)
    question=HERE.parent/'TabletDoom/build/tablet.patch'
    if not question.exists(): raise RuntimeError('Run python -B TabletDoom/build_question_tab.py from the workspace first')
    (DIST/'tablet.patch').write_bytes(question.read_bytes())
    print('Prepared native signatures and three preserved script copies.')

if __name__=='__main__':main()
