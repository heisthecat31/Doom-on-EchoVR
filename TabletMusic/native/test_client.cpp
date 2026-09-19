/* Offline check of the native music client: launches the real worker, reads a
   real Windows media snapshot and confirms the queue and idle behaviour. Passes
   whether or not a player happens to be running on the build machine. */
#include "client.h"
#include <cassert>
#include <cstdio>

static const MusicSnapshot& settle(MusicClient& client,DWORD milliseconds) {
    DWORD until=GetTickCount()+milliseconds;
    while((LONG)(until-GetTickCount())>0) {client.shown();client.snapshot();Sleep(20);}
    return client.snapshot();
}

/* Every byte of a complete UTF-8 string, ignoring a trailing ".." marker. */
static bool validUtf8(const char* s) {
    size_t n=strlen(s);
    if(n>=2 && s[n-1]=='.' && s[n-2]=='.') n-=2;
    for(size_t i=0;i<n;) {
        unsigned char c=(unsigned char)s[i];
        size_t len=c<0x80?1:(c&0xe0)==0xc0?2:(c&0xf0)==0xe0?3:(c&0xf8)==0xf0?4:0;
        if(!len || i+len>n) return false;
        for(size_t k=1;k<len;k++) if(((unsigned char)s[i+k]&0xc0)!=0x80) return false;
        i+=len;
    }
    return true;
}
/* The label fitting and progress maths the tablet runs every frame. */
static void checkFormatting() {
    char out[MUSIC_TEXT+4];
    musicClamp(out,sizeof(out),64,38,"Short Title");
    assert(strcmp(out,"Short Title")==0);
    musicClamp(out,sizeof(out),64,38,"");
    assert(out[0]==0);
    musicClamp(out,sizeof(out),64,38,nullptr);
    assert(out[0]==0);
    /* Cut by the display limit; the marker counts toward that limit. */
    const char* wide="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnop";
    musicClamp(out,sizeof(out),128,38,wide);
    assert(strlen(out)==38 && strncmp(out,wide,36)==0 && strcmp(out+36,"..")==0);
    /* Cut by the element's reserved capacity even when display allows more. */
    musicClamp(out,sizeof(out),16,120,wide);
    assert(strlen(out)==15 && strncmp(out,wide,13)==0 && strcmp(out+13,"..")==0);
    /* A long title must never exceed what the canvas slot reserved. */
    char huge[512]; memset(huge,'x',sizeof(huge)-1); huge[sizeof(huge)-1]=0;
    for(unsigned capacity=4;capacity<=MUSIC_TEXT;capacity++) {
        musicClamp(out,sizeof(out),capacity,1000,huge);
        assert(strlen(out)<capacity);
    }
    /* Truncation must not split a multi-byte sequence: "é" is two bytes. */
    musicClamp(out,sizeof(out),128,5,"abcd\xc3\xa9zz");
    assert(strcmp(out,"abc..")==0);
    musicClamp(out,sizeof(out),128,6,"abcd\xc3\xa9zz");
    assert(strcmp(out,"abcd..")==0);
    /* 7 bytes cannot hold "abcd" + a two-byte "é" + the marker, so the é goes. */
    musicClamp(out,sizeof(out),128,7,"abcd\xc3\xa9zz");
    assert(strcmp(out,"abcd..")==0);
    /* 8 bytes fit the whole string, so nothing is cut and no marker is added. */
    musicClamp(out,sizeof(out),128,8,"abcd\xc3\xa9zz");
    assert(strcmp(out,"abcd\xc3\xa9zz")==0);
    /* No cut, at any limit, may leave a partial sequence behind. */
    for(unsigned n=1;n<=16;n++) {
        musicClamp(out,sizeof(out),128,n,"\xe2\x99\xaa ab\xf0\x9f\x8e\xb5 \xc3\xa9z");
        assert(strlen(out)<=n && validUtf8(out));
    }
    assert(musicFilled(0,240,24)==0);
    assert(musicFilled(240,240,24)==24);
    assert(musicFilled(120,240,24)==12);
    assert(musicFilled(-1,240,24)==0);
    assert(musicFilled(60,0,24)==0);
    assert(musicFilled(60,-1,24)==0);
    assert(musicFilled(9999,240,24)==24); /* player reports past the end */
    printf("Label fitting and progress maths behave at every boundary\n");
}

int main() {
    setvbuf(stdout,nullptr,_IONBF,0); /* assert() aborts without flushing */
    checkFormatting();
    MusicClient client;
    /* Before any worker exists the snapshot must still be safe to render. */
    auto& empty=client.snapshot();
    assert(empty.updates==0 && empty.playback==MP_UNKNOWN && empty.volume==-1);
    if(!client.start()) {printf("Native client launch failed: Windows error %lu\n",GetLastError());return 1;}
    auto& first=settle(client,4000);
    if(client.updates()==0) {printf("Worker published no snapshot; see %%LOCALAPPDATA%%\\EchoTabletTrainer\\Music\\worker.log\n");return 1;}
    assert(client.running());
    assert(strlen(first.source)>0 && strlen(first.source)<MUSIC_SOURCE);
    assert(strlen(first.title)<MUSIC_TEXT && strlen(first.artist)<MUSIC_TEXT);
    assert(first.sessions>=0);
    assert(first.playback==MP_UNKNOWN || first.playback==MP_PLAYING || first.playback==MP_PAUSED);
    assert(first.duration<0 || (first.position>=0 && first.position<=first.duration));
    assert(first.volume>=-1 && first.volume<=100);
    printf("Windows reports %ld media session(s); showing \"%s\" by \"%s\" on %s (state %ld, %ld/%lds, volume %ld)\n",
        first.sessions,first.title,first.artist,first.source,first.playback,first.position,first.duration,first.volume);

    /* A full burst of presses must neither block nor overrun the ring. */
    for(int i=0;i<MUSIC_QUEUE*3;i++) client.command(MC_SOURCE);
    client.command(0);client.command(MC_COUNT);client.command(-5);
    LONG before=client.updates();
    settle(client,2000);
    assert(client.running() && client.updates()>before);
    printf("Queue burst drained with the worker still alive after %ld updates\n",client.updates()-before);

    /* Leaving the page must idle the worker instead of polling at page rate. */
    client.blur();Sleep(2500);
    LONG idleStart=client.updates();Sleep(1000);
    LONG idlePolls=client.updates()-idleStart;
    assert(idlePolls<=2);
    client.shown();Sleep(800);
    assert(client.updates()>idleStart+idlePolls);
    printf("PASS: worker starts, publishes a bounded snapshot, survives a queue burst and idles when the page is hidden (%ld poll(s) per idle second).\n",idlePolls);
    return 0;
}
