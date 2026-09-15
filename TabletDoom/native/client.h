#pragma once
#include "shared.h"
#include <string>
#include <vector>
#include <array>
#include <algorithm>

struct DoomQuad {float rect[4];uint32_t color;};
class DoomClient {
    HANDLE mapping=nullptr,frameMutex=nullptr,process=nullptr,job=nullptr;
    DoomShared* shared=nullptr;
    std::array<uint32_t,DOOM_WIDTH*DOOM_HEIGHT> pixels{};
    bool haveFrame=false;
public:
    bool start() {
        if(process && WaitForSingleObject(process,0)==WAIT_TIMEOUT) return true;
        if(process) {CloseHandle(process);process=nullptr;}
        wchar_t name[128];swprintf_s(name,L"Local\\EchoTabletDoom-%lu",GetCurrentProcessId());
        if(!mapping) mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(DoomShared),name);
        if(!mapping) return false;
        if(!shared) shared=(DoomShared*)MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(DoomShared));
        std::wstring mutexName=std::wstring(name)+L"-frame";
        if(!frameMutex) frameMutex=CreateMutexW(nullptr,FALSE,mutexName.c_str());
        if(!shared || !frameMutex) return false;
        shared->magic=DOOM_SHARED_MAGIC;shared->frames=0;shared->status=0;shared->keys=0;haveFrame=false;
        wchar_t path[32768];GetModuleFileNameW(nullptr,path,32768);
        std::wstring bin(path);bin.resize(bin.find_last_of(L"\\/"));
        auto worker=bin+L"\\doom\\DoomWorker.exe",wad=bin+L"\\doom\\doom1.wad";
        if(GetFileAttributesW(worker.c_str())==INVALID_FILE_ATTRIBUTES || GetFileAttributesW(wad.c_str())==INVALID_FILE_ATTRIBUTES) return false;
        GetEnvironmentVariableW(L"LOCALAPPDATA",path,32768);
        auto work=std::wstring(path)+L"\\EchoTabletTrainer\\Doom";
        if(!CreateDirectoryW(work.c_str(),nullptr) && GetLastError()!=ERROR_ALREADY_EXISTS) return false;
        SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
        HANDLE output=CreateFileW((work+L"\\worker.log").c_str(),GENERIC_WRITE,FILE_SHARE_READ,&sa,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
        if(output!=INVALID_HANDLE_VALUE) {startup.dwFlags|=STARTF_USESTDHANDLES;startup.hStdOutput=output;startup.hStdError=output;}
        std::wstring cmd=L"\""+worker+L"\" "+std::to_wstring(GetCurrentProcessId())+L" \""+name+L"\" \""+wad+L"\"";
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
    void key(unsigned key,bool down) {
        if(!shared || key>=DK_COUNT) return;
        if(down) InterlockedOr(&shared->keys,LONG(1u<<key));
        else InterlockedAnd(&shared->keys,~LONG(1u<<key));
    }
    void blur() {if(shared) {InterlockedExchange(&shared->keys,0);InterlockedExchange(&shared->heartbeat,0);}}
    void shown() {if(shared) InterlockedExchange(&shared->heartbeat,LONG(GetTickCount()));}
    LONG frames() const {return shared?InterlockedCompareExchange(&shared->frames,0,0):0;}
    bool running() const {return process && WaitForSingleObject(process,0)==WAIT_TIMEOUT;}
    void geometry(std::vector<DoomQuad>& out,unsigned budget) {
        if(shared && InterlockedCompareExchange(&shared->frames,0,0)>0 && WaitForSingleObject(frameMutex,0)==WAIT_OBJECT_0) {
            std::copy(std::begin(shared->pixels),std::end(shared->pixels),pixels.begin());
            ReleaseMutex(frameMutex);haveFrame=true;
        }
        const unsigned widths[]={160,128,96,80,64,48,32};
        for(auto width:widths) {
            unsigned height=width*5/8;out.clear();
            for(unsigned y=0;y<height;y++) {
                unsigned begin=0;uint32_t previous=0;
                for(unsigned x=0;x<=width;x++) {
                    uint32_t c=0;
                    if(x<width) {
                        c=haveFrame?pixels[(y*DOOM_HEIGHT/height)*DOOM_WIDTH+x*DOOM_WIDTH/width]:
                            ((x+GetTickCount()/100)%(width)<width/2?0xe08020:0x204080);
                        c=0xff000000|((c>>16)&255)|(c&0xff00)|((c&255)<<16);
                    }
                    if(x && (x==width || c!=previous)) {
                        out.push_back({{151.f+640.f*begin/width,16.f+480.f*y/height,151.f+640.f*x/width,16.f+480.f*(y+1)/height},previous});begin=x;
                    }
                    previous=c;
                }
            }
            if(out.size()<=budget) return;
        }
        out.clear(); // never overrun Echo's shared UI vertex/index budget
    }
};
