#include "policy.h"
#include "cut_redirect.h"
#include <cassert>
#include <cstdio>
int main() {
    Session s;
    assert(!s.active && !s.toggle(100,false));
    assert(s.toggle(100,true));
    assert(s.seconds(100)==60 && s.tick(60099,true));
    assert(!s.tick(60100,true));
    assert(!s.tick(60101,true)); // eligibility never restarts a timed-out session
    assert(s.toggle(70000,true));
    assert(!s.tick(70001,false)); // map/phase/disc-off cancels immediately
    assert(!s.tick(70002,true));
    assert(s.toggle(80000,true));
    assert(!s.toggle(80001,true)); // second press stops
    assert(s.toggle(90000,true) && s.deadline==150000);
    std::puts("PASS: default OFF, deny, exact 60-second deadline, cancellation, no auto-restart, manual stop");
    CutRedirect cut;
    cut.begin({0,0,-32},{0,0,-20});
    assert(cut.update({0,0,-28},{0,0,-20},false)==CutRedirect::Flying);
    assert(cut.update({0,0,-31.7f},{0,0,-20},false)==CutRedirect::Slap);
    assert(cut.update({0,0,-32},{0,0,-20},false)==CutRedirect::Cancel);
    cut.begin({0,0,-32},{0,0,-20});
    assert(cut.update({0,0,-31.7f},{0,0,-20},true)==CutRedirect::Cancel);
    cut.begin({0,0,-32},{0,0,-20});
    assert(cut.update({0,0,-31.7f},{5,0,-18},false)==CutRedirect::Cancel);
    cut.begin({0,0,-32},{0,0,-20});
    assert(cut.update({1,0,-32},{0,0,-20},false)==CutRedirect::Cancel);
    cut.begin({0,0,-32},{0,0,-20});
    assert(cut.update({0,0,-33},{0,0,-20},false)==CutRedirect::Cancel);
    std::puts("PASS: one slap per pass, catch/deflection/missed-point cancellation");
    unsigned mix[3]={};
    for(unsigned r=0;r<100;r++) mix[unsigned(shotKind(r))]++;
    assert(mix[0]==30 && mix[1]==25 && mix[2]==45);
    std::puts("PASS: 30% cut, 25% bounce, 45% direct selection weights");
}
