# Doom on the hand tablet

The left **?** tab now starts a native Doom worker and displays its frames inside
the tablet page. The old **COMING SOON** page has been replaced by a screen and
eight controls. This is an experimental, silent shareware port. The actual engine,
frame transfer, input, pause/resume and native client pass offline tests; the wrist
display and touch controls still need in-game verification.

## Play

Start Echo normally, open the hand tablet and select **?**. Doom launches
automatically; no Python or manually started helper is required.

- **ENTER** opens/selects Doom menu items; **MENU** goes back or opens its menu.
- **UP / DOWN** move forward/back or select menu items; **LEFT / RIGHT** turn.
- **FIRE** shoots; **USE** opens doors and activates switches.
- Keyboard equivalents are arrows, Ctrl, Space, Enter and Escape while Echo has
  desktop focus and the Doom page is drawing.
- Switching to another tablet page pauses Doom and releases held input. The worker
  exits with Echo. The standard four tabs and Tools remain at their existing positions.

The engine runs at 320×200. This first display path uses horizontal runs of colored
UI quads, starting at 160×100 and adapting downward to Echo's available draw budget.
The picture is shown at 4:3 inside a 640×480 rectangle; navigation and the controls
remain outside it. A direct GPU texture upload is a future quality/performance
improvement. Audio is disabled in this initial port.

## Native integration

[`native/worker.c`](native/worker.c) hosts the unmodified
[doomgeneric](https://github.com/ozkl/doomgeneric) engine in a hidden native process.
It publishes real frames through a mutex-protected shared mapping and consumes
the tablet's press/release state. Its game clock freezes when the page stops
drawing, avoiding catch-up simulation on resume. The worker's normal Doom exit
paths cannot exit the Echo process.

[`native/client.h`](native/client.h) launches the worker, reads complete frames,
converts them into bounded draw commands and owns the parent-lifetime job. The
[native tablet runtime](../PersonalDiscTrainer/native/runtime.cpp) submits those
commands through Echo's existing canvas renderer. It pins the relevant executable
prologues and reserves vertex/index space before drawing. Frame pixels are confined
to the Doom page. No global graphics API hooks or per-map asset edits are used.

Renderer entry points verified against this executable's decompilation:

- `0x724ff0`: root canvas render and draw-budget reservation.
- `0x725730`: element/child rendering with the current canvas transform and clip.
- `0x5657e0`: native colored quad submission.

The game uses DirectX 12. General tablet renderer research remains in the
[tablet UI notes](../EchoVr-Tablet-Probing/ECHOVR_TABLET_UI_NOTES.md).

## Files and build

- [build_question_tab.py](build_question_tab.py): shared six-tab layout and eight
  native controls; preserves existing button rows and verifies navigation spacing.
- `baselines/landing_v1.json`: hashes of the previously installed placeholder, so
  the installer can upgrade it without accepting unknown tablet modifications.
- `native/`: platform adapter, shared protocol, frame client and native tests.
- `vendor/doomgeneric/`: pinned, unchanged upstream source.
- `data/doom1.wad`: shareware game data. [SOURCES.md](SOURCES.md) records provenance
  and hashes. No archive executable was run.
- `build/`: generated canvases, patch, native worker, tests and captured frames.

From the workspace:

```powershell
.\TabletDoom\build_worker.cmd
python -B TabletDoom/build_question_tab.py
python -B TabletDoom/test_worker.py
.\TabletDoom\test_client.cmd
Set-Location PersonalDiscTrainer
python -B prepare_native.py
.\native\build.cmd
python -B package_native.py
```

The [existing native installer](../PersonalDiscTrainer/native/Install.ps1) installs
the runtime, tablet patch and `bin/win10/doom/{DoomWorker.exe,doom1.wad}`. Restore
removes the added worker/data and restores the original files. The local test ZIP
includes the Doom worker's corresponding source and GPL license. See the
[repository README](../README.md) for prerequisites and install/restore commands.

Diagnostics: `%LOCALAPPDATA%\EchoTabletTrainer\trainer.log` includes frame/quad
counts; `%LOCALAPPDATA%\EchoTabletTrainer\Doom\worker.log` contains Doom output.

## Remaining headset checks

Confirm the Doom picture appears, Enter reaches a level, every touch control
presses/releases, ? → Tools → Friends → ? remains clean, closing/reopening pauses
correctly, and lobby/arena travel recreates the page. Check VR frame time and image
quality before treating this as a verified playable tablet port.
