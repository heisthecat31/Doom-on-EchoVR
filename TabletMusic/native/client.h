#pragma once
#include "shared.h"
#include <string>
#include <cstring>

/* Fit `value` into a canvas text slot: never more than the element's reserved
   capacity, never more than `display` bytes, never splitting a UTF-8 sequence.
   Writes a NUL-terminated string and marks a truncation with "..". */
inline void musicClamp(char* out,size_t size,unsigned capacity,unsigned display,const char* value) {
    if(!out || size<4) return;
    if(!value) value="";
    unsigned limit=capacity?capacity-1:0;   /* total bytes the result may occupy */
    if(display<limit) limit=display;
    if(limit>unsigned(size)-1) limit=unsigned(size)-1;
    unsigned n=0; while(n<limit && value[n]) n++;
    if(value[n]) {
        n=limit>=2?limit-2:0;               /* the ".." marker counts toward the limit */
        while(n>0 && (unsigned char)value[n]>=0x80 && (unsigned char)value[n]<0xc0) n--;
        memcpy(out,value,n);
        if(limit>=2) {out[n++]='.';out[n++]='.';}
    } else memcpy(out,value,n);
    out[n]=0;
}
/* Whole progress segments to light, clamped to the bar even if the player
   reports a position past the end. */
inline int musicFilled(LONG position,LONG duration,unsigned segments) {
    if(duration<=0 || position<0) return 0;
    LONG filled=(position*LONG(segments)+duration/2)/duration;
    if(filled<0) return 0;
    if(filled>LONG(segments)) return int(segments);
    return int(filled);
}

/* Snapshot of the worker's last poll, cached so the tablet's per-frame render
   path never takes the text mutex unless something actually changed. */
struct MusicSnapshot {
    char title[MUSIC_TEXT]{};
    char artist[MUSIC_TEXT]{};
    char source[MUSIC_SOURCE]{};
    LONG playback=MP_UNKNOWN,position=-1,duration=-1,volume=-1,sessions=0,updates=0;
};

class MusicClient {
    HANDLE mapping=nullptr,textMutex=nullptr,process=nullptr,job=nullptr;
    MusicShared* shared=nullptr;
    MusicSnapshot cached{};
public:
    bool start() {
        if(process && WaitForSingleObject(process,0)==WAIT_TIMEOUT) return true;
        if(process) {CloseHandle(process);process=nullptr;}
        wchar_t name[128];swprintf_s(name,L"Local\\EchoTabletMusic-%lu",GetCurrentProcessId());
        if(!mapping) mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(MusicShared),name);
        if(!mapping) return false;
        if(!shared) shared=(MusicShared*)MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(MusicShared));
        std::wstring mutexName=std::wstring(name)+L"-text";
        if(!textMutex) textMutex=CreateMutexW(nullptr,FALSE,mutexName.c_str());
        if(!shared || !textMutex) return false;
        shared->magic=MUSIC_SHARED_MAGIC;shared->status=0;shared->updates=0;
        shared->head=shared->tail=0;shared->heartbeat=0;
        shared->playback=MP_UNKNOWN;shared->position=-1;shared->duration=-1;
        shared->volume=-1;shared->sessions=0;shared->selected=-1;
        cached=MusicSnapshot{};
        wchar_t path[32768];GetModuleFileNameW(nullptr,path,32768);
        std::wstring bin(path);bin.resize(bin.find_last_of(L"\\/"));
        auto worker=bin+L"\\music\\MusicWorker.exe";
        if(GetFileAttributesW(worker.c_str())==INVALID_FILE_ATTRIBUTES) return false;
        GetEnvironmentVariableW(L"LOCALAPPDATA",path,32768);
        auto root=std::wstring(path)+L"\\EchoTabletTrainer";
        auto work=root+L"\\Music";
        CreateDirectoryW(root.c_str(),nullptr); /* the runtime's log already makes this in Echo */
        if(!CreateDirectoryW(work.c_str(),nullptr) && GetLastError()!=ERROR_ALREADY_EXISTS) return false;
        SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
        HANDLE output=CreateFileW((work+L"\\worker.log").c_str(),GENERIC_WRITE,FILE_SHARE_READ,&sa,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
        if(output!=INVALID_HANDLE_VALUE) {startup.dwFlags|=STARTF_USESTDHANDLES;startup.hStdOutput=output;startup.hStdError=output;}
        std::wstring cmd=L"\""+worker+L"\" "+std::to_wstring(GetCurrentProcessId())+L" \""+name+L"\"";
        PROCESS_INFORMATION pi{};
        BOOL ok=CreateProcessW(worker.c_str(),cmd.data(),nullptr,nullptr,output!=INVALID_HANDLE_VALUE,CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,work.c_str(),&startup,&pi);
        if(output!=INVALID_HANDLE_VALUE) CloseHandle(output);
        if(!ok) return false;
        if(!job) {
            job=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
            info.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if(job) SetInformationJobObject(job,JobObjectExtendedLimitInformation,&info,sizeof(info));
        }
        if(job) AssignProcessToJobObject(job,pi.hProcess);
        process=pi.hProcess;ResumeThread(pi.hThread);CloseHandle(pi.hThread);return true;
    }
    /* Single producer: the tablet dispatch hook already holds the runtime lock. */
    void command(LONG value) {
        if(!shared || value<=MC_NONE || value>=MC_COUNT) return;
        LONG tail=InterlockedCompareExchange(&shared->tail,0,0);
        if(tail-InterlockedCompareExchange(&shared->head,0,0)>=MUSIC_QUEUE) return;
        InterlockedExchange(&shared->queue[tail%MUSIC_QUEUE],value);
        InterlockedExchange(&shared->tail,tail+1);
    }
    void shown() {if(shared) InterlockedExchange(&shared->heartbeat,LONG(GetTickCount()));}
    void blur() {if(shared) InterlockedExchange(&shared->heartbeat,0);}
    bool running() const {return process && WaitForSingleObject(process,0)==WAIT_TIMEOUT;}
    LONG updates() const {return shared?InterlockedCompareExchange(&shared->updates,0,0):0;}
    const MusicSnapshot& snapshot() {
        if(!shared) return cached;
        LONG updated=InterlockedCompareExchange(&shared->updates,0,0);
        if(updated!=cached.updates && WaitForSingleObject(textMutex,0)==WAIT_OBJECT_0) {
            memcpy(cached.title,shared->title,MUSIC_TEXT);
            memcpy(cached.artist,shared->artist,MUSIC_TEXT);
            memcpy(cached.source,shared->source,MUSIC_SOURCE);
            ReleaseMutex(textMutex);
            cached.title[MUSIC_TEXT-1]=cached.artist[MUSIC_TEXT-1]=0;
            cached.source[MUSIC_SOURCE-1]=0;
            cached.updates=updated;
        }
        cached.playback=InterlockedCompareExchange(&shared->playback,0,0);
        cached.position=InterlockedCompareExchange(&shared->position,0,0);
        cached.duration=InterlockedCompareExchange(&shared->duration,0,0);
        cached.volume=InterlockedCompareExchange(&shared->volume,0,0);
        cached.sessions=InterlockedCompareExchange(&shared->sessions,0,0);
        return cached;
    }
};
