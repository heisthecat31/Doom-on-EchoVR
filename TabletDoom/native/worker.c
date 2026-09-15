#define WIN32_LEAN_AND_MEAN
#include "shared.h"
#include "doomgeneric.h"
#include "doomkeys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static DoomShared* shared;
static HANDLE frameMutex,parent;
static unsigned short queue[64];
static unsigned head,tail,oldKeys;
static const unsigned char doomKeys[DK_COUNT]={KEY_UPARROW,KEY_DOWNARROW,KEY_LEFTARROW,KEY_RIGHTARROW,KEY_FIRE,KEY_USE,KEY_ENTER,KEY_ESCAPE};
static const int winKeys[DK_COUNT]={VK_UP,VK_DOWN,VK_LEFT,VK_RIGHT,VK_CONTROL,VK_SPACE,VK_RETURN,VK_ESCAPE};
static DWORD parentId;
static DWORD pausedTime;
static void cleanup(void) {if(shared) InterlockedExchange(&shared->status,2);}
static int focused(void) {
    return (DWORD)(GetTickCount()-InterlockedCompareExchange(&shared->heartbeat,0,0))<200;
}
static void sampleKeys(int focus) {
    unsigned keys=focus?(unsigned)InterlockedCompareExchange(&shared->keys,0,0):0;
    DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    if(focus && foreground==parentId) for(unsigned i=0;i<DK_COUNT;i++) if(GetAsyncKeyState(winKeys[i])&0x8000) keys|=1u<<i;
    for(unsigned i=0;i<DK_COUNT;i++) if((oldKeys^keys)&(1u<<i)) {
        if(head-tail<64) queue[(head++)%64]=(unsigned short)(((keys>>i&1)<<8)|doomKeys[i]);
    }
    oldKeys=keys;
}
static void keepAlive(void) {if(WaitForSingleObject(parent,0)!=WAIT_TIMEOUT) exit(0);}
void DG_Init(void) {}
void DG_DrawFrame(void) {
    keepAlive();
    if(WaitForSingleObject(frameMutex,10)==WAIT_OBJECT_0) {
        memcpy(shared->pixels,DG_ScreenBuffer,sizeof(shared->pixels));
        InterlockedIncrement(&shared->frames);InterlockedExchange(&shared->status,1);
        ReleaseMutex(frameMutex);
    }
}
void DG_SleepMs(uint32_t ms) {Sleep(ms>20?20:ms);keepAlive();}
uint32_t DG_GetTicksMs(void) {return GetTickCount()-pausedTime;}
int DG_GetKey(int* pressed,unsigned char* key) {
    sampleKeys(focused());
    if(head==tail) return 0;
    unsigned short event=queue[(tail++)%64];*pressed=event>>8;*key=(unsigned char)event;return 1;
}
void DG_SetWindowTitle(const char* title) {(void)title;}
int main(int argc,char** argv) {
    if(argc!=4) {fprintf(stderr,"Usage: DoomWorker parent-pid mapping-name wad\n");return 2;}
    parentId=strtoul(argv[1],NULL,10);parent=OpenProcess(SYNCHRONIZE,FALSE,parentId);
    HANDLE mapping=OpenFileMappingA(FILE_MAP_ALL_ACCESS,FALSE,argv[2]);
    char mutexName[256];snprintf(mutexName,sizeof(mutexName),"%s-frame",argv[2]);
    frameMutex=OpenMutexA(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,mutexName);
    if(!parent || !mapping || !frameMutex) return 3;
    shared=MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(DoomShared));
    if(!shared || shared->magic!=DOOM_SHARED_MAGIC) return 4;
    atexit(cleanup);
    char* args[]={"DoomWorker","-iwad",argv[3],"-nosound","-nomouse",NULL};
    doomgeneric_Create(5,args);
    for(;;) {
        keepAlive();
        sampleKeys(focused());
        if(!focused()) {
            DWORD start=GetTickCount();
            do {Sleep(20);keepAlive();sampleKeys(0);} while(!focused());
            pausedTime+=GetTickCount()-start;
        }
        doomgeneric_Tick();
        Sleep(1);
    }
}
