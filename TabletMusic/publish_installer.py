"""Publish a double-clickable music-tab installer into a folder.

Copies the built package from PersonalDiscTrainer/native/dist and writes the
wrapper scripts beside it. Intended for a Personal Disc v3.0.1 folder, whose own
INSTALL.cmd must be run first: the tablet runtime wraps that mod's script DLLs
and is compiled against their exact bytes.

    python -B TabletMusic/publish_installer.py --dest "C:/path/to/EchoVR-Personal-Disc-Everywhere-main"
"""
import argparse,shutil
from pathlib import Path

HERE=Path(__file__).resolve().parent
DIST=HERE.parent/'PersonalDiscTrainer/native/dist'

PS1 = r'''<#
Installs the Echo VR MUSIC tablet tab.

Runs after Personal Disc v3.0.1: the tablet runtime wraps that mod's two script
DLLs and pins their exact bytes, so this refuses to run against stock scripts.
#>
param([string]$GameRoot = '', [switch]$Restore)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

# Personal Disc v3.0.1. The tablet runtime is compiled against these exact bytes.
$discScripts = @{
    '351c49438bd38225.dll' = '5421de8a34703d9872df0fa3841443eee8a28577fb50461cc5c01e2911bcd911'
    '38965d90a823f03f.dll' = 'deca1e86cd437116a1afab07bcf228274d9d51274b385a92ab7568d346759c75'
}

function Test-GameRoot([string]$path) {
    if (!$path) { return $false }
    return Test-Path -LiteralPath (Join-Path $path 'bin\win10\echovr.exe')
}

function Find-Game {
    $roots = @()
    foreach ($variable in 'ProgramW6432', 'ProgramFiles', 'ProgramFiles(x86)') {
        $base = [Environment]::GetEnvironmentVariable($variable)
        if ($base) {
            foreach ($launcher in 'Meta Horizon', 'Oculus') {
                $roots += (Join-Path $base "$launcher\Software\Software\ready-at-dawn-echo-arena")
            }
        }
    }
    # Oculus libraries are commonly placed outside Program Files, on any drive.
    foreach ($drive in [IO.DriveInfo]::GetDrives()) {
        if (!$drive.IsReady) { continue }
        foreach ($tail in 'Oculus\Games\Software\Software\ready-at-dawn-echo-arena',
                          'Oculus\Software\Software\ready-at-dawn-echo-arena',
                          'Games\Oculus\Software\Software\ready-at-dawn-echo-arena') {
            $roots += (Join-Path $drive.RootDirectory $tail)
        }
    }
    $found = @()
    foreach ($root in $roots) {
        if ((Test-GameRoot $root) -and ($found -notcontains $root)) { $found += $root }
    }
    return $found
}

function Get-Sha([string]$path) {
    return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLower()
}

if (!(Test-GameRoot $GameRoot)) {
    $found = @(Find-Game)
    if ($found.Count -eq 1) {
        $GameRoot = $found[0]
        Write-Host "Found Echo VR: $GameRoot"
    } else {
        if ($found.Count -gt 1) {
            Write-Host 'Several Echo VR installations were found:'
            $found | ForEach-Object { Write-Host "  $_" }
        } else {
            Write-Host 'Could not find Echo VR automatically.'
        }
        $GameRoot = (Read-Host 'Paste your ready-at-dawn-echo-arena folder').Trim('"').Trim()
    }
}
if (!(Test-GameRoot $GameRoot)) {
    throw "Not an Echo VR folder (no bin\win10\echovr.exe): $GameRoot"
}
if (Get-Process -Name 'echovr' -ErrorAction SilentlyContinue) {
    throw 'Close Echo VR completely, then run this again.'
}

$installer = Join-Path $here 'Install.ps1'
$statePath = Join-Path $GameRoot 'bin\win10\EchoTabletTrainer.install.json'
$installed = Test-Path -LiteralPath $statePath

if ($Restore) {
    if (!$installed) { Write-Host 'The music tab is not installed; nothing to restore.'; exit }
    & $installer -Action Restore -GameRoot $GameRoot
    Write-Host ''
    Write-Host 'Music tab removed. Personal Disc is untouched.'
    exit
}

# The runtime hooks Personal Disc's gate functions and refuses to start if their
# bytes differ, which would disable the whole tablet, music tab included. When the
# tab is already installed the live scripts are our wrappers, so check the
# preserved originals instead.
$scriptDir = Join-Path $GameRoot 'bin\win10\scripts'
foreach ($name in $discScripts.Keys) {
    $live = Join-Path $scriptDir $name
    if ($installed) {
        $live = Join-Path $scriptDir ($name -replace '\.dll$', '.trainer-original.dll')
    }
    if (!(Test-Path -LiteralPath $live)) { throw "Missing script: $live" }
    if ((Get-Sha $live) -ne $discScripts[$name]) {
        Write-Host ''
        Write-Host 'Personal Disc v3.0.1 is not installed in this game folder.'
        Write-Host 'The music tab is built against it. Run INSTALL.cmd in this folder'
        Write-Host 'first, then run INSTALL_MUSIC.cmd again.'
        throw "Unexpected $name"
    }
}

& $installer -Action Verify -GameRoot $GameRoot

# The manifest merger will not overwrite tablet resources whose hashes it does not
# recognise, including ones an earlier build of this package installed, so an
# upgrade restores first.
if ($installed) {
    Write-Host 'A previous music tab is installed; removing it before upgrading.'
    & $installer -Action Restore -GameRoot $GameRoot
}
& $installer -Action Install -GameRoot $GameRoot
& $installer -Action Verify -GameRoot $GameRoot

Write-Host ''
Write-Host 'Music tab installed. Start Echo VR and open MUSIC on the hand tablet.'
Write-Host 'Have a player open (Spotify, YouTube Music, Apple Music) or the page'
Write-Host 'will read NOTHING PLAYING until one registers with Windows.'
'''

INSTALL_CMD = r'''@echo off
setlocal
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0music-tab\InstallMusic.ps1" %*
echo.
pause
'''

RESTORE_CMD = r'''@echo off
setlocal
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0music-tab\InstallMusic.ps1" -Restore %*
echo.
pause
'''

README = r'''# MUSIC tablet tab

Adds a **MUSIC** tab to the Echo VR hand tablet: what is playing, a progress bar,
and six big touch controls for Spotify, YouTube Music and Apple Music.

It drives whatever is playing through Windows' System Media Transport Controls —
the same mechanism behind the keyboard media keys — so there is no account
linking, no API key and no network access.

## Install

1. Close Echo VR completely.
2. Install **Personal Disc v3.0.1** first: run `INSTALL.cmd` in this folder.
   The music tab wraps that mod's scripts and is built against their exact bytes.
3. Run **`INSTALL_MUSIC.cmd`**.
4. Start Echo VR and open **MUSIC** on the hand tablet.

Start a player before putting the headset on, or the page reads `NOTHING PLAYING`
until one registers with Windows.

`INSTALL_MUSIC.cmd` finds the game itself, including Oculus libraries outside
Program Files. Pass a path if it cannot:

```powershell
powershell -ExecutionPolicy Bypass -File music-tab\InstallMusic.ps1 -GameRoot "C:\path\to\ready-at-dawn-echo-arena"
```

If you get **Access denied**, right-click `INSTALL_MUSIC.cmd` and choose
**Run as administrator**.

## Controls

| Control | Action |
| --- | --- |
| PREV / NEXT | Previous or next track |
| PLAY / PAUSE | Toggle playback; the label shows what a press will do |
| VOL − / VOL + | Move that app's volume by 5%, leaving Echo's own audio alone |
| Source chip | Switch players when several apps are playing |

## Remove

Run **`RESTORE_MUSIC.cmd`**. That removes the tab and leaves Personal Disc
installed. To remove Personal Disc as well, run `RESTORE.cmd` afterwards.

Installing backs up every file it replaces under
`bin\win10\EchoTabletTrainer-backup-*` in the game folder.

## What gets installed

| Path under the game folder | |
| --- | --- |
| `bin\win10\EchoTabletTrainer.dll` | the tablet runtime |
| `bin\win10\music\MusicWorker.exe` | reads Windows media state, out of Echo's process |
| `bin\win10\scripts\*.dll` | thin wrappers; the originals are kept as `.trainer-original.dll` |
| `_data\...\manifests`, `_data\...\packages` | the tablet page, merged in |

Installing also brings the shared **TOOLS** page with the Personal Disc toggle and
the Goalie trainer. Personal Disc defaults on, Goalie off.

Logs: `%LOCALAPPDATA%\EchoTabletTrainer\trainer.log` and
`%LOCALAPPDATA%\EchoTabletTrainer\Music\worker.log`.
'''


def publish(dest):
    dest=Path(dest)
    if not (DIST/'package.json').exists():
        raise SystemExit(f'No built package at {DIST}. Run package_native.py first.')
    if not (dest/'patch_personal_disc.py').exists():
        print(f'Warning: {dest} does not look like a Personal Disc folder.')
    out=dest/'music-tab'
    if out.exists(): shutil.rmtree(out)
    # .exp/.lib are link by-products, not part of the package manifest.
    shutil.copytree(DIST,out,ignore=shutil.ignore_patterns('*.exp','*.lib'))
    for rel,payload in [('music-tab/InstallMusic.ps1',PS1),('INSTALL_MUSIC.cmd',INSTALL_CMD),
                        ('RESTORE_MUSIC.cmd',RESTORE_CMD),('MUSIC-README.md',README)]:
        path=dest/rel
        path.write_bytes(payload.replace('\r\n', '\n').replace('\n', '\r\n').encode('utf-8'))
        print('wrote',path)
    print(f'Published the music-tab installer into {dest}')

if __name__=='__main__':
    ap=argparse.ArgumentParser()
    ap.add_argument('--dest',required=True,help='folder to publish into')
    publish(ap.parse_args().dest)
