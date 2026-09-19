/* MusicWorker: bridges Windows' System Media Transport Controls to the tablet.
   Runs hidden in its own process so every WinRT/COM call stays out of Echo. */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
#include <cwctype>
#include <chrono>
#include "shared.h"

using namespace winrt;
using namespace winrt::Windows::Media::Control;
using Manager=GlobalSystemMediaTransportControlsSessionManager;
using Session=GlobalSystemMediaTransportControlsSession;
using Status=GlobalSystemMediaTransportControlsSessionPlaybackStatus;

static MusicShared* block=nullptr;
static HANDLE textMutex=nullptr,parent=nullptr;
static DWORD parentId=0;
static std::wstring selectedId;

static std::string utf8(hstring const& value) {
    if(value.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,0,value.c_str(),int(value.size()),nullptr,0,nullptr,nullptr);
    std::string out(size_t(n<0?0:n),'\0');
    if(n>0) WideCharToMultiByte(CP_UTF8,0,value.c_str(),int(value.size()),out.data(),n,nullptr,nullptr);
    return out;
}
static std::wstring lower(std::wstring value) {
    std::transform(value.begin(),value.end(),value.begin(),[](wchar_t c){return wchar_t(towlower(c));});
    return value;
}
/* A Win32 app registers its own image name as the model id; a packaged app
   registers "<family>!<entry>". Both forms are reduced to one match key. */
static std::wstring volumeKey(std::wstring const& modelId) {
    auto bang=modelId.find(L'!');
    if(bang==std::wstring::npos) return lower(modelId);
    auto family=modelId.substr(0,bang);
    auto underscore=family.find(L'_');
    return lower(underscore==std::wstring::npos?family:family.substr(0,underscore));
}
static bool packaged(std::wstring const& modelId) {return modelId.find(L'!')!=std::wstring::npos;}
static std::string friendlyName(std::wstring const& modelId) {
    struct Known {const wchar_t* key; const char* label;};
    static const Known known[]={
        {L"spotify",            "SPOTIFY"},
        {L"appleinc.applemusic","APPLE MUSIC"},
        {L"applemusic",         "APPLE MUSIC"},
        {L"itunes",             "APPLE MUSIC"},
        {L"youtube music",      "YT MUSIC"},
        {L"youtube_music",      "YT MUSIC"},
        {L"th-ch.youtube",      "YT MUSIC"},
        {L"chrome",             "CHROME"},
        {L"msedge",             "EDGE"},
        {L"firefox",            "FIREFOX"},
        {L"brave",              "BRAVE"},
        {L"opera",              "OPERA"},
        {L"tidal",              "TIDAL"},
        {L"deezer",             "DEEZER"},
        {L"vlc",                "VLC"},
        {L"foobar2000",         "FOOBAR2000"},
    };
    auto id=lower(modelId);
    for(auto& entry:known) if(id.find(entry.key)!=std::wstring::npos) return entry.label;
    /* Fall back to the bare model id: strip the path, extension and entry point.
       Only a packaged id carries a "_<publisher hash>" suffix worth removing; an
       exe name may legitimately contain an underscore. */
    bool fromPackage=packaged(id);
    auto slash=id.find_last_of(L"\\/");
    if(slash!=std::wstring::npos) id=id.substr(slash+1);
    auto bang=id.find(L'!');
    if(bang!=std::wstring::npos) id=id.substr(0,bang);
    if(fromPackage) {
        auto underscore=id.find(L'_');
        if(underscore!=std::wstring::npos) id=id.substr(0,underscore);
    }
    if(id.size()>4 && id.compare(id.size()-4,4,L".exe")==0) id.resize(id.size()-4);
    std::string out;
    for(wchar_t c:id) out+=char(c<128?towupper(c):'?');
    return out.empty()?"PLAYER":out;
}

static std::wstring processName(DWORD pid) {
    if(!pid) return {};
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(!process) return {};
    wchar_t buffer[MAX_PATH];DWORD n=MAX_PATH;std::wstring name;
    if(QueryFullProcessImageNameW(process,0,buffer,&n)) {
        std::wstring path(buffer,n);
        auto slash=path.find_last_of(L"\\/");
        name=lower(slash==std::wstring::npos?path:path.substr(slash+1));
    }
    CloseHandle(process);
    return name;
}
/* delta 0 only reads. Returns 0..100, or -1 when no audio session matched. */
static LONG volumeControl(std::wstring const& modelId,float delta) {
    com_ptr<IMMDeviceEnumerator> devices;
    if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),devices.put_void()))) return -1;
    com_ptr<IMMDevice> endpoint;
    if(FAILED(devices->GetDefaultAudioEndpoint(eRender,eMultimedia,endpoint.put()))) return -1;
    com_ptr<IAudioSessionManager2> manager;
    if(FAILED(endpoint->Activate(__uuidof(IAudioSessionManager2),CLSCTX_ALL,nullptr,manager.put_void()))) return -1;
    com_ptr<IAudioSessionEnumerator> sessions;
    if(FAILED(manager->GetSessionEnumerator(sessions.put()))) return -1;
    int count=0;
    if(FAILED(sessions->GetCount(&count))) return -1;
    auto key=volumeKey(modelId);
    bool byPackage=packaged(modelId);
    LONG result=-1;
    /* Pass 0 matches the reported player; pass 1 accepts any other process that
       is actively rendering audio, so an unmatched player still responds. */
    for(int pass=0;pass<2 && result<0;pass++) {
        for(int i=0;i<count;i++) {
            com_ptr<IAudioSessionControl> control;
            if(FAILED(sessions->GetSession(i,control.put()))) continue;
            auto control2=control.try_as<IAudioSessionControl2>();
            if(!control2 || control2->IsSystemSoundsSession()==S_OK) continue;
            DWORD pid=0;
            if(FAILED(control2->GetProcessId(&pid)) || !pid || pid==parentId || pid==GetCurrentProcessId()) continue;
            bool match=false;
            if(pass==0) {
                if(byPackage) {
                    LPWSTR identifier=nullptr;
                    if(SUCCEEDED(control2->GetSessionIdentifier(&identifier)) && identifier) {
                        match=lower(identifier).find(key)!=std::wstring::npos;
                        CoTaskMemFree(identifier);
                    }
                    if(!match) match=processName(pid).find(key)!=std::wstring::npos;
                } else match=processName(pid)==key;
            } else {
                AudioSessionState state=AudioSessionStateInactive;
                match=SUCCEEDED(control->GetState(&state)) && state==AudioSessionStateActive;
            }
            if(!match) continue;
            auto volume=control.try_as<ISimpleAudioVolume>();
            if(!volume) continue;
            float level=0;
            if(FAILED(volume->GetMasterVolume(&level))) continue;
            if(delta!=0) {
                level=std::min(1.f,std::max(0.f,level+delta));
                volume->SetMasterVolume(level,nullptr);
            }
            result=LONG(level*100.f+.5f);
        }
    }
    return result;
}

static void publishText(std::string const& title,std::string const& artist,std::string const& source) {
    if(WaitForSingleObject(textMutex,50)!=WAIT_OBJECT_0) return;
    auto copy=[](char* out,size_t capacity,std::string const& in) {
        size_t n=in.size()<capacity-1?in.size():capacity-1;
        memcpy(out,in.data(),n);memset(out+n,0,capacity-n);
    };
    copy(block->title,MUSIC_TEXT,title);
    copy(block->artist,MUSIC_TEXT,artist);
    copy(block->source,MUSIC_SOURCE,source);
    ReleaseMutex(textMutex);
}

static std::vector<Session> listSessions(Manager const& manager) {
    std::vector<Session> out;
    try {for(auto const& session:manager.GetSessions()) out.push_back(session);} catch(...) {}
    return out;
}
/* Keep showing the same player across polls; fall back to whatever is playing. */
static int pickSession(std::vector<Session> const& sessions,Manager const& manager) {
    if(sessions.empty()) return -1;
    if(!selectedId.empty())
        for(size_t i=0;i<sessions.size();i++)
            try {if(std::wstring(sessions[i].SourceAppUserModelId())==selectedId) return int(i);} catch(...) {}
    for(size_t i=0;i<sessions.size();i++)
        try {if(sessions[i].GetPlaybackInfo().PlaybackStatus()==Status::Playing) return int(i);} catch(...) {}
    try {
        auto current=manager.GetCurrentSession();
        if(current) {
            std::wstring id(current.SourceAppUserModelId());
            for(size_t i=0;i<sessions.size();i++)
                try {if(std::wstring(sessions[i].SourceAppUserModelId())==id) return int(i);} catch(...) {}
        }
    } catch(...) {}
    return 0;
}

static void poll(Manager const& manager) {
    auto sessions=listSessions(manager);
    int index=pickSession(sessions,manager);
    InterlockedExchange(&block->sessions,LONG(sessions.size()));
    InterlockedExchange(&block->selected,index);
    if(index<0) {
        publishText("","","NO PLAYER");
        InterlockedExchange(&block->playback,MP_UNKNOWN);
        InterlockedExchange(&block->position,-1);
        InterlockedExchange(&block->duration,-1);
        InterlockedExchange(&block->volume,-1);
        InterlockedIncrement(&block->updates);
        selectedId.clear();
        return;
    }
    auto session=sessions[size_t(index)];
    std::wstring modelId;
    try {modelId=session.SourceAppUserModelId();} catch(...) {}
    selectedId=modelId;
    std::string title,artist;
    try {
        auto properties=session.TryGetMediaPropertiesAsync().get();
        title=utf8(properties.Title());
        artist=utf8(properties.Artist());
        if(artist.empty()) artist=utf8(properties.AlbumTitle());
    } catch(...) {}
    LONG playback=MP_UNKNOWN;
    try {
        auto status=session.GetPlaybackInfo().PlaybackStatus();
        playback=status==Status::Playing?MP_PLAYING:status==Status::Paused?MP_PAUSED:MP_UNKNOWN;
    } catch(...) {}
    LONG position=-1,duration=-1;
    try {
        auto timeline=session.GetTimelineProperties();
        auto seconds=[](auto span){return LONG(span.count()/10000000LL);};
        LONG end=seconds(timeline.EndTime()),start=seconds(timeline.StartTime());
        LONG at=seconds(timeline.Position());
        if(end>start) {
            duration=end-start;
            LONG offset=at-start;
            /* Players push timeline updates only on play/pause/seek/track change,
               never per second, so Position() is a snapshot. Carry it forward from
               the moment it was published or the bar would sit still while playing. */
            if(playback==MP_PLAYING) {
                auto updated=timeline.LastUpdatedTime();
                if(updated.time_since_epoch().count()>0) {
                    LONG since=LONG(std::chrono::duration_cast<std::chrono::seconds>(
                        winrt::clock::now()-updated).count());
                    if(since>0 && since<12*3600) offset+=since;
                }
            }
            position=std::min(duration,std::max(0L,offset));
        }
    } catch(...) {}
    publishText(title,artist,friendlyName(modelId));
    InterlockedExchange(&block->playback,playback);
    InterlockedExchange(&block->position,position);
    InterlockedExchange(&block->duration,duration);
    InterlockedExchange(&block->volume,volumeControl(modelId,0));
    InterlockedIncrement(&block->updates);
}

static void apply(Manager const& manager,LONG command) {
    auto sessions=listSessions(manager);
    int index=pickSession(sessions,manager);
    if(command==MC_SOURCE) {
        if(sessions.size()>1) {
            int next=(index<0?0:index+1)%int(sessions.size());
            try {selectedId=sessions[size_t(next)].SourceAppUserModelId();} catch(...) {}
        }
        return;
    }
    if(index<0) return;
    auto session=sessions[size_t(index)];
    try {
        switch(command) {
            case MC_PLAYPAUSE: session.TryTogglePlayPauseAsync().get(); break;
            case MC_NEXT:      session.TrySkipNextAsync().get(); break;
            case MC_PREV:      session.TrySkipPreviousAsync().get(); break;
            case MC_VOLUP:
            case MC_VOLDOWN: {
                std::wstring modelId(session.SourceAppUserModelId());
                InterlockedExchange(&block->volume,volumeControl(modelId,command==MC_VOLUP?.05f:-.05f));
                break;
            }
            default: break;
        }
    } catch(...) {}
}

int main(int argc,char** argv) {
    if(argc!=3) {fprintf(stderr,"Usage: MusicWorker parent-pid mapping-name\n");return 2;}
    parentId=strtoul(argv[1],nullptr,10);
    parent=OpenProcess(SYNCHRONIZE,FALSE,parentId);
    HANDLE mapping=OpenFileMappingA(FILE_MAP_ALL_ACCESS,FALSE,argv[2]);
    char mutexName[256];snprintf(mutexName,sizeof(mutexName),"%s-text",argv[2]);
    textMutex=OpenMutexA(SYNCHRONIZE,FALSE,mutexName);
    if(!parent || !mapping || !textMutex) {fprintf(stderr,"Shared state unavailable\n");return 3;}
    block=(MusicShared*)MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(MusicShared));
    if(!block || block->magic!=MUSIC_SHARED_MAGIC) {fprintf(stderr,"Bad shared block\n");return 4;}
    init_apartment(apartment_type::multi_threaded);
    Manager manager{nullptr};
    try {manager=Manager::RequestAsync().get();} catch(...) {}
    if(!manager) {
        fprintf(stderr,"Media transport controls unavailable\n");
        InterlockedExchange(&block->status,2);return 5;
    }
    InterlockedExchange(&block->status,1);
    printf("MusicWorker ready for parent %lu\n",parentId);fflush(stdout);
    while(WaitForSingleObject(parent,0)==WAIT_TIMEOUT) {
        for(int guard=0;guard<MUSIC_QUEUE;guard++) {
            LONG head=InterlockedCompareExchange(&block->head,0,0);
            if(head==InterlockedCompareExchange(&block->tail,0,0)) break;
            LONG command=InterlockedExchange(&block->queue[head%MUSIC_QUEUE],MC_NONE);
            InterlockedExchange(&block->head,head+1);
            if(command>MC_NONE && command<MC_COUNT) apply(manager,command);
        }
        try {poll(manager);} catch(...) {}
        /* The tablet stamps heartbeat while the page draws; idle far slower
           otherwise, but in slices so opening the page refreshes it promptly. */
        auto fresh=[]{LONG beat=InterlockedCompareExchange(&block->heartbeat,0,0);
                      return beat!=0 && LONG(GetTickCount())-beat<2000;};
        bool visible=fresh();
        for(int slept=0,total=visible?250:1500;slept<total;slept+=50) {
            Sleep(50);
            if(!visible && fresh()) break;
        }
    }
    InterlockedExchange(&block->status,2);
    return 0;
}
