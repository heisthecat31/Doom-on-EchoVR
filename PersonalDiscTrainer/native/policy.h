#pragma once
#include <cstdint>

struct Session {
    bool active = false;
    uint64_t deadline = 0;
    void stop() { active=false; deadline=0; }
    bool toggle(uint64_t now, bool allowed) {
        if (active) { stop(); return false; }
        if (!allowed) return false;
        active=true; deadline=now+60000; return true;
    }
    bool tick(uint64_t now, bool allowed) {
        if (active && (!allowed || now>=deadline)) stop();
        return active;
    }
    unsigned seconds(uint64_t now) const {
        return active && deadline>now ? unsigned((deadline-now+999)/1000) : 0;
    }
};
