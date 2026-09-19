"""Assemble the native release and integrity manifest (developer tool only)."""
from pathlib import Path
import json,hashlib,shutil,zipfile
HERE=Path(__file__).resolve().parent
native=HERE/'native';out=native/'dist'
inputs=json.loads((native/'generated/install_inputs.json').read_text())
shutil.copy2(native/'Install.ps1',out/'Install.ps1')
shutil.copy2(HERE/'README.md',out/'README.md')
allowed={
    '351c49438bd38225.dll':['a8a8e156223ba10246f867e4513d272f5d89f0c3b5bd1a3167682176a429c4c1'],
    '38965d90a823f03f.dll':['777653b7547925b2c86bf5f343848564b3c07c6efd8df421acbb629604ed67ed'],
}
doom=HERE.parent/'TabletDoom';music=HERE.parent/'TabletMusic'
# Both tab workers are optional: the installed tablet patch decides which tab
# exists, so package whichever workers have been built.
workers=[]
if (doom/'build/DoomWorker.exe').exists():
    (out/'doom').mkdir(exist_ok=True)
    shutil.copy2(doom/'build/DoomWorker.exe',out/'doom/DoomWorker.exe')
    shutil.copy2(doom/'data/doom1.wad',out/'doom/doom1.wad')
    workers+=sorted((out/'doom').iterdir())
if (music/'build/MusicWorker.exe').exists():
    (out/'music').mkdir(exist_ok=True)
    shutil.copy2(music/'build/MusicWorker.exe',out/'music/MusicWorker.exe')
    workers+=sorted((out/'music').iterdir())
manifest=dict(version='0.2.1',exe_size=inputs['exe_size'],exe_timestamp=inputs['exe_timestamp'],scripts=[],files=[])
for name,s in inputs['scripts'].items():
    manifest['scripts'].append(dict(name=name,accepted=[s['sha256'],*allowed.get(name,[])]))
paths=[out/'EchoTabletTrainer.dll',out/'manifest_merge.exe',out/'tablet.patch',*sorted((out/'scripts').glob('*.dll')),*workers]
for path in paths:
    manifest['files'].append(dict(path=path.relative_to(out).as_posix(),sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
(out/'package.json').write_text(json.dumps(manifest,indent=2))
licenses=out/'licenses';licenses.mkdir(exist_ok=True)
shutil.copy2(native/'vendor/minhook/LICENSE.txt',licenses/'MinHook.txt')
shutil.copy2(native/'vendor/zstd/LICENSE',licenses/'Zstd.txt')
shutil.copy2(doom/'vendor/doomgeneric/LICENSE',licenses/'DoomGeneric.txt')
with zipfile.ZipFile(licenses/'DoomWorker-source.zip','w',zipfile.ZIP_DEFLATED) as archive:
    for path in (doom/'vendor/doomgeneric').rglob('*'):
        if path.is_file() and '.git' not in path.parts:archive.write(path,Path('TabletDoom')/path.relative_to(doom))
    for path in [*sorted((doom/'native').glob('*')),*doom.glob('build_worker.*')]:
        archive.write(path,Path('TabletDoom')/path.relative_to(doom))
with zipfile.ZipFile(native/'EchoTabletTrainer-test.zip','w',zipfile.ZIP_DEFLATED) as archive:
    for path in [*paths,out/'Install.ps1',out/'package.json',out/'README.md',*licenses.glob('*.txt'),licenses/'DoomWorker-source.zip']:
        archive.write(path,Path('EchoTabletTrainer')/path.relative_to(out))
included=', '.join(sorted({p.parent.name for p in workers})) or 'none'
print(f'Packaged native runtime, preserved scripts, tablet merge patch, and installer; tab workers: {included}.')
