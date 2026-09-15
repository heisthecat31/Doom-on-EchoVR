param(
    [ValidateSet('Install','Verify','Restore')][string]$Action='Install',
    [string]$GameRoot
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
if (!$GameRoot) {
    foreach ($candidate in @('C:\Program Files\Meta Horizon\Software\Software\ready-at-dawn-echo-arena','C:\echovr\ready-at-dawn-echo-arena')) {
        if (Test-Path -LiteralPath (Join-Path $candidate 'bin\win10\echovr.exe')) { $GameRoot=$candidate; break }
    }
}
if (!$GameRoot) { throw 'Pass -GameRoot with your ready-at-dawn-echo-arena folder.' }
$GameRoot=[IO.Path]::GetFullPath($GameRoot).TrimEnd('\')
$bin=Join-Path $GameRoot 'bin\win10'
$data=Join-Path $GameRoot '_data\5932408047\rad15\win10'
$statePath=Join-Path $bin 'EchoTabletTrainer.install.json'
$manifest=Join-Path $data 'manifests\48037dc70b0ecab2'
$exe=Join-Path $bin 'echovr.exe'
function GameRunning {
    @((Get-Process echovr -ErrorAction SilentlyContinue) | Where-Object {$_.Path -eq $exe}).Count -gt 0
}
function Hash([string]$path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Target([string]$relative) {
    $path=[IO.Path]::GetFullPath((Join-Path $GameRoot $relative))
    if (!$path.StartsWith($GameRoot+'\',[StringComparison]::OrdinalIgnoreCase)) {throw 'Target escapes game directory'}
    return $path
}
function CopyAtomic([string]$source,[string]$target) {
    $pending=$target+'.trainer-pending'
    if (Test-Path -LiteralPath $pending) {throw "Unfinished write exists: $pending"}
    Copy-Item -LiteralPath $source -Destination $pending
    if ((Hash $source) -ne (Hash $pending)) {throw 'Copy verification failed'}
    if (Test-Path -LiteralPath $target) { [IO.File]::Replace($pending,$target,[NullString]::Value) }
    else { [IO.File]::Move($pending,$target) }
}
if ($Action -eq 'Restore') {
    if (GameRunning) {throw 'Close Echo VR before restoring.'}
    $state=Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
    foreach ($entry in $state.files) {
        $target=Target $entry.relative
        if (!(Test-Path -LiteralPath $target) -or (Hash $target) -ne $entry.after) {throw "File changed since install: $target"}
        if ($entry.existed -and (Hash $entry.backup) -ne $entry.before) {throw 'Backup hash mismatch'}
    }
    foreach ($entry in $state.files) {
        $target=Target $entry.relative
        if ($entry.existed) {CopyAtomic $entry.backup $target}
        else {Remove-Item -LiteralPath $target}
    }
    Move-Item -LiteralPath $statePath -Destination ($statePath+'.restored-'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ'))
    Write-Host 'Original scripts and manifest restored. Backups retained.'
    exit
}
$package=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'package.json') -Raw | ConvertFrom-Json
foreach ($file in $package.files) {
    if ((Hash (Join-Path $PSScriptRoot $file.path)) -ne $file.sha256) {throw "Package file changed: $($file.path)"}
}
$exe=Join-Path $bin 'echovr.exe'
$bytes=[IO.File]::ReadAllBytes($exe)
$pe=[BitConverter]::ToInt32($bytes,60)
if ([BitConverter]::ToUInt32($bytes,$pe+8) -ne $package.exe_timestamp -or [BitConverter]::ToUInt32($bytes,$pe+24+56) -ne $package.exe_size) {throw 'Unsupported Echo executable version.'}
$active=$null
if (Test-Path -LiteralPath $statePath) {
    $active=Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
    foreach ($entry in $active.files) {
        if ((Hash (Target $entry.relative)) -ne $entry.after) {throw "Installed file changed; refusing to overwrite: $($entry.relative)"}
    }
} else {
    foreach ($script in $package.scripts) {
        $sha=Hash (Join-Path $bin ('scripts\'+$script.name))
        if ($sha -notin $script.accepted) {throw "Unsupported script: $($script.name). No files changed."}
    }
}
if ($Action -eq 'Verify') {Write-Host 'Package, game version and script baselines verified.';exit}
if (GameRunning) {throw 'Close Echo VR before installing.'}
$stamp=[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
$staging=Join-Path $env:TEMP ('EchoTabletTrainer-'+$stamp)
New-Item -ItemType Directory -Path $staging | Out-Null
& (Join-Path $PSScriptRoot 'manifest_merge.exe') $manifest (Join-Path $PSScriptRoot 'tablet.patch') $staging
if ($LASTEXITCODE -ne 0) {throw 'Tablet manifest merge refused. No game files changed.'}
$copies=@()
foreach ($file in $package.files) {
    if ($file.path -eq 'EchoTabletTrainer.dll' -or $file.path.StartsWith('scripts/') -or $file.path.StartsWith('doom/')) {
        $copies+=@{relative='bin\win10\'+$file.path.Replace('/','\');source=(Join-Path $PSScriptRoot $file.path)}
    }
}
foreach ($file in Get-ChildItem -LiteralPath $staging -File) {
    if ($file.Name -eq 'manifest') {$relative='_data\5932408047\rad15\win10\manifests\48037dc70b0ecab2'}
    else {$relative='_data\5932408047\rad15\win10\packages\'+$file.Name}
    $copies+=@{relative=$relative;source=$file.FullName}
}
# Prepare and verify every backup before replacing the first game file.
foreach ($copy in $copies) {
    $target=Target $copy.relative
    $pending=$target+'.trainer-pending'
    if (Test-Path -LiteralPath $pending) {
        if ((Hash $pending) -ne (Hash $copy.source)) {throw "Unrecognized pending file: $pending"}
        Move-Item -LiteralPath $pending -Destination (Join-Path $staging ([IO.Path]::GetFileName($pending)))
    }
}
$backup=Join-Path $bin ('EchoTabletTrainer-backup-'+$stamp)
New-Item -ItemType Directory -Path $backup | Out-Null
$records=@();$rollback=@();$index=0
foreach ($copy in $copies) {
    $target=Target $copy.relative
    $parent=Split-Path -Parent $target
    if (!(Test-Path -LiteralPath $parent)) {New-Item -ItemType Directory -Path $parent | Out-Null}
    $exists=Test-Path -LiteralPath $target
    $saved=Join-Path $backup ($index.ToString()+'.before');$index++
    $before=$null
    if ($exists) {Copy-Item -LiteralPath $target -Destination $saved;$before=Hash $target;if ((Hash $saved) -ne $before) {throw 'Backup verification failed'}}
    $after=Hash $copy.source
    $record=@{relative=$copy.relative;existed=$exists;backup=$saved;before=$before;after=$after}
    $rollback+=$record.Clone()
    if ($active) {
        $old=@($active.files | Where-Object {$_.relative -eq $copy.relative})
        if ($old.Count) {$record.existed=$old[0].existed;$record.backup=$old[0].backup;$record.before=$old[0].before}
    }
    $records+=$record
}
if ($active) {
    foreach ($old in $active.files) {
        if (!@($records | Where-Object {$_.relative -eq $old.relative}).Count) {$records+=$old}
    }
}
try {
    foreach ($copy in $copies) {CopyAtomic $copy.source (Target $copy.relative)}
    foreach ($record in $records) {if ((Hash (Target $record.relative)) -ne $record.after) {throw 'Installed file verification failed'}}
    $state=@{version=$package.version;installed=$stamp;game=$GameRoot;files=$records}
    $state | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $staging 'state.json') -Encoding UTF8
    CopyAtomic (Join-Path $staging 'state.json') $statePath
} catch {
    $failure=$_
    Write-Warning ('Install failed: '+$failure.Exception.Message)
    foreach ($record in $rollback) {
        $target=Target $record.relative
        $pending=$target+'.trainer-pending'
        if (Test-Path -LiteralPath $pending) {
            if ((Hash $pending) -eq $record.after) {Remove-Item -LiteralPath $pending}
            else {Write-Warning "Unknown pending file requires review: $pending";continue}
        }
        if ($record.existed) {CopyAtomic $record.backup $target}
        elseif (Test-Path -LiteralPath $target) {Remove-Item -LiteralPath $target}
    }
    throw $failure
}
Write-Host 'Installed. Start Echo VR normally. No Python or separate trainer process is needed.'
Write-Host 'Personal Disc defaults ON. Goalie defaults OFF; arena only, permitted phases, 60 seconds.'
Write-Host 'Log: %LOCALAPPDATA%\EchoTabletTrainer\trainer.log'
