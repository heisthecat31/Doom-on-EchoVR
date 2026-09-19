#pragma once
#include <windows.h>
#include <stdint.h>
/* Tablet <-> MusicWorker protocol. The worker owns every WinRT/COM call; Echo's
   process only reads this block and appends commands. */
#define MUSIC_SHARED_MAGIC 0x4b52544d /* 'MTRK' */
#define MUSIC_TEXT 128
#define MUSIC_SOURCE 64
#define MUSIC_QUEUE 16
enum { MC_NONE,MC_PLAYPAUSE,MC_NEXT,MC_PREV,MC_VOLUP,MC_VOLDOWN,MC_SOURCE,MC_COUNT };
enum { MP_UNKNOWN,MP_PLAYING,MP_PAUSED };
typedef struct MusicShared {
    LONG magic;
    volatile LONG heartbeat;   /* GetTickCount when the page last drew; 0 = page hidden */
    volatile LONG status;      /* 0 starting, 1 running, 2 exited */
    volatile LONG updates;     /* snapshot counter; 0 until the first poll completes */
    volatile LONG head,tail;   /* command ring; the client only appends, the worker only drains */
    volatile LONG queue[MUSIC_QUEUE];
    volatile LONG playback;    /* MP_* */
    volatile LONG position;    /* seconds into the track, -1 unknown */
    volatile LONG duration;    /* track length in seconds, -1 unknown */
    volatile LONG volume;      /* 0..100 for the matched app, -1 when none matched */
    volatile LONG sessions;    /* media sessions currently registered with Windows */
    volatile LONG selected;    /* index of the session being shown */
    char title[MUSIC_TEXT];    /* guarded by the -text mutex */
    char artist[MUSIC_TEXT];
    char source[MUSIC_SOURCE];
} MusicShared;
