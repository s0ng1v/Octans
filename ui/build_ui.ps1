param(
    [switch]$DebugBuild
)

$ErrorActionPreference = "Stop"

$RootDir = Split-Path -Parent $PSScriptRoot
$UiDir   = $PSScriptRoot
$OutDir  = Join-Path $RootDir "build"
$ObjDir  = Join-Path $OutDir "obj_ui"

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
New-Item -ItemType Directory -Force -Path $ObjDir | Out-Null

# Locate LLVM-MinGW (UCRT) clang++ toolchain
$ClangExe = $null
$ArExe = $null
$ObjdumpExe = $null
$NmExe = $null

$WinGetRoots = @(
    (Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Packages\MartinStorsjo.LLVM-MinGW.UCRT*\llvm-mingw-*\bin"),
    "C:\llvm-mingw\bin",
    "D:\llvm-mingw\bin"
)

foreach ($pattern in $WinGetRoots) {
    $dirs = Get-ChildItem -Path $pattern -ErrorAction SilentlyContinue
    foreach ($d in $dirs) {
        $cand = Join-Path $d.FullName "clang++.exe"
        if (Test-Path $cand) {
            $ClangExe   = $cand
            $ArExe      = Join-Path $d.FullName "llvm-ar.exe"
            $ObjdumpExe = Join-Path $d.FullName "llvm-objdump.exe"
            $NmExe      = Join-Path $d.FullName "llvm-nm.exe"
            break
        }
    }
    if ($ClangExe) { break }
}

if (-not $ClangExe) {
    $cmd = Get-Command clang++ -ErrorAction SilentlyContinue
    if ($cmd) {
        $ClangExe = $cmd.Source
        $binDir = Split-Path -Parent $ClangExe
        $ArExe = Join-Path $binDir "llvm-ar.exe"
        $ObjdumpExe = Join-Path $binDir "llvm-objdump.exe"
        $NmExe = Join-Path $binDir "llvm-nm.exe"
    }
}

if (-not $ClangExe) {
    throw "Could not find LLVM-MinGW (UCRT) clang++.exe. Please install MartinStorsjo.LLVM-MinGW.UCRT."
}

Write-Host "[Octans UI] Toolchain: $ClangExe" -ForegroundColor Cyan

$CommonFlags = @(
    "-std=c++17",
    "-fno-exceptions",
    "-fno-rtti",
    "-fno-threadsafe-statics",
    "-Wall",
    "-Wextra",
    "-Wshadow",
    "-DUNICODE",
    "-D_UNICODE",
    "-DWIN32_LEAN_AND_MEAN",
    "-DNOMINMAX",
    "-I$RootDir\include",
    "-I$UiDir"
)

if ($DebugBuild) {
    $CommonFlags += @("-O0", "-g")
} else {
    $CommonFlags += @("-Os", "-ffunction-sections", "-fdata-sections")
}

$LibSources = @(
    "ui_theme.cpp",
    "ui_fonts.cpp",
    "ui_brand.cpp",
    "ui_glyphs.cpp",
    "ui_tray.cpp",
    "ui_rename_dlg.cpp",
    "ui_updater.cpp",
    "ui_about_dlg.cpp",
    "ui_menu.cpp",
    "ui_panel.cpp",
    "ui_api.cpp"
)

$LibObjs = @()
foreach ($src in $LibSources) {
    $srcPath = Join-Path $UiDir $src
    $objPath = Join-Path $ObjDir ($src -replace "\.cpp$", ".o")
    Write-Host "  [CC] ui/$src"
    & $ClangExe @CommonFlags -c $srcPath -o $objPath
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed for $src" }
    $LibObjs += $objPath
}

# Build static library for seamless backend linkage
$LibOut = Join-Path $OutDir "liboctans_ui.a"
if (Test-Path $LibOut) { Remove-Item -Force $LibOut }
Write-Host "  [AR] build/liboctans_ui.a"
& $ArExe rcs $LibOut @LibObjs
if ($LASTEXITCODE -ne 0) { throw "Archive failed for liboctans_ui.a" }

# Compile standalone demo harness
$DemoObj = Join-Path $ObjDir "demo_main.o"
Write-Host "  [CC] ui/demo_main.cpp"
& $ClangExe @CommonFlags -c (Join-Path $UiDir "demo_main.cpp") -o $DemoObj
if ($LASTEXITCODE -ne 0) { throw "Compilation failed for demo_main.cpp" }

# Link standalone demo using pure Windows system libs (NO libstdc++ / NO network DLLs)
$DemoExe = Join-Path $OutDir "octans_ui_demo.exe"
$LinkFlags = @(
    "-mwindows",
    "-municode",
    "-nostdlib++",
    "-Wl,--gc-sections"
)
if (-not $DebugBuild) {
    $LinkFlags += "-Wl,-s"
}

$SysLibs = @(
    "-ld2d1",
    "-ldwrite",
    "-ld3d11",
    "-ldxgi",
    "-ldcomp",
    "-ldwmapi",
    "-lshell32",
    "-luser32",
    "-lgdi32",
    "-lole32",
    "-ladvapi32",
    "-lwinmm",
    "-luuid"
)

Write-Host "  [LD] build/octans_ui_demo.exe"
& $ClangExe @LinkFlags $DemoObj $LibOut @SysLibs -o $DemoExe
if ($LASTEXITCODE -ne 0) { throw "Link failed for octans_ui_demo.exe" }

# Stage lightweight private Anthropic Serif font beside executable for portable loading
$SrcFont = Join-Path $UiDir "fonts\AnthropicSerifWebText.ttf"
if (Test-Path $SrcFont) {
    $OutFontDir = Join-Path $OutDir "fonts"
    New-Item -ItemType Directory -Force -Path $OutFontDir | Out-Null
    Copy-Item -Force $SrcFont (Join-Path $OutFontDir "AnthropicSerifWebText.ttf")
}

Write-Host ""
Write-Host "[Octans UI] Build succeeded!" -ForegroundColor Green
Get-Item $LibOut, $DemoExe | Format-Table Name, @{Label="KB"; Expression={[math]::Round($_.Length / 1024, 1)}} -AutoSize

if (Test-Path $ObjdumpExe) {
    Write-Host "[Octans UI] DLL Imports:" -ForegroundColor Cyan
    & $ObjdumpExe -p $DemoExe | Select-String "DLL Name:"
}

if (Test-Path $NmExe) {
    Write-Host "[Octans UI] Exported C ABI Symbols:" -ForegroundColor Cyan
    & $NmExe --defined-only $LibOut | Select-String "T octans_ui_"
}
