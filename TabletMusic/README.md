# Music remote on the hand tablet

The left tablet tab becomes a **MUSIC** page: what is playing, a progress bar, and
six big touch controls. It drives Spotify, YouTube Music and Apple Music through
one mechanism, so there is no account linking, no API key and no network access.

**Status:** the worker, shared protocol, page canvas and native runtime build and
pass offline tests. The in-headset appearance and the touch controls still need
verification in the headset, the same way the Doom port does.

## How it reaches the music apps

Windows exposes whatever is playing through **System Media Transport Controls**
(`GlobalSystemMediaTransportControlsSessionManager`) — the same thing behind the
media overlay and the keyboard media keys. Any app that registers a session is
controllable, which covers:

| Player | Shows as | Notes |
| --- | --- | --- |
| Spotify desktop | `SPOTIFY` | |
| Apple Music for Windows | `APPLE MUSIC` | |
| YouTube Music desktop app | `YT MUSIC` | |
| YouTube Music in a browser | `CHROME` / `EDGE` / `FIREFOX` | Windows reports the browser, not the site |
| Tidal, Deezer, VLC, foobar2000, … | their own name | Anything that registers a session works |

Because Windows does the talking, nothing here stores credentials or contacts a
music service.

## Page and controls

The page is 942 x 528 inside the tablet's 1024 x 768 root canvas. The tablet's own
title bar already reads MUSIC, so the page carries no heading of its own.

```
▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁ gradient accent strip ▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁
[ SPOTIFY ]   <- tap to change player

                    Track title
                      Artist
▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
1:23                                                      4:56

[    PREV    ] [       PAUSE       ] [    NEXT    ]
[   VOL  -   ] [ ▮▮▮▮▮▯▯▯▯▯▯▯ ] [   VOL  +   ]
```

| Control | Size | Action |
| --- | --- | --- |
| PREV | 262 x 136 | Previous track |
| PLAY / PAUSE | 330 x 136 | Toggle playback; the label shows what a press will do |
| NEXT | 262 x 136 | Next track |
| VOL − / VOL + | 238 x 72 | Move that app's volume by 5%, leaving Echo's own audio alone |
| Source chip | 306 x 66 | Cycle to the next media session when more than one app is playing |

The progress bar is 60 separate panels and the volume meter 12, which the runtime
shows or hides. Each panel carries its own colour, so the bar is shaded along a
gradient at no cost — the hue also tells you roughly how far through a track you
are. The elapsed and total times are anchored to the two ends of the bar.

### Themes

`--theme` picks the palette: `sunset` (amber into pink), `neon` (cyan into
magenta) or `echo` (a richer take on the tablet's own teal). Only the chosen
theme writes the installable patch:

```powershell
python -B TabletMusic/build_music_tab.py --theme sunset
python -B TabletMusic/build_music_tab.py --theme neon --preview-only
```

`--preview-only` writes just `build/music-<theme>.canvas`, which
[preview_page.py](preview_page.py) renders for comparison. Adding a theme is a
single entry in `THEMES`; nothing else changes.

Volume is applied per application through Core Audio, matched from the session's
model id. If no session matches, the worker falls back to whichever other process
is actively rendering audio, and never touches Echo's own volume.

Switching to another tablet page stops the heartbeat, so the worker idles instead
of polling; opening the page again refreshes it within about 50 ms. The worker
exits with Echo through the same job object the Doom worker uses.

## Native integration

[`native/worker.cpp`](native/worker.cpp) is a hidden process that owns every
WinRT and COM call. The tablet only reads a snapshot and appends commands to a
16-entry ring, so nothing async, apartment-bound or blocking runs on Echo's
threads. [`native/shared.h`](native/shared.h) defines that block and
[`native/client.h`](native/client.h) launches the worker and caches the snapshot
so the per-frame render path takes no mutex unless something changed.

Unlike the Doom port, **this page installs no renderer hooks**. It is built from
ordinary canvas panels and text, so it uses only the entry points the Tools page
already relies on:

- `0x71c820` show/hide element
- `0x727f10` set element text
- `0x726f00` set element alpha
- `0x92f3f0` / `0x92b9e0` / `0x92bd10` native button update, disable, enable
- `0x510060` component event dispatch

The two Doom renderer hooks (`0x724ff0`, `0x725730`) stay inactive. That also
means no draw-budget pressure beyond the page's own reservation, which matches
the Doom page's proven 3072 vertices / 4608 indices.

Every label the runtime rewrites reserves capacity in the canvas text buffer
(128 bytes for title and artist, 64 elsewhere). The runtime clamps to that
capacity *and* to what fits the rect, backing off to a UTF-8 boundary, so a long
track name can never overrun the slot. The `..` marker counts toward the limit,
so the result is always shorter than the reserved capacity.

Each label also sets its own text anchor. The donor element inherits
`(0.0, 0.5)` — left, vertically centred — which is why the stock Tools page insets
its labels horizontally from their panels. Labels here share their control's rect,
so the builder writes `0.5` into the X anchor at `+0x9c` to centre them. See the
[tablet UI notes](../EchoVr-Tablet-Probing/ECHOVR_TABLET_UI_NOTES.md) for how that
field was identified.

## Previewing the layout

`preview_page.py` renders the built canvas to SVG, reading the real rects,
colours, font sizes, hidden flags and anchors, so the layout can be judged without
the headset:

```powershell
python -B TabletMusic/preview_page.py
```

It writes `build/music-page-playing.svg`, `build/music-page-idle.svg` and a
`build/music-page.html` sheet showing both. Only the typeface is approximated.

## Files and build

- [build_music_tab.py](build_music_tab.py): navigation tab, page canvas, six
  touch hit boxes and the generated header. Verifies the stock tabs and their hit
  boxes are preserved and that the new tab overlaps none of them.
- [preview_page.py](preview_page.py): renders the built page to SVG/HTML.
- `native/`: worker, shared protocol, client and the two offline tests.
- `build/`: generated canvases, patch, worker and previews (not committed).

From the repository root:

```powershell
.\TabletMusic\build_worker.cmd
python -B TabletMusic/build_music_tab.py
.\TabletMusic\test_client.cmd
.\PersonalDiscTrainer\native\build.cmd
```

`test_client.cmd` runs two tests. The first checks label fitting and progress
maths at every boundary, then launches the real worker and prints what Windows
reports — so it doubles as a check that your player is visible. It passes with
nothing playing.

The second registers a media session of its own and drives the worker against it,
confirming metadata comes back exactly and that play/pause, next and previous
reach the owning app. It only sends commands once the worker reports *that*
session as selected, so a real player on the build machine is left alone, and it
never exercises volume, which would move another app's level.

The MUSIC tab and the Doom **?** tab claim the same free navigation slot, so the
installed tablet patch decides which one exists. Build one or the other; restore
the previous install before switching. `MUSIC_ENABLED` in
[runtime.cpp](../PersonalDiscTrainer/native/runtime.cpp) gates the tab, worker
launch and page rendering independently of `DOOM_ENABLED`.

Diagnostics: `%LOCALAPPDATA%\EchoTabletTrainer\trainer.log` for the tab, and
`%LOCALAPPDATA%\EchoTabletTrainer\Music\worker.log` for the worker.

## Remaining headset checks

Confirm the MUSIC tab appears and opens, the six controls press and release, the
title and progress update while a track plays, the source chip cycles when two
players are open, MUSIC → Tools → Friends → MUSIC stays clean, closing and
reopening the tablet pauses polling, and lobby/arena travel recreates the page.
