# Tablet tabs for Echo VR

Extra tabs on Echo VR's hand tablet, sharing one native runtime:

- **MUSIC** — a music remote for Spotify, YouTube Music and Apple Music, with big
  touch controls. Enabled by default. See [TabletMusic/](TabletMusic/README.md).
- **?** — Doom shareware rendered into the tablet with eight touch controls.
  Disabled by default. See [Enable or disable a tab](#enable-or-disable-a-tab).
- **TOOLS** — Personal Disc toggle and Goalie trainer. Always available.

The MUSIC and **?** tabs claim the same free navigation slot, so the tablet patch
you build decides which one is installed.

**Experimental Windows PC build.** Offline tests pass for both tabs; neither tab's
appearance or touch controls have been verified in the headset yet.

## Music remote

The left tab shows what is playing and six large controls: PREV, PLAY/PAUSE,
NEXT, VOL −, VOL + and a source chip that cycles between players.

| Tablet control | Action |
| --- | --- |
| PREV / NEXT | Previous or next track |
| PLAY / PAUSE | Toggle playback; the label shows what a press will do |
| VOL − / VOL + | Move that app's volume by 5%, leaving Echo's audio alone |
| Source chip | Switch to the next media session when several apps are playing |

It drives the player through Windows' System Media Transport Controls — the same
mechanism behind the media keys — so there is no account linking, no API key and
no network access. Spotify, Apple Music and the YouTube Music desktop app are
named on the page; YouTube Music in a browser shows as the browser, because that
is what Windows reports. This page installs no renderer hooks.

## Play Doom

After enabling, building and installing, launch Echo VR normally and open **?** on the hand
tablet. The worker starts automatically; Python is only needed for development.

| Tablet control | Keyboard equivalent | Action |
| --- | --- | --- |
| UP / DOWN | Up / Down arrows | Move forward/back or navigate menus |
| LEFT / RIGHT | Left / Right arrows | Turn |
| FIRE | Ctrl | Shoot |
| USE | Space | Open doors and activate switches |
| ENTER | Enter | Select a menu item |
| MENU | Escape | Open the menu or go back |

Keyboard input requires Echo's desktop window to have focus while the Doom page
is drawing. Switching pages pauses Doom and releases held input. The worker exits
with Echo.

The engine renders at 320 x 200. Echo displays the image at 4:3 using colored UI
quads, starting at 160 x 100 and lowering detail to fit the available draw budget.

## Enable or disable a tab

Two independent build-time switches in
[PersonalDiscTrainer/native/runtime.cpp](PersonalDiscTrainer/native/runtime.cpp):

```cpp
static constexpr bool DOOM_ENABLED=false;
static constexpr bool MUSIC_ENABLED=true;
```

- `DOOM_ENABLED` `false` (default): hides the **?** tab, disables its touch
  target, prevents worker launch and skips both Doom rendering hooks.
- `MUSIC_ENABLED` `true` (default): shows the **MUSIC** tab, enables its six
  touch targets, launches the media worker and renders the page.

The flags are independent, but the two tabs share one navigation slot, so the
tablet patch you install decides which tab actually exists. A flag left on for a
tab whose patch is not installed costs nothing: its canvas is never found and its
buttons never appear.

Tools, Personal Disc and Goalie remain available in every mode. Doom's source,
assets and shared tablet integration are retained for reuse. The flags only
control the installed tablet runtime; standalone worker tests still run.

This is a build-time setting. After completing the initial build/preparation
below, apply a changed flag from the repository root with:

```powershell
.\PersonalDiscTrainer\native\build.cmd
python -B PersonalDiscTrainer/package_native.py
```

Close Echo VR and install the rebuilt package using the
[installation instructions](#install-and-restore). Editing the flag alone does
not change an already installed DLL. To re-enable Doom, set it to `true` and
repeat the same build, package and install steps.

## Build

Requirements:

- Windows x64 and Python 3.10 or newer.
- Visual Studio C++ tools and the Windows SDK. Every `.cmd` locates them through
  [vcvars.cmd](vcvars.cmd), which asks `vswhere` for the latest install; set the
  `VCVARS` environment variable to a `vcvars64.bat` path to override it.
- For packaging/installing: a compatible PC Echo VR installation and the exact
  PersonalDiscEverywhere v3.0.1 script pair pinned in
  [disc_binding.py](EchoVr-Tablet-Probing/disc_binding.py). The preparation script
  validates these inputs and reads original script copies from an existing native
  trainer installation when present.

From the repository root:

```powershell
python -m pip install -r requirements.txt
.\TabletMusic\build_worker.cmd
python -B TabletMusic/build_music_tab.py
.\TabletMusic\test_client.cmd
.\PersonalDiscTrainer\native\build.cmd
```

That builds the music worker, the MUSIC tablet patch and the shared native
runtime, and runs the music client and native policy tests. The Doom tab builds
alongside it into its own directory:

```powershell
.\TabletDoom\build_worker.cmd
python -B TabletDoom/build_question_tab.py
python -B TabletDoom/test_worker.py
.\TabletDoom\test_client.cmd
```

These commands build the workers, tablet patch and shared native runtime, and run
the worker, frame-client and native policy tests. The committed canvas
inputs and generated headers allow this stage to run without reading a game
installation. The built-in trainer shot bank is included as generated source;
rebaking it uses the separate DiscTracer research workspace.

To prepare an installable package, set the path to your compatible game, then run:

```powershell
$env:ECHOVR_GAME = 'C:\path\to\ready-at-dawn-echo-arena'
$env:ECHOVR_TABLET_TAB = 'music'
python -B PersonalDiscTrainer/prepare_native.py
.\PersonalDiscTrainer\native\build.cmd
python -B PersonalDiscTrainer/package_native.py
```

`ECHOVR_TABLET_TAB` picks which tab's patch is installed, `music` or `doom`. It is
only required when both have been built; otherwise the one you built is used.

The second native build compiles the freshly checked executable bindings.
Packaging produces `PersonalDiscTrainer/native/dist/` and
`PersonalDiscTrainer/native/EchoTabletTrainer-test.zip`, including whichever tab
workers you built and their third-party licenses. Compiled packages and copied
game script DLLs are generated locally and excluded from Git.

## Install and restore

This port shares the **EchoTabletTrainer** runtime with the Tools page, Personal
Disc toggle and Goalie trainer. Installing the package also installs those
features. Personal Disc defaults on and Goalie defaults off.

Close Echo VR. Open PowerShell as administrator in
`PersonalDiscTrainer/native/dist/` and run:

```powershell
.\Install.ps1 -Action Verify -GameRoot 'C:\path\to\ready-at-dawn-echo-arena'
.\Install.ps1 -Action Install -GameRoot 'C:\path\to\ready-at-dawn-echo-arena'
```

Start Echo normally. Do not run the legacy Python tablet runner or trainer
alongside the native runtime. The installer checks package hashes and game/script
compatibility, backs up replaced files, and merges five tablet resources while
preserving unrelated manifest entries.

**Reinstalling after a tablet change.** The manifest merger refuses to overwrite a
tablet resource whose current hash it does not recognise — including one an earlier
build of this same patch installed, which it cannot tell apart from a third-party
modification. Run `-Action Restore` first, then install again:

```powershell
.\Install.ps1 -Action Restore -GameRoot 'C:\path\to\ready-at-dawn-echo-arena'
.\Install.ps1 -Action Install -GameRoot 'C:\path\to\ready-at-dawn-echo-arena'
```

To undo the installation, close Echo and run from the same package directory:

```powershell
.\Install.ps1 -Action Restore -GameRoot 'C:\path\to\ready-at-dawn-echo-arena'
```

Restore checks for subsequent modifications before restoring originals. Backups
remain under `bin/win10/EchoTabletTrainer-backup-*` in the game folder.

## Source layout

| Path | Contents |
| --- | --- |
| [TabletMusic/](TabletMusic/README.md) | Media worker, shared-memory protocol, client, test and MUSIC page builder |
| [TabletDoom/](TabletDoom/README.md) | Doom adapter, shared-memory protocol, frame client, tests and tablet page builder |
| `TabletDoom/vendor/doomgeneric/` | Pinned upstream Doom engine source |
| `TabletDoom/data/` and `TabletDoom/downloads/` | Shareware WAD and original provenance archive |
| [PersonalDiscTrainer/native/](PersonalDiscTrainer/README.md) | Shared tablet runtime, script wrappers, installer, manifest merger and generated bindings |
| [EchoVr-Tablet-Probing/](EchoVr-Tablet-Probing/README.md) | Supporting canvas/manifest tools and reproducible tablet build inputs |

The sibling directory names preserve the existing source includes and build
scripts. See [TabletDoom/SOURCES.md](TabletDoom/SOURCES.md) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for provenance and licenses.

## Diagnostics and remaining checks

- Runtime: `%LOCALAPPDATA%\EchoTabletTrainer\trainer.log`
- Music: `%LOCALAPPDATA%\EchoTabletTrainer\Music\worker.log`
- Doom: `%LOCALAPPDATA%\EchoTabletTrainer\Doom\worker.log`
- Offline worker captures: `TabletDoom/build/doom-*.png`

In-headset checks still needed for the MUSIC tab: the tab appears and opens, every
touch control presses and releases, the title and progress update while a track
plays, the source chip cycles between players, switching between MUSIC/Tools/Friends,
closing/reopening the tablet, and lobby/arena transitions.

For Doom: picture visibility, entering a level, every touch control's
press/release behavior, switching between Doom/Tools/Friends, closing/reopening
the tablet, lobby/arena transitions, and VR frame time.
