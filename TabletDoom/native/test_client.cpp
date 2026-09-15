#include "client.h"
#include <cassert>
#include <cstdio>
int main() {
    DoomClient client;
    std::vector<DoomQuad> quads;
    for(unsigned budget:{0u,32u,512u,4096u,16000u}) {
        client.geometry(quads,budget);assert(quads.size()<=budget);
        for(auto& q:quads) {
            assert(q.rect[0]>=151 && q.rect[2]<=791 && q.rect[1]>=16 && q.rect[3]<=496);
            assert(q.rect[0]<q.rect[2] && q.rect[1]<q.rect[3]);
            assert((q.color>>24)==255);
        }
    }
    if(!client.start()) {printf("Native client launch failed: Windows error %lu\n",GetLastError());return 1;}
    DWORD until=GetTickCount()+2000;
    while((LONG)(until-GetTickCount())>0) {client.shown();Sleep(20);}
    assert(client.frames()>5);
    client.geometry(quads,4096);assert(quads.size()>100 && quads.size()<=4096);
    for(auto& q:quads) assert(q.rect[0]>=151 && q.rect[2]<=791 && q.rect[1]>=16 && q.rect[3]<=496);
    client.blur();Sleep(400);LONG before=client.frames();Sleep(200);assert(client.frames()==before);
    printf("PASS: native client starts Doom, reads real frames, confines pixels to page, honors draw budget and pauses.\n");
}
