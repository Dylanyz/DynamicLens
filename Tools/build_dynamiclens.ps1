<#
    build_dynamiclens.ps1 — build the DynamicLens plugin, and optionally install it.

    Purpose : RunUAT BuildPlugin into a temp package dir; -Install copies the result over this repo.
    Runs in : plain PowerShell. No Unreal editor needed for the build.
    Safety  : the BUILD is safe while the Unreal editor is running (it packages elsewhere).
              The INSTALL is not, because the editor holds a lock on the DLL. -Install refuses
              while UnrealEditor.exe is alive, by design. Never close the editor for someone:
              ask, wait for a yes, let them close it. See .claude/rules/editor-restarts.md.
    Verified: 2026-09-07 produced a working editor DLL on UE 5.8. Re-verify after an engine upgrade.

    Usage:
      Tools\build_dynamiclens.ps1                 # build only, leaves the package in $PackageDir
      Tools\build_dynamiclens.ps1 -Install        # build then install (editor must be closed)
      Tools\build_dynamiclens.ps1 -InstallOnly    # install an existing package without rebuilding
      Tools\build_dynamiclens.ps1 -Engine "C:\Program Files\Epic Games\UE_5.8"
#>
[CmdletBinding()]
param(
    [string] $Repo,
    [string] $Engine,
    [string] $PackageDir = "$env:TEMP\dlb",
    [switch] $Install,
    [switch] $InstallOnly
)

$ErrorActionPreference = "Stop"

# This script lives in <repo>\Tools, so the repo is its parent. No hard-coded user paths.
if (-not $Repo) { $Repo = Split-Path -Parent $PSScriptRoot }

# Engine: prefer the version the .uplugin targets, else the newest UE_* install found.
if (-not $Engine) {
    $want = $null
    $upJson = Get-Content (Join-Path $Repo "DynamicLens.uplugin") -Raw | ConvertFrom-Json
    if ($upJson.EngineVersion -match '^(\d+)\.(\d+)') { $want = "UE_$($Matches[1]).$($Matches[2])" }
    $roots = @("C:\Program Files\Epic Games", "D:\Program Files\Epic Games")
    $cands = @()
    foreach ($r in $roots) {
        if (Test-Path $r) { $cands += Get-ChildItem $r -Directory -Filter "UE_*" -ErrorAction SilentlyContinue }
    }
    $cands = $cands | Sort-Object Name -Descending
    $pick = $cands | Where-Object { $_.Name -eq $want } | Select-Object -First 1
    if (-not $pick) { $pick = $cands | Select-Object -First 1 }
    if (-not $pick) { throw "No Unreal Engine install found. Pass -Engine explicitly." }
    $Engine = $pick.FullName
    Write-Host "Engine: $Engine" -ForegroundColor DarkGray
}

function Test-EditorRunning {
    return [bool](Get-Process UnrealEditor -ErrorAction SilentlyContinue)
}

$uplugin = Join-Path $Repo "DynamicLens.uplugin"
$runUAT  = Join-Path $Engine "Engine\Build\BatchFiles\RunUAT.bat"
foreach ($p in @($uplugin, $runUAT)) {
    if (-not (Test-Path $p)) { throw "Not found: $p" }
}

# ---------------------------------------------------------------- build
if (-not $InstallOnly) {

    # UBT probes every host platform's AutoSDK and errors out when UE_SDKS_ROOT is unset or bogus.
    # An empty HostWin64 folder satisfies the probe without pretending we have Android/iOS SDKs.
    if (-not $env:UE_SDKS_ROOT -or -not (Test-Path $env:UE_SDKS_ROOT)) {
        $stub = Join-Path $env:LOCALAPPDATA "DynamicLensBuild\AutoSDK"
        New-Item -ItemType Directory -Force -Path (Join-Path $stub "HostWin64") | Out-Null
        $env:UE_SDKS_ROOT = $stub
        Write-Host "UE_SDKS_ROOT -> $stub (stub)" -ForegroundColor DarkGray
    }

    if (Test-Path $PackageDir) { Remove-Item $PackageDir -Recurse -Force }

    Write-Host "Building DynamicLens -> $PackageDir" -ForegroundColor Cyan
    & $runUAT BuildPlugin -Plugin="$uplugin" -Package="$PackageDir" -Rocket -TargetPlatforms=Win64
    if ($LASTEXITCODE -ne 0) { throw "BuildPlugin failed with exit code $LASTEXITCODE" }

    $dll = Join-Path $PackageDir "Binaries\Win64\UnrealEditor-DynamicLens.dll"
    if (-not (Test-Path $dll)) { throw "Build reported success but $dll is missing" }
    Write-Host ("BUILD OK  {0:yyyy-MM-dd HH:mm}  {1:N0} bytes" -f (Get-Item $dll).LastWriteTime, (Get-Item $dll).Length) -ForegroundColor Green
}

# ---------------------------------------------------------------- install
if (-not ($Install -or $InstallOnly)) {
    Write-Host ""
    Write-Host "Not installed. The package is waiting at $PackageDir." -ForegroundColor Yellow
    Write-Host "Installing needs the editor closed - ASK FIRST, then re-run with -InstallOnly." -ForegroundColor Yellow
    return
}

if (Test-EditorRunning) {
    throw "UnrealEditor.exe is running, so the plugin DLL is locked. Ask Dylan to close the editor, then re-run with -InstallOnly. Do not close it for him."
}

if (-not (Test-Path (Join-Path $PackageDir "Binaries\Win64"))) {
    throw "No built package at $PackageDir. Run without -InstallOnly first."
}

# Only Binaries\Win64 and Intermediate\Build come across. Copying the whole package would
# overwrite Content and Source with the packaged copies and blow away uncommitted work.
foreach ($sub in @("Binaries\Win64", "Intermediate\Build")) {
    $src = Join-Path $PackageDir $sub
    $dst = Join-Path $Repo $sub
    if (-not (Test-Path $src)) { continue }
    Write-Host "install $sub" -ForegroundColor Cyan
    robocopy $src $dst /E /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE) for $sub" }
}

$installed = Join-Path $Repo "Binaries\Win64\UnrealEditor-DynamicLens.dll"
Write-Host ("INSTALLED {0:yyyy-MM-dd HH:mm}  {1}" -f (Get-Item $installed).LastWriteTime, $installed) -ForegroundColor Green
Write-Host ""
Write-Host "Next: relaunch the editor, then re-run any dependent Python, e.g." -ForegroundColor Yellow
Write-Host "  import dynamiclens_tools as dl; dl.build_image_circle_material(force=True); dl.import_presets()" -ForegroundColor Yellow
