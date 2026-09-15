"""Generate the native worker compiler response file; no changes to upstream."""
from pathlib import Path
import xml.etree.ElementTree as X
HERE=Path(__file__).resolve().parent
source=HERE/'vendor/doomgeneric/doomgeneric'
build=HERE/'build/worker';build.mkdir(parents=True,exist_ok=True)
files=[source/x.get('Include') for x in X.parse(source/'doomgeneric.vcxproj').iter() if x.tag.endswith('ClCompile') and x.get('Include') and x.get('Include')!='doomgeneric_win.c']
files.append(HERE/'native/worker.c')
args=['/nologo','/O2','/MT','/std:c11','/D_CRT_SECURE_NO_WARNINGS','/DDOOMGENERIC_RESX=320','/DDOOMGENERIC_RESY=200',f'/I"{source}"',f'/Fo"{build}/"',f'/Fe"{HERE}/build/DoomWorker.exe"']
args += ['"'+str(p)+'"' for p in files]
args += ['/link','user32.lib','winmm.lib']
(HERE/'build/worker.rsp').write_text('\n'.join(args))
print('Prepared native worker sources.')
