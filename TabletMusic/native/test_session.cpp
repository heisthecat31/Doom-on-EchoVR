/* Round-trip check with no real player involved: this process registers its own
   System Media Transport Controls session, then drives the worker and confirms
   the presses arrive back here.

   It only ever sends transport commands once the worker reports *this* session as
   the selected one, so a real player running on the build machine is untouched.
   Volume commands are deliberately not exercised: they would move another app's
   level. */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <systemmediatransportcontrolsinterop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.h>
#include <atomic>
#include <chrono>
#include <cassert>
#include <cstdio>
#include "client.h"

using namespace winrt;
using namespace winrt::Windows::Media;
using Controls=SystemMediaTransportControls;
using Button=SystemMediaTransportControlsButton;

static const char* TITLE="Tablet Music Offline Track";
static const char* ARTIST="Echo Tablet Regression";
static constexpr LONG LENGTH=240,AT=75;
static std::atomic<int> plays{0},pauses{0},nexts{0},previous{0};

/* SMTC obtained through GetForWindow delivers its events on this thread's
   message queue, so every wait has to keep pumping. */
static void pump(MusicClient& client,DWORD milliseconds) {
    DWORD until=GetTickCount()+milliseconds;
    while((LONG)(until-GetTickCount())>0) {
        MSG message;
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&message);DispatchMessageW(&message);}
        client.shown();client.snapshot();
        Sleep(15);
    }
}
static bool waitForUpdate(MusicClient& client,DWORD milliseconds) {
    LONG before=client.updates();
    DWORD until=GetTickCount()+milliseconds;
    while((LONG)(until-GetTickCount())>0) {
        pump(client,60);
        if(client.updates()>before) return true;
    }
    return false;
}

int main() {
    setvbuf(stdout,nullptr,_IONBF,0);
    init_apartment(apartment_type::single_threaded);
    WNDCLASSW cls{};
    cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"EchoTabletMusicTest";
    RegisterClassW(&cls);
    HWND window=CreateWindowExW(0,cls.lpszClassName,L"Echo Tablet Music Test",WS_OVERLAPPED,
        0,0,160,120,nullptr,nullptr,cls.hInstance,nullptr);
    if(!window) {printf("Could not create the test window: %lu\n",GetLastError());return 1;}

    Controls controls{nullptr};
    try {
        auto interop=get_activation_factory<Controls,ISystemMediaTransportControlsInterop>();
        check_hresult(interop->GetForWindow(window,guid_of<Controls>(),put_abi(controls)));
    } catch(hresult_error const& error) {
        printf("Could not register a media session: %ls\n",error.message().c_str());
        return 1;
    }
    controls.ButtonPressed([](Controls const&,SystemMediaTransportControlsButtonPressedEventArgs const& args) {
        switch(args.Button()) {
            case Button::Play: plays++; break;
            case Button::Pause: pauses++; break;
            case Button::Next: nexts++; break;
            case Button::Previous: previous++; break;
            default: break;
        }
    });
    controls.IsPlayEnabled(true);controls.IsPauseEnabled(true);
    controls.IsNextEnabled(true);controls.IsPreviousEnabled(true);
    controls.PlaybackStatus(MediaPlaybackStatus::Playing);
    auto updater=controls.DisplayUpdater();
    updater.Type(MediaPlaybackType::Music);
    updater.MusicProperties().Title(to_hstring(TITLE));
    updater.MusicProperties().Artist(to_hstring(ARTIST));
    updater.Update();
    SystemMediaTransportControlsTimelineProperties timeline;
    timeline.StartTime(std::chrono::seconds{0});
    timeline.MinSeekTime(std::chrono::seconds{0});
    timeline.Position(std::chrono::seconds{AT});
    timeline.MaxSeekTime(std::chrono::seconds{LENGTH});
    timeline.EndTime(std::chrono::seconds{LENGTH});
    controls.UpdateTimelineProperties(timeline);
    DWORD published=GetTickCount();   /* the worker extrapolates from this moment */
    controls.IsEnabled(true);

    MusicClient client;
    if(!client.start()) {printf("Native client launch failed: Windows error %lu\n",GetLastError());return 1;}
    if(!waitForUpdate(client,6000)) {printf("Worker published no snapshot\n");return 1;}

    /* Find our own session, cycling the source the way the tablet chip does. */
    const MusicSnapshot* shown=&client.snapshot();
    int cycles=0,limit=shown->sessions*2+4;
    while(strcmp(shown->title,TITLE)!=0 && cycles<limit) {
        client.command(MC_SOURCE);cycles++;
        waitForUpdate(client,3000);
        pump(client,300);
        shown=&client.snapshot();
    }
    if(strcmp(shown->title,TITLE)!=0) {
        printf("Worker never reported this process's session (saw \"%s\" on %s after %d cycle(s))\n",
            shown->title,shown->source,cycles);
        return 1;
    }
    printf("Found our synthetic session after %d source cycle(s), reported as %s\n",cycles,shown->source);
    assert(strcmp(shown->artist,ARTIST)==0);
    assert(shown->playback==MP_PLAYING);
    assert(shown->duration==LENGTH);
    LONG expected=AT+LONG((GetTickCount()-published)/1000);
    assert(shown->position>=expected-3 && shown->position<=expected+3);
    printf("Metadata round-trip exact: \"%s\" / \"%s\", playing, %ld/%lds\n",
        shown->title,shown->artist,shown->position,shown->duration);

    /* This session publishes its timeline once, exactly like a real player, which
       updates only on play/pause/seek/track change. The worker must carry the
       position forward or the tablet's bar sits still for the whole track. */
    LONG before=shown->position;
    pump(client,4000);
    shown=&client.snapshot();
    LONG advanced=shown->position-before;
    if(advanced<3 || advanced>6) {
        printf("Position advanced %ld s over 4 s of playback with no timeline update\n",advanced);
        return 1;
    }
    printf("Position advances while playing: +%ld s over 4 s, no timeline update\n",advanced);

    /* Paused it must hold still, the way a real player republishes and stops. */
    const LONG HELD=150;
    controls.PlaybackStatus(MediaPlaybackStatus::Paused);
    timeline.Position(std::chrono::seconds{HELD});
    controls.UpdateTimelineProperties(timeline);
    pump(client,1500);
    shown=&client.snapshot();
    assert(shown->playback==MP_PAUSED);
    if(shown->position<HELD-2 || shown->position>HELD+2) {
        printf("Paused position reported %ld s, expected about %ld\n",shown->position,HELD);
        return 1;
    }
    LONG parked=shown->position;
    pump(client,3500);
    shown=&client.snapshot();
    if(shown->position!=parked) {
        printf("Paused position drifted %ld -> %ld\n",parked,shown->position);
        return 1;
    }
    printf("Position holds at %ld s while paused\n",parked);
    controls.PlaybackStatus(MediaPlaybackStatus::Playing);
    pump(client,800);

    /* Transport presses must reach this session's own handler. */
    struct Step {LONG command; std::atomic<int>* seen; const char* name;};
    const Step steps[]={{MC_PLAYPAUSE,&pauses,"PLAY/PAUSE -> Pause"},
                        {MC_NEXT,&nexts,"NEXT -> Next"},
                        {MC_PREV,&previous,"PREV -> Previous"}};
    for(auto& step:steps) {
        int seen=step.seen->load();
        client.command(step.command);
        DWORD until=GetTickCount()+5000;
        while(step.seen->load()==seen && (LONG)(until-GetTickCount())>0) pump(client,50);
        if(step.seen->load()==seen) {printf("No callback for %s\n",step.name);return 1;}
        printf("  %s delivered\n",step.name);
    }
    assert(plays.load()==0); /* a Playing session must be told to pause, not play */
    printf("PASS: worker enumerates this session, reports its metadata exactly and delivers "
           "play/pause, next and previous back to the owning app.\n");
    return 0;
}
