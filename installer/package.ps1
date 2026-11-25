param(
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$RootDir = Split-Path -Parent $PSScriptRoot
Push-Location $RootDir
try {
    # 1. Read canonical version from include/octans_version.h
    $verHeader = Get-Content -Raw "include\octans_version.h"
    if ($verHeader -notmatch '#define\s+OCTANS_VERSION_STR\s+"([^"]+)"') {
        throw "Could not parse OCTANS_VERSION_STR from include\octans_version.h"
    }
    $appVersion = $Matches[1]
    Write-Host "[Octans Packager] Target version: v$appVersion" -ForegroundColor Cyan

    # 2. Build fresh binaries & run tests unless -SkipBuild is passed
    if (-not $SkipBuild) {
        Write-Host "[Octans Packager] Running build.ps1..." -ForegroundColor Cyan
        & powershell -ExecutionPolicy Bypass -File ".\build.ps1"
        if ($LASTEXITCODE -ne 0) { throw "build.ps1 failed." }
    }

    foreach ($req in @("build\Octans.exe", "build\octans-cli.exe", "build\fonts\AnthropicSerifWebText.ttf", "assets\octans.ico")) {
        if (-not (Test-Path $req)) {
            throw "Missing required staging asset: $req"
        }
    }

    # 3. Locate Inno Setup ISCC.exe
    $isccExe = $null
    $cmd = Get-Command iscc.exe -ErrorAction SilentlyContinue
    if ($cmd) {
        $isccExe = $cmd.Source
    } else {
        $candidates = @(
            "${env:LOCALAPPDATA}\Programs\Inno Setup 6\ISCC.exe",
            "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
            "${env:ProgramFiles}\Inno Setup 6\ISCC.exe"
        )
        foreach ($cand in $candidates) {
            if (Test-Path $cand) {
                $isccExe = $cand
                break
            }
        }
    }
    if (-not $isccExe) {
        throw "Inno Setup 6 ISCC.exe not found. Install via: winget install JRSoftware.InnoSetup"
    }
    Write-Host "[Octans Packager] Using ISCC: $isccExe" -ForegroundColor Cyan

    # 4. Ensure dist/ exists and compile installer
    New-Item -ItemType Directory -Force -Path "dist" | Out-Null
    & $isccExe "/DMyAppVersion=$appVersion" "installer\octans.iss"
    if ($LASTEXITCODE -ne 0) { throw "ISCC compilation failed." }

    $outExe = Join-Path $RootDir "dist\Octans-Setup-v$appVersion.exe"
    if (-not (Test-Path $outExe)) {
        throw "Expected installer not produced: $outExe"
    }

    $item = Get-Item $outExe
    $hash = (Get-FileHash $outExe -Algorithm SHA256).Hash
    $sizeKB = [math]::Round($item.Length / 1024.0, 1)
    Write-Host ""
    Write-Host "[Octans Packager] Installer ready!" -ForegroundColor Green
    Write-Host "  File   : dist\Octans-Setup-v$appVersion.exe"
    Write-Host "  Size   : $($item.Length) bytes ($sizeKB KB)"
    Write-Host "  SHA256 : $hash"
} finally {
    Pop-Location
}
