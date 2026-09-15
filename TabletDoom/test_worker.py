"""Exercise the real native Doom worker, IPC, input and focus pause offline."""
import ctypes as C, os, subprocess, time, struct, zlib
from ctypes import wintypes as W
from pathlib import Path
HERE=Path(__file__).resolve().parent
k=C.WinDLL('kernel32',use_last_error=True)
k.CreateFileMappingW.argtypes=[W.HANDLE,C.c_void_p,W.DWORD,W.DWORD,W.DWORD,W.LPCWSTR];k.CreateFileMappingW.restype=W.HANDLE
k.MapViewOfFile.argtypes=[W.HANDLE,W.DWORD,W.DWORD,W.DWORD,C.c_size_t];k.MapViewOfFile.restype=C.c_void_p
k.CreateMutexW.argtypes=[C.c_void_p,W.BOOL,W.LPCWSTR];k.CreateMutexW.restype=W.HANDLE
k.WaitForSingleObject.argtypes=[W.HANDLE,W.DWORD];k.ReleaseMutex.argtypes=[W.HANDLE]
k.UnmapViewOfFile.argtypes=[C.c_void_p];k.CloseHandle.argtypes=[W.HANDLE]
class Shared(C.Structure):
    _fields_=[('magic',W.LONG),('heartbeat',W.LONG),('keys',W.LONG),('status',W.LONG),('frames',W.LONG),('pixels',W.DWORD*64000)]
name=f'Local\\EchoTabletDoom-test-{os.getpid()}'
mapping=k.CreateFileMappingW(W.HANDLE(-1),None,4,0,C.sizeof(Shared),name)
ptr=k.MapViewOfFile(mapping,0xf001f,0,0,C.sizeof(Shared));assert ptr
s=Shared.from_address(ptr);s.magic=0x314d4f44
mutex=k.CreateMutexW(None,False,name+'-frame');assert mutex
log=(HERE/'build/worker-test.log').open('w')
p=subprocess.Popen([str(HERE/'build/DoomWorker.exe'),str(os.getpid()),name,str(HERE/'data/doom1.wad')],cwd=HERE/'build',stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
def run(seconds,keys=0,focus=True):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        assert p.poll() is None, f'Worker exited: {p.returncode}; see worker-test.log'
        if focus:s.heartbeat=k.GetTickCount()
        s.keys=keys;time.sleep(.025)
def shot(name):
    assert k.WaitForSingleObject(mutex,1000)==0
    try:raw=bytes(s.pixels)
    finally:k.ReleaseMutex(mutex)
    rgb=b''.join(raw[i:i+3][::-1] for i in range(0,len(raw),4))
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
    png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>2I5B',320,200,8,2,0,0,0))
    png+=chunk(b'IDAT',zlib.compress(b''.join(b'\0'+rgb[y*960:(y+1)*960] for y in range(200))))+chunk(b'IEND',b'')
    (HERE/'build'/name).write_bytes(png)
    assert len(set(struct.unpack('<64000I',raw)))>32
    return raw
try:
    run(2);assert s.status==1 and s.frames>10
    shot('doom-title.png')
    for _ in range(5):run(.12,1<<6);run(.6)
    first=shot('doom-game.png');run(1,1<<0);second=shot('doom-moved.png')
    assert first!=second,'Movement did not change framebuffer'
    run(.4,focus=False);before=s.frames;run(.4,focus=False);assert s.frames==before,'Hidden page kept ticking'
    run(.4);assert s.frames>before,'Focus failed to resume'
    print(f'PASS: real Doom frames ({s.frames}), tablet key input, hidden-page pause/resume.')
finally:
    if p.poll() is None:p.terminate()
    p.wait(timeout=5);log.close();k.UnmapViewOfFile(ptr);k.CloseHandle(mapping);k.CloseHandle(mutex)
