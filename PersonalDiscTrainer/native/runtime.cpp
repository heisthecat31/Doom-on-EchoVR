#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <array>
#include <map>
#include <set>
#include <vector>
#include <mutex>
#include <atomic>
#include <share.h>
#include <algorithm>
#include "vendor/minhook/include/MinHook.h"
#include "policy.h"
#include "cut_redirect.h"
#include "../../TabletDoom/native/client.h"

using U=uint64_t; using P=unsigned char*;
template<class T> T& at(void* p,size_t n) { return *reinterpret_cast<T*>(static_cast<P>(p)+n); }
template<class T> T fn(void* p,size_t n) { return reinterpret_cast<T>(static_cast<P>(p)+n); }
template<class T> T virt(void* p,size_t n) { return at<T>(at<void*>(p,0),n); }
constexpr U NONE=~U(0), TAB=0x998a2aa21c569965, GOALIE=0xaca450182e7e67b1, DISC=0x3f3f0d98078a6755;
#include "generated/question_tab.h"
constexpr U PRESS=0xfdb213d9dc5e4826, RELEASE=0x0d7ded077f321f69b;
static P exe;
static std::recursive_mutex mutex;
static std::atomic<bool> fault{false}, discOn{true};
static bool initialized=false;
static Session session;
static const char* reason="ARENA ONLY | 60 SECOND SESSIONS";
static FILE* logfile=nullptr;
static void log(const char* message) {
    if (!logfile) {
        wchar_t p[32768]; GetEnvironmentVariableW(L"LOCALAPPDATA",p,32768);
        std::wstring path(p); path+=L"\\EchoTabletTrainer"; CreateDirectoryW(path.c_str(),nullptr);
        path+=L"\\trainer.log"; logfile=_wfsopen(path.c_str(),L"a",_SH_DENYNO);
    }
    if (logfile) { fprintf(logfile,"%llu %s\n",GetTickCount64(),message); fflush(logfile); }
}
static void fail(const char* message) { fault=true; session.stop(); reason="TRAINER ERROR: SEE trainer.log"; log(message); }

struct Shot { Vec start, velocity; float seconds; unsigned bounces; };
struct CutShot { Vec start,point,target; float speed; };
#include "generated/shots.h"
#include "generated/cuts.h"
#include "generated/bindings.h"

using Show=U(*)(void*,unsigned,int);
using Text=U(*)(void*,unsigned,const char*);
using Alpha=U(*)(void*,float);
using Mask=void(*)(void*,U,unsigned short);
using Dispatch=void(*)(void*,U,U,U,int);
static Show show; static Text text; static Alpha alpha;
static Mask enable,disable; static Dispatch dispatchOriginal;
using CanvasCall=U(*)(void*,U,U,U);
using UnloadCall=void(*)(void*,U,U,U);
static CanvasCall loadedOriginal; static UnloadCall unloadOriginal;
static void(*buttonOriginal)(void*);
static DoomClient doom;
static std::atomic<void*> doomRootCanvas{nullptr},doomPageCanvas{nullptr};
static std::atomic<bool> doomSelected{false};
static void(*canvasRenderOriginal)(void*);
static void(*elementsRenderOriginal)(void*,void*,void*);
static thread_local std::vector<DoomQuad> doomQuads;
static thread_local bool doomDrawing=false;
static int doomKey(U name) {for(unsigned i=0;i<DK_COUNT;i++) if(DOOM_KEYS[i]==name) return int(i);return -1;}

struct View { void* p=nullptr; bool saved=false; float opacity=0; std::array<bool,3> visible{}; };
enum class Page { Stock,Tools,Doom };
struct Context { Page page=Page::Stock; std::array<View,7> views{}; std::set<unsigned> masked; };
static std::map<void*,Context> contexts;
static std::map<void*,std::pair<void*,unsigned>> canvases;
static std::set<void*> pending;
static std::set<std::array<U,3>> held;
static const U content[]={0x5071f3ac46630320,0x645c2234c519a275,0x645c2234c519a276,0x645c2234c519a277,
    0x6567539d25efbdc0,0xfc4ee470b38fb49d,0xfc4ee470b38fb49e,0xfc4ee470b38fb49f,
    0x2755735736742790,0x2755735736742791,0x2755735736742792,0x2755735736742793,
    0x2755735736742794,0x2755735736742795,0x2755735736742796,0x2755735736742797};
static bool stock(U n) { return n>=0x275876572b742791 && n<=0x275876572b742794; }
static bool isContent(U n) { return std::find(std::begin(content),std::end(content),n)!=std::end(content); }
static void remember(void* p,unsigned depth=0) {
    if (!p || depth>8 || canvases.count(p)) return;
    U n=at<U>(p,0xc0); auto elements=at<P>(p,0x90);
    const unsigned counts[]={11,11,8,24,59,7,DOOM_ELEMENTS}, indices[]={7,9,1,0,0,0,1};
    const U markers[]={0xe1c7bcf6b87ef759,0xcca1f4ca7df27d97,0x2e30b3b1c63a9307,
        0x41d2cf3808220b1a,0x2fd5888f5f7a5286,0xf79f5459a0f0cf34,DOOM_MARKER};
    for(unsigned kind=0;kind<7;kind++) {
        if(n!=counts[kind] || !elements || at<U>(elements,indices[kind]*224)!=markers[kind]) continue;
        auto cs=at<void*>(p,0x370);
        if (!cs) { pending.insert(p); return; }
        pending.erase(p);
        auto gs=at<void*>(cs,0x80);
        contexts[gs].views[kind]=View{p}; canvases[p]={gs,kind};
        if(kind==0) doomRootCanvas=p;
        if(kind==6) doomPageCanvas=p;
        char line[120]; sprintf_s(line,"canvas kind=%u gs=%p ptr=%p",kind,gs,p); log(line);
        remember(at<void*>(p,0x378),depth+1);
        return;
    }
}
static void render(Context& c) {
    bool active=c.page!=Page::Stock && !fault;
    if(c.views[0].p==doomRootCanvas.load()) doomSelected=active && c.page==Page::Doom;
    for(unsigned i=3;i<6;i++) {
        auto& v=c.views[i]; if(!v.p) continue;
        if(active) { if(!v.saved) {v.opacity=at<float>(v.p,0x358);v.saved=true;} alpha(v.p,0); }
        else if(v.saved) {alpha(v.p,v.opacity);v.saved=false;}
    }
    auto& root=c.views[0];
    if(root.p) {
        const unsigned hide[]={0,1,3};
        if(active && !root.saved) {
            auto rows=at<P>(root.p,0x90);
            for(unsigned i=0;i<3;i++) root.visible[i]=at<unsigned>(rows,hide[i]*224+0x10)==0;
            root.saved=true;
        }
        if(active) for(auto i:hide) show(root.p,i,0);
        else if(root.saved) {for(unsigned i=0;i<3;i++) show(root.p,hide[i],root.visible[i]);root.saved=false;}
        show(root.p,7,active && c.page==Page::Tools);
        for(unsigned i=8;i<=9;i++) show(root.p,i,active);
        show(root.p,10,active && c.page==Page::Doom);
        text(root.p,9,c.page==Page::Doom?"DOOM":"TOOLS");
    }
    if(c.views[1].p) text(c.views[1].p,9,active && c.page==Page::Tools?"TOOLS*":"TOOLS");
    if(active && c.page==Page::Doom && c.views[6].p)
        text(c.views[6].p,3,!doom.running()?"DOOM STOPPED | PRESS ? TO RESTART":doom.frames()>0?"ARROWS | CTRL | SPACE | ENTER | ESC":"STARTING DOOM...");
    if(c.views[2].p) {
        char line[64];
        if(session.active) sprintf_s(line,"GOALIE TRAINER: ON (%us)",session.seconds(GetTickCount64()));
        else strcpy_s(line,"GOALIE TRAINER: OFF");
        text(c.views[2].p,4,line);
        text(c.views[2].p,6,discOn?"PERSONAL DISC: ON":"PERSONAL DISC: OFF");
        text(c.views[2].p,7,reason);
    }
}
struct Button {U name; unsigned handle; unsigned short reasons;};
static void gate(Context& c,void* cs) {
    auto resource=at<void*>(cs,0xd0); if(!resource) return;
    auto rows=at<P>(resource,0); U count=at<U>(resource,0x30);
    unsigned n=at<unsigned short>(cs,0x10a);
    if(count>8192 || n>count) {fail("Unexpected native button pool layout");return;}
    std::vector<Button> buttons;
    auto instances=at<P>(cs,0x110), inverse=at<P>(cs,0xf0), handles=at<P>(cs,0xe0), masks=at<P>(cs,0xf8);
    for(unsigned i=0;i<n;i++) {
        unsigned row=at<unsigned short>(instances,i*400);
        if(row>=count) {fail("Invalid button row");return;}
        U name=at<U>(rows,row*296);
        if(name!=TAB && name!=QUESTION && name!=GOALIE && name!=DISC && doomKey(name)<0 && !isContent(name)) continue;
        if(!isContent(name) && at<U>(rows,row*296+8)!=0x6c1f6ff04e070923) continue;
        unsigned slot=at<unsigned short>(inverse,i*2);
        if(slot>=count) {fail("Invalid button handle slot");return;}
        unsigned h=slot|(unsigned(at<unsigned short>(handles,slot*4+2))<<16);
        buttons.push_back({name,h,at<unsigned short>(masks,i*2)});
    }
    std::set<unsigned> live; for(auto b:buttons) live.insert(b.handle);
    for(auto it=c.masked.begin();it!=c.masked.end();) if(!live.count(*it)) it=c.masked.erase(it); else ++it;
    for(auto b:buttons) {
        bool hide=!fault && (doomKey(b.name)>=0?c.page!=Page::Doom:(b.name==GOALIE || b.name==DISC)?c.page!=Page::Tools:
            isContent(b.name)?c.page!=Page::Stock:!(c.views[0].p && c.views[1].p && c.views[b.name==QUESTION?6:2].p));
        if(hide && !c.masked.count(b.handle)) {
            if(b.reasons&0x8000) {fail("Button disable bit 0x8000 already owned");return;}
            disable(cs,b.handle,0x8000);c.masked.insert(b.handle);
        } else if(!hide && c.masked.count(b.handle)) {enable(cs,b.handle,0x8000);c.masked.erase(b.handle);}
    }
}

using ScriptUpdate=int(*)(void*); using ScriptDestroy=void(*)(void*);
using Allowed=int(*)(U*);
struct Script {P module=nullptr; ScriptUpdate update=nullptr; ScriptDestroy destroy=nullptr; Allowed allowed=nullptr;};
static Script scripts[3];
using Getter=void(*)(void*,unsigned,void*);
static Getter arenaGetter=nullptr;
static unsigned arenaSlot=0;
static std::set<void*> controllers, discs;
static void* trainingController=nullptr;
static void* trainingDisc=nullptr;
static U phaseUntil=0;
static unsigned phase=0,shotIndex=0;
static bool cutShot=false;
static unsigned cutIndex=0;
static CutRedirect redirect;
static unsigned randomState=0x147381ab;
static unsigned random32() {randomState^=randomState<<13;randomState^=randomState>>17;randomState^=randomState<<5;return randomState;}
static void* gamespace(void* ctx) {
    auto csref=at<void*>(ctx,0x30);
    auto cs=csref?at<void*>(csref,0):nullptr;
    return cs?at<void*>(cs,0x80):nullptr;
}
static std::array<U,28> nodeFor(void* ctx) {
    std::array<U,28> n{}; n[0]=reinterpret_cast<U>(ctx);
    n[4]=n[5]=n[6]=at<U>(ctx,0x58);
    n[8]=n[9]=NONE; n[0x17]=at<U>(ctx,0x48); n[0x18]=at<U>(ctx,0x50);n[0x1a]=NONE;
    return n;
}
static void emptyInput(void*,U,unsigned,void*) {}
// Use the v3 guard's already registered expression, not guessed game variables.
static std::pair<bool,bool> matchKind(void* ctx) {
    auto m=scripts[0].module;
    auto expr=at<void(*)(void*,void*,void*,void*)>(m,0xa090);
    auto getter=at<void(*)(void*,unsigned,void*)>(m,0xa0b8);
    if(!expr || !getter || !arenaGetter) return {true,false};
    auto n=nodeFor(ctx); U input[49]={},scratch[100]={}; unsigned char social=1,arena=0;
    input[0]=reinterpret_cast<U>(n.data());
    expr(n.data(),scratch,input,reinterpret_cast<void*>(emptyInput));
    getter(scratch,at<unsigned>(m,0xa110),&social);
    arenaGetter(scratch,arenaSlot,&arena);
    return {social!=0,arena!=0};
}
static bool eligible(void* ctx) {
    if(fault || !discOn || !scripts[0].allowed || !scripts[1].allowed || !ctx || !controllers.count(ctx)) return false;
    auto gs=gamespace(ctx); if(!gs) return false;
    // Ask the current network match, not a script's gamespace. Pooled personal
    // discs can retain a lobby-arena template gamespace in a full arena match.
    auto kind=matchKind(ctx); if(kind.first || !kind.second) return false;
    auto n=nodeFor(ctx);
    return scripts[0].allowed(n.data())!=0;
}
static void stop(const char* why) {
    if(session.active || trainingController) log(why);
    session.stop(); phase=0;trainingDisc=nullptr;trainingController=nullptr;
    redirect.armed=false;
    reason=why;
}
static int controllerAllowed(U* n) { return discOn && !fault && scripts[1].allowed ? scripts[0].allowed(n):0; }
static int discAllowed(U* n) { return discOn && !fault && scripts[0].allowed ? scripts[1].allowed(n):0; }

struct Body {void* p=nullptr; void* touch=nullptr; unsigned touchHandle=0;};
static Body bodyFor(void* ctx) {
    auto state=at<P>(ctx,0x68); auto gs=gamespace(ctx);
    if(!gs || !state || !at<unsigned char>(state,0xa7)) return {};
    U self=at<U>(state,0xb8); if(self==NONE) return {};
    auto physics=at<void*>(gs,0xc78); if(!physics) return {};
    unsigned handle=0xffffffff;
    virt<void*(*)(void*,unsigned*,U,U)>(physics,0x1a8)(physics,&handle,self,NONE);
    if(!virt<int(*)(void*,unsigned)>(physics,0x1c8)(physics,handle)) return {};
    U h[3]={NONE,NONE,NONE};
    fn<void*(*)(void*,void*,unsigned short)>(exe,0x44cc40)(physics,h,(unsigned short)handle);
    auto body=fn<void*(*)(void*)>(exe,0x638830)(h);
    // Identity comes from the local-disc script's exact actor and the engine's
    // generation-checked component/body handles. Collision-mesh vertices are
    // not physics particles: the live personal disc has 26, not the mesh's 40.
    if(!body || !at<U>(body,0x438) || at<U>(body,0x438)>4096) return {};
    auto findCS=fn<void*(*)(void*,U)>(exe,0x105510);
    auto touch=findCS(gs,0x2e40131422f7b1fc);
    unsigned th=0xffffffff;
    if(!touch) return {};
    virt<void*(*)(void*,unsigned*,U,U)>(touch,0x1a8)(touch,&th,self,NONE);
    if(!virt<int(*)(void*,unsigned)>(touch,0x1c8)(touch,th)) return {};
    return {body,touch,th};
}
static bool caught(Body b) {
    return fn<bool(*)(void*,unsigned short)>(exe,0x75fd80)(b.touch,(unsigned short)b.touchHandle);
}
static void setVelocity(Body b,Vec v) {
    // Never clear physics flag 4. The old trainer incorrectly called that a
    // held flag; the authored touch-interaction component decides possession.
    if(!b.p || (at<unsigned>(b.p,0xbc)&4) || caught(b)) return;
    fn<void(*)(void*,Vec*,int)>(exe,0x6320e0)(b.p,&v,0);
}
static void requestDisc(void* controller) {
    auto gs=gamespace(controller);
    // The same authored event emitted by the stock personal-disc activity.
    if(gs) dispatchOriginal(gs,0x59aeeb3e03192a1b,0xffffffffffffULL,NONE,1);
}
static void trainerTick(void* ctx) {
    U now=GetTickCount64();
    if(!session.active || ctx!=trainingController) return;
    if(!session.tick(now,eligible(ctx))) {stop("SESSION ENDED | PRESS GOALIE TO START");return;}
    if(phase==0) {
        requestDisc(ctx); phase=1;phaseUntil=now+2500;
        reason="PREPARING SHOT | 60 SECOND SESSION";
        return;
    }
    if(phase==1) {
        void* match=nullptr;
        for(auto d:discs) {
            auto state=at<P>(d,0x68);
            if(state && at<unsigned char>(state,0xa7) && bodyFor(d).p) {
                if(match) {stop("MULTIPLE LOCAL DISCS: SEE trainer.log");return;}
                match=d;
            }
        }
        if(!match) {if(now>=phaseUntil) stop("DISC NOT FOUND: SEE trainer.log");return;}
        trainingDisc=match;
        auto body=bodyFor(match);
        {auto pos=at<Vec>(body.p,0x8f8);char line[256];
        sprintf_s(line,"resolved disc body=%p particles=%llu flags=%x held=%d pos=%.3f,%.3f,%.3f",body.p,at<U>(body.p,0x438),at<unsigned>(body.p,0xbc),int(caught(body)),pos.x,pos.y,pos.z);log(line);}
        if(caught(body)) {if(now>=phaseUntil) stop("RELEASE DISC THEN START GOALIE");return;}
        auto kind=shotKind(random32()%100);
        cutShot=kind==ShotKind::Cut;
        unsigned count=0;
        for(auto& shot:SHOTS) if(bool(shot.bounces)==(kind==ShotKind::Bounce)) count++;
        unsigned pick=random32()%count;
        for(unsigned i=0;i<std::size(SHOTS);i++) {
            if(bool(SHOTS[i].bounces)!=(kind==ShotKind::Bounce)) continue;
            if(pick--==0) {shotIndex=i;break;}
        }
        cutIndex=random32()%unsigned(std::size(CUTS));
        redirect.armed=false;
        phase=2;phaseUntil=now+6000;
        log("Local personal disc resolved through actor and generation-checked physics handles");
    }
    if(!trainingDisc || !discs.count(trainingDisc)) {phase=0;trainingDisc=nullptr;return;}
    auto body=bodyFor(trainingDisc);
    if(!body.p) {phase=0;return;}
    if(phase==2) {
        if(caught(body)) {reason="RELEASE DISC TO CONTINUE"; if(now>=phaseUntil) stop("RELEASE DISC THEN START GOALIE");return;}
        auto pos=at<Vec>(body.p,0x8f8);
        if(!std::isfinite(pos.x+pos.y+pos.z)) {stop("INVALID DISC POSITION: SEE trainer.log");return;}
        Vec delta=(cutShot?CUTS[cutIndex].start:SHOTS[shotIndex].start)-pos; float distance=length(delta);
        if(distance<.04f) {
            if(cutShot) {
                auto& cut=CUTS[cutIndex]; Vec toPoint=cut.point-pos;
                Vec velocity=toPoint*(cut.speed/length(toPoint));
                setVelocity(body,velocity);redirect.begin(cut.point,velocity);
                phase=5;phaseUntil=now+U((length(toPoint)/cut.speed+.75f)*1000);
                reason="CUT PASS | SLAP NEAR GOAL";log(reason);return;
            }
            setVelocity(body,SHOTS[shotIndex].velocity);
            phase=3;phaseUntil=now+U((SHOTS[shotIndex].seconds+1.25f)*1000);
            reason=SHOTS[shotIndex].bounces?"BOUNCE SHOT | NATURAL FLIGHT":"DIRECT SHOT | NATURAL FLIGHT";
            log(reason);return;
        }
        if(now>=phaseUntil) {setVelocity(body,{0,0,0});stop("CANNOT STAGE DISC: TRY AGAIN");return;}
        // Staging only. Once launched, never overwrite flight, a bounce or save.
        float speed=std::min(18.f,distance*6.f);
        setVelocity(body,delta*(speed/std::max(distance,.001f)));
    } else if(phase==5) {
        auto result=redirect.update(at<Vec>(body.p,0x8f8),at<Vec>(body.p,0x910),caught(body));
        if(result==CutRedirect::Cancel || now>=phaseUntil) {
            redirect.armed=false;phase=4;phaseUntil=now+1000;
            reason="PASS ENDED | NEXT SHOT";log(reason);return;
        }
        if(result==CutRedirect::Slap) {
            auto& cut=CUTS[cutIndex];Vec delta=cut.target-at<Vec>(body.p,0x8f8);
            float distance=length(delta);
            if(distance<1 || !std::isfinite(distance)) {stop("INVALID SLAP POSITION");return;}
            setVelocity(body,delta*(cut.speed/distance));
            phase=3;phaseUntil=now+U((distance/cut.speed+1.25f)*1000);
            reason="SLAP SHOT | NATURAL FLIGHT";
            char line[180];sprintf_s(line,"SLAP SHOT distance=%.2fm speed=%.2fm/s",distance,cut.speed);log(line);
        }
    } else if(phase==3 && (caught(body) || now>=phaseUntil)) {
        phase=4;phaseUntil=now+1000;reason="NEXT SHOT";
    } else if(phase==4 && now>=phaseUntil) phase=0;
}

static void updateScript(unsigned kind,void* ctx) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if(kind==0) {
        if(controllers.insert(ctx).second) {char line[160];sprintf_s(line,"controller ctx=%p gs=%p",ctx,gamespace(ctx));log(line);}
        trainerTick(ctx);
    } else if(kind==1) {
        if(discs.insert(ctx).second) {
            auto s=at<P>(ctx,0x68);char line[220];
            sprintf_s(line,"disc ctx=%p gs=%p local=%u self=%llx/%llx/%llx",ctx,gamespace(ctx),unsigned(at<unsigned char>(s,0xa7)),at<U>(s,0xb0),at<U>(s,0xb8),at<U>(s,0xc0));log(line);
        }
    }
}
// SEH contains only our extension. Never swallow exceptions from stock Echo.
static void safeUpdate(unsigned kind,void* ctx) {
    __try {updateScript(kind,ctx);} __except(EXCEPTION_EXECUTE_HANDLER) {fail("Native extension access fault in script update; trainer disabled");}
}
template<unsigned K> static int scriptUpdate(void* ctx) {
    int busy=scripts[K].update(ctx);safeUpdate(K,ctx);return std::max(busy,1);
}
template<unsigned K> static void scriptDestroy(void* ctx) {
    {std::lock_guard<std::recursive_mutex> lock(mutex);
    char line[120];sprintf_s(line,"script destroy kind=%u ctx=%p",K,ctx);log(line);
    controllers.erase(ctx);discs.erase(ctx);
    if(ctx==trainingController || ctx==trainingDisc) stop("MAP CHANGED | GOALIE OFF");}
    scripts[K].destroy(ctx);
}
static U loaded(void* p,U a,U b,U c) {
    U r=loadedOriginal(p,a,b,c);
    if(r==0) {std::lock_guard<std::recursive_mutex> lock(mutex);remember(p);}return r;
}
static void unload(void* p,U a,U b,U c) {
    {std::lock_guard<std::recursive_mutex> lock(mutex);
    pending.erase(p); auto it=canvases.find(p);
    if(p==doomRootCanvas.load()) {doomRootCanvas=nullptr;doomSelected=false;doom.blur();}
    if(p==doomPageCanvas.load()) doomPageCanvas=nullptr;
    if(it!=canvases.end()) {
        auto& ctx=contexts[it->second.first];unsigned kind=it->second.second;
        if(ctx.views[kind].p==p) ctx.views[kind]=View{};
        if(kind==0) {ctx.page=Page::Stock;held.clear();stop("MAP CHANGED | GOALIE OFF");}
        canvases.erase(it);
    }}
    unloadOriginal(p,a,b,c);
}
static void button(void* cs) {
    {std::lock_guard<std::recursive_mutex> lock(mutex);
    std::vector<void*> retry(pending.begin(),pending.end());for(auto p:retry) remember(p);
    auto& ctx=contexts[at<void*>(cs,0x80)];gate(ctx,cs);render(ctx);}
    buttonOriginal(cs);
    {std::lock_guard<std::recursive_mutex> lock(mutex);
    auto& ctx=contexts[at<void*>(cs,0x80)];gate(ctx,cs);render(ctx);}
}
static void dispatch(void* gs,U event,U actor,U component,int arg) {
    {std::lock_guard<std::recursive_mutex> lock(mutex);
    if(!fault && (event==PRESS || event==RELEASE) && (component==TAB || component==QUESTION || component==GOALIE || component==DISC || doomKey(component)>=0 || stock(component))) {
        auto it=contexts.find(gs);
        if(it!=contexts.end() && it->second.views[0].p) {
            std::array<U,3> key={reinterpret_cast<U>(gs),actor,component};
            if(event==RELEASE) {held.erase(key);if(doomKey(component)>=0) doom.key(unsigned(doomKey(component)),false);}
            else if(held.insert(key).second) {
                auto& c=it->second;
                if(stock(component)) {c.page=Page::Stock;doom.blur();}
                else if(component==TAB) {c.page=Page::Tools;doom.blur();}
                else if(component==QUESTION) {c.page=Page::Doom;log(doom.start()?"Question tab: native Doom worker started/resumed":"Doom worker/data missing or launch failed");}
                else if(c.page==Page::Doom && doomKey(component)>=0) doom.key(unsigned(doomKey(component)),true);
                else if(c.page==Page::Tools && component==DISC) {discOn=!discOn;if(!discOn) stop("PERSONAL DISC OFF | GOALIE OFF");log(discOn?"Personal Disc ON":"Personal Disc OFF");}
                else if(c.page==Page::Tools && component==GOALIE) {
                    if(session.active) stop("GOALIE STOPPED");
                    else {
                        void* controller=nullptr;
                        {char line[120];sprintf_s(line,"goalie press controllers=%zu discs=%zu",controllers.size(),discs.size());log(line);}
                        for(auto p:controllers) {
                            auto kind=matchKind(p);auto n=nodeFor(p);char line[180];
                            sprintf_s(line,"goalie eligibility ctx=%p social=%d arena=%d phaseAllowed=%d discOn=%d",p,int(kind.first),int(kind.second),scripts[0].allowed?scripts[0].allowed(n.data()):-1,int(discOn.load()));log(line);
                        }
                        for(auto p:controllers) if(eligible(p)) {if(controller) {controller=nullptr;break;}controller=p;}
                        if(session.toggle(GetTickCount64(),controller!=nullptr)) {
                            trainingController=controller;phase=0;reason="STARTING 60 SECOND SESSION";log(reason);
                        } else {reason=discOn?"ARENA ONLY | WAIT FOR ROUND TO END":"TURN PERSONAL DISC ON FIRST";log(reason);}
                    }
                }
            }
        }
    }}
    dispatchOriginal(gs,event,actor,component,arg);
}
static bool hook(void* address,void* detour,void** original) {
    auto status=MH_CreateHook(address,detour,original);
    if(status!=MH_OK) {fail(MH_StatusToString(status));return false;}
    status=MH_EnableHook(address);
    if(status!=MH_OK) {fail(MH_StatusToString(status));return false;}
    return true;
}
static void elementsRender(void* canvas,void* renderer,void* state) {
    elementsRenderOriginal(canvas,renderer,state);
    if(canvas!=doomPageCanvas.load() || !doomDrawing || !doomSelected || fault) return;
    doom.shown();
    auto draw=fn<void(*)(void*,void*,const float*,uint32_t)>(exe,0x5657e0);
    for(auto& quad:doomQuads) draw(renderer,state,quad.rect,quad.color);
}
static void canvasRender(void* canvas) {
    if(canvas!=doomRootCanvas.load() || !doomSelected || fault) {canvasRenderOriginal(canvas);return;}
    auto graphics=at<P>(exe,0x20a3248);if(!graphics) {canvasRenderOriginal(canvas);return;}
    auto renderer=graphics+0x378;
    unsigned vertices=at<unsigned>(canvas,0x344),indices=at<unsigned>(canvas,0x348);
    U usedV=at<U>(renderer,0x100),usedI=at<U>(renderer,0x108);
    U maxV=at<unsigned>(renderer,0x68),maxI=at<unsigned>(renderer,0xd8);
    unsigned budget=0;
    if(usedV+vertices<maxV && usedI+indices<maxI && vertices<65520)
        budget=unsigned(std::min({(maxV-usedV-vertices)/8,(maxI-usedI-indices)/12,U((65520-vertices)/4),U(16000)}));
    doom.geometry(doomQuads,budget);
    at<unsigned>(canvas,0x344)=vertices+unsigned(doomQuads.size())*4;
    at<unsigned>(canvas,0x348)=indices+unsigned(doomQuads.size())*6;
    at<int>(canvas,0x340)=std::max(at<int>(canvas,0x340),2);
    doomDrawing=true;canvasRenderOriginal(canvas);doomDrawing=false;
    at<unsigned>(canvas,0x344)=vertices;at<unsigned>(canvas,0x348)=indices;
    static U lastLog=0;U now=GetTickCount64();
    if(now-lastLog>10000) {char line[180];sprintf_s(line,"Doom display frames=%ld quads=%zu budget=%u baseVertices=%u",doom.frames(),doomQuads.size(),budget,vertices);log(line);lastLog=now;}
}
static bool initialize() {
    if(initialized) return !fault;
    initialized=true;exe=reinterpret_cast<P>(GetModuleHandleW(nullptr));
    log("Native tablet trainer initializing; no Python or Frida runtime");
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(exe+at<unsigned>(exe,0x3c));
    if(nt->OptionalHeader.SizeOfImage!=35852288 || nt->FileHeader.TimeDateStamp!=EXE_TIMESTAMP) {fail("Unsupported Echo executable version");return false;}
    for(auto s:signatures) if(memcmp(exe+s.rva,s.bytes,16)!=0) {char line[120];sprintf_s(line,"Native signature mismatch at %x",s.rva);fail(line);return false;}
    if(MH_Initialize()!=MH_OK) {fail("MinHook initialization failed");return false;}
    show=fn<Show>(exe,0x71c820);text=fn<Text>(exe,0x727f10);alpha=fn<Alpha>(exe,0x726f00);
    enable=fn<Mask>(exe,0x92bd10);disable=fn<Mask>(exe,0x92b9e0);
    bool ok=hook(exe+0x71fc90,reinterpret_cast<void*>(loaded),reinterpret_cast<void**>(&loadedOriginal)) &&
        hook(exe+0x7287b0,reinterpret_cast<void*>(unload),reinterpret_cast<void**>(&unloadOriginal)) &&
        hook(exe+0x510060,reinterpret_cast<void*>(dispatch),reinterpret_cast<void**>(&dispatchOriginal)) &&
        hook(exe+0x92f3f0,reinterpret_cast<void*>(button),reinterpret_cast<void**>(&buttonOriginal)) &&
        hook(exe+0x724ff0,reinterpret_cast<void*>(canvasRender),reinterpret_cast<void**>(&canvasRenderOriginal)) &&
        hook(exe+0x725730,reinterpret_cast<void*>(elementsRender),reinterpret_cast<void**>(&elementsRenderOriginal));
    if(!ok) MH_DisableHook(MH_ALL_HOOKS);
    return ok;
}
extern "C" __declspec(dllexport) void BindTrainer(unsigned kind,void* table,HMODULE module) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if(kind>2 || !initialize()) return;
    auto& script=scripts[kind];
    if(script.module) return;
    script.module=reinterpret_cast<P>(module);
    if(kind<2) {
        unsigned rva=kind?0x20000:0xd000;
        // The installer verifies complete file hashes; repeat the gate signature
        // here so a replacement installed later cannot turn into a blind hook.
        if(memcmp(script.module+rva,guardSignatures[kind],16)!=0) {fail("Installed personal-disc v3.0.1 guard differs");return;}
        if(!hook(script.module+rva,reinterpret_cast<void*>(kind?discAllowed:controllerAllowed),reinterpret_cast<void**>(&script.allowed))) return;
        if(kind==0) {
            at<void(*)(U,const char*,unsigned,unsigned*,Getter*)>(table,0x18)(0x993e022a8336e85a,"matchisarena",4,&arenaSlot,&arenaGetter);
        }
        // Script resources can call setup_bindings again and copy fresh tables
        // during level transitions. Hook the actual functions once so every
        // table, including later ones, discovers and invalidates live instances.
        auto update=at<ScriptUpdate>(table,0x58);auto destroy=at<ScriptDestroy>(table,0x50);
        if(!hook(reinterpret_cast<void*>(update),reinterpret_cast<void*>(kind?scriptUpdate<1>:scriptUpdate<0>),reinterpret_cast<void**>(&script.update))) return;
        if(!hook(reinterpret_cast<void*>(destroy),reinterpret_cast<void*>(kind?scriptDestroy<1>:scriptDestroy<0>),reinterpret_cast<void**>(&script.destroy))) return;
    }
    HMODULE pinned=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(BindTrainer),&pinned);
    log(kind==0?"Controller native gate ready":kind==1?"Personal-disc actor native gate ready":"Tablet native loader ready");
}
