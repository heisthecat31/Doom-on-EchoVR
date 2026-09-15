#pragma once
#include <windows.h>
#include <stdint.h>
#define DOOM_WIDTH 320
#define DOOM_HEIGHT 200
#define DOOM_SHARED_MAGIC 0x314d4f44
enum { DK_UP,DK_DOWN,DK_LEFT,DK_RIGHT,DK_FIRE,DK_USE,DK_ENTER,DK_ESCAPE,DK_COUNT };
typedef struct DoomShared {
    LONG magic;
    volatile LONG heartbeat;
    volatile LONG keys;
    volatile LONG status; /* 0 starting, 1 running, 2 exited */
    volatile LONG frames;
    uint32_t pixels[DOOM_WIDTH*DOOM_HEIGHT]; /* 0x00RRGGBB */
} DoomShared;
