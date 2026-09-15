# Doom on Echo VR

Run Doom shareware inside Echo VR's hand tablet. When enabled, the left **?** tab
launches a native Doom worker and displays its frames alongside eight touch controls.

**Doom is disabled by default** so the tablet integration can be reused for
another project. See [Enable or disable Doom](#enable-or-disable-doom).

**Experimental Windows PC build:** offline engine, input, pause/resume and native
frame-client tests pass. The Doom picture, touch controls and performance still
need in-headset verification. Audio is currently disabled.

## Play

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

## Enable or disable Doom

Set `DOOM_ENABLED` in
[PersonalDiscTrainer/native/runtime.cpp](PersonalDiscTrainer/native/runtime.cpp):

```cpp
static constexpr bool DOOM_ENABLED=false;
```

- `false` (default): hides the **?** tab, disables its touch target, prevents
  worker launch and skips both Doom rendering hooks.
- `true`: enables the Doom tab, worker and rendering hooks.

Tools, Personal Disc and Goalie remain available in either mode. Doom's source,
assets and shared tablet integration are retained for reuse. The flag only
controls the installed tablet runtime; standalone worker tests still run Doom.

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
- Visual Studio C++ tools and the Windows SDK. The three `.cmd` files currently
  use `C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat`;
  update that path for your Visual Studio installation.
- For packaging/installing: a compatible PC Echo VR installation and the exact
  PersonalDiscEverywhere v3.0.1 script pair pinned in
  [disc_binding.py](EchoVr-Tablet-Probing/disc_binding.py). The preparation script
  validates these inputs and reads original script copies from an existing native
  trainer installation when present.

From the repository root:

```powershell
python -m pip install -r requirements.txt
.\TabletDoom\build_worker.cmd
python -B TabletDoom/build_question_tab.py
python -B TabletDoom/test_worker.py
.\TabletDoom\test_client.cmd
.\PersonalDiscTrainer\native\build.cmd
```

These commands build the worker, tablet patch and shared native runtime, and run
the existing worker, frame-client and native policy tests. The committed canvas
inputs and generated headers allow this stage to run without reading a game
installation. The built-in trainer shot bank is included as generated source;
rebaking it uses the separate DiscTracer research workspace.

To prepare an installable package, set the path to your compatible game, then run:

```powershell
$env:ECHOVR_GAME = 'C:\path\to\ready-at-dawn-echo-arena'
python -B PersonalDiscTrainer/prepare_native.py
.\PersonalDiscTrainer\native\build.cmd
python -B PersonalDiscTrainer/package_native.py
```

The second native build compiles the freshly checked executable bindings.
Packaging produces `PersonalDiscTrainer/native/dist/` and
`PersonalDiscTrainer/native/EchoTabletTrainer-test.zip`, including the worker's
corresponding source and third-party licenses. Compiled packages and copied game
script DLLs are generated locally and excluded from Git.

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

To undo the installation, close Echo and run from the same package directory:

```powershell
.\Install.ps1 -Action Restore -GameRoot 'C:\path\to\ready-at-dawn-echo-arena'
```

Restore checks for subsequent modifications before restoring originals. Backups
remain under `bin/win10/EchoTabletTrainer-backup-*` in the game folder.

## Source layout

| Path | Contents |
| --- | --- |
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
- Doom: `%LOCALAPPDATA%\EchoTabletTrainer\Doom\worker.log`
- Offline worker captures: `TabletDoom/build/doom-*.png`

In-headset checks still needed: picture visibility, entering a level, every touch
control's press/release behavior, switching between Doom/Tools/Friends,
closing/reopening the tablet, lobby/arena transitions, and VR frame time.
