param(
    [switch]$DebugBuild
)

$ErrorActionPreference = "Stop"

function Find-ToolchainBin {
    $cmd = Get-Command clang.exe -ErrorAction SilentlyContinue
    if ($cmd) {
        return Split-Path $cmd.Source -Parent
    }
    $pkgRoot = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\MartinStorsjo.LLVM-MinGW.UCRT*" -Directory -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($pkgRoot) {
        $clangExe = Get-ChildItem $pkgRoot.FullName -Filter "clang.exe" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($clangExe) {
            return $clangExe.DirectoryName
        }
    }
    throw "LLVM-MinGW clang.exe not found in PATH or WinGet packages."
}

$binDir = Find-ToolchainBin
if (-not ($env:PATH -split ";" | Where-Object { $_ -eq $binDir })) {
    $env:PATH = "$binDir;$env:PATH"
}
Write-Host "Using toolchain: $binDir"

New-Item -ItemType Directory -Force -Path "build" | Out-Null
New-Item -ItemType Directory -Force -Path "build\obj_ui" | Out-Null
New-Item -ItemType Directory -Force -Path "build\obj_app" | Out-Null

# Remove legacy lowercase build\octans.exe so NTFS preserves exact Octans.exe casing
Get-ChildItem "build" -Filter "octans.exe" -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -ceq "octans.exe" } |
    ForEach-Object { Remove-Item -Force $_.FullName }

$coreSources = @(
    "core\hid_enum.c",
    "core\hid_dev.c",
    "core\hid_parse.c",
    "core\hotplug.c",
    "core\bluetooth.c",
    "core\xinput.c"
)
$engineSources = @(
    "engine\frame.c",
    "engine\hidpp.c",
    "engine\family_request.c",
    "engine\user_rules.c",
    "engine\broad_scan.c",
    "engine\merge.c",
    "engine\engine.c"
)
$uiSources = @(
    "ui\ui_theme.cpp",
    "ui\ui_fonts.cpp",
    "ui\ui_brand.cpp",
    "ui\ui_glyphs.cpp",
    "ui\ui_tray.cpp",
    "ui\ui_rename_dlg.cpp",
    "ui\ui_menu.cpp",
    "ui\ui_panel.cpp",
    "ui\ui_api.cpp"
)

$backendLibs = @("-lhid", "-lsetupapi", "-lcfgmgr32", "-ladvapi32", "-lole32", "-luuid")
$uiLibs      = @("-ld2d1", "-ldwrite", "-ld3d11", "-ldxgi", "-ldcomp", "-ldwmapi", "-lshell32", "-luser32", "-lgdi32", "-lole32", "-ladvapi32", "-lwinmm", "-luuid")

Write-Host "`n[1/7] Checking include/octans_ui_api.h C++ compatibility..."
& clang++ -std=c++17 -fsyntax-only -Wall -Wextra -Werror -x c++ -Iinclude include\octans_ui_api.h
if ($LASTEXITCODE -ne 0) { throw "octans_ui_api.h C++ syntax check failed." }

Write-Host "[2/7] Building build\octans-cli.exe (Headless Release -Os)..."
$cliArgs = @("-std=c11", "-Os", "-Wall", "-Wextra", "-Werror", "-ffunction-sections", "-fdata-sections", "-Iinclude") +
           $coreSources + $engineSources + @("cli\main.c", "-o", "build\octans-cli.exe", "-Wl,--gc-sections", "-s") + $backendLibs
& clang @cliArgs
if ($LASTEXITCODE -ne 0) { throw "Failed to build build\octans-cli.exe." }

Write-Host "[3/7] Building & running build\test_engine.exe..."
$testArgs = @("-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Iinclude") +
            $coreSources + $engineSources + @("tests\test_engine.c", "-o", "build\test_engine.exe") + $backendLibs
& clang @testArgs
if ($LASTEXITCODE -ne 0) { throw "Failed to build build\test_engine.exe." }

& .\build\test_engine.exe
if ($LASTEXITCODE -ne 0) { throw "Unit tests failed." }

Write-Host "`n[4/7] Building build\liboctans_ui.a (Native Direct2D/DComp UI)..."
$uiFlags = @(
    "-std=c++17", "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics",
    "-Wall", "-Wextra", "-Wshadow",
    "-DUNICODE", "-D_UNICODE", "-DWIN32_LEAN_AND_MEAN", "-DNOMINMAX",
    "-Iinclude", "-Iui"
)
if ($DebugBuild) {
    $uiFlags += @("-O0", "-g")
} else {
    $uiFlags += @("-Os", "-ffunction-sections", "-fdata-sections")
}

$uiObjs = @()
foreach ($src in $uiSources) {
    $name = [System.IO.Path]::GetFileNameWithoutExtension($src)
    $objPath = "build\obj_ui\$name.o"
    & clang++ @uiFlags -c $src -o $objPath
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed for $src" }
    $uiObjs += $objPath
}

$libOut = "build\liboctans_ui.a"
if (Test-Path $libOut) { Remove-Item -Force $libOut }
& llvm-ar rcs $libOut @uiObjs
if ($LASTEXITCODE -ne 0) { throw "Archive failed for $libOut" }

Write-Host "[5/7] Building build\Octans.exe (Combined Tray App)..."
$resObj = "build\obj_app\octans_res.o"
& windres -I app -I assets app\octans.rc -O coff -o $resObj
if ($LASTEXITCODE -ne 0) { throw "Resource compilation failed for app\octans.rc" }

$cFlags = @("-std=c11", "-Wall", "-Wextra", "-Werror", "-Iinclude")
if ($DebugBuild) {
    $cFlags += @("-O0", "-g")
} else {
    $cFlags += @("-Os", "-ffunction-sections", "-fdata-sections")
}

$appObjs = @($resObj)
foreach ($src in ($coreSources + $engineSources + @("app\app_main.c"))) {
    $name = [System.IO.Path]::GetFileNameWithoutExtension($src)
    $objPath = "build\obj_app\$name.o"
    & clang @cFlags -c $src -o $objPath
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed for $src" }
    $appObjs += $objPath
}

$linkFlags = @("-mwindows", "-municode", "-nostdlib++", "-Wl,--gc-sections")
if (-not $DebugBuild) {
    $linkFlags += "-Wl,-s"
}
$allLibs = ($backendLibs + $uiLibs) | Select-Object -Unique
& clang++ @linkFlags @appObjs $libOut @allLibs -o "build\Octans.exe"
if ($LASTEXITCODE -ne 0) { throw "Link failed for build\Octans.exe" }

# Stage private Anthropic Serif font beside executable
$srcFont = "ui\fonts\AnthropicSerifWebText.ttf"
if (Test-Path $srcFont) {
    New-Item -ItemType Directory -Force -Path "build\fonts" | Out-Null
    Copy-Item -Force $srcFont "build\fonts\AnthropicSerifWebText.ttf"
}

Write-Host "`n[6/7] Checking binary footprints..."
$cliItem = Get-Item "build\octans-cli.exe"
$appItem = Get-Item "build\Octans.exe"
Write-Host ("  build\octans-cli.exe : {0} bytes ({1} KB)" -f $cliItem.Length, [math]::Round($cliItem.Length / 1024.0, 1))
Write-Host ("  build\Octans.exe     : {0} bytes ({1} KB)" -f $appItem.Length, [math]::Round($appItem.Length / 1024.0, 1))
if ($cliItem.Length -gt 307200) {
    throw "octans-cli.exe size exceeded 300 KB budget!"
}
if ($appItem.Length -gt 358400) {
    throw "Octans.exe size exceeded 350 KB budget!"
}

Write-Host "[7/7] Checking DLL imports (offline & zero non-system dependencies)..."
foreach ($bin in @("build\octans-cli.exe", "build\Octans.exe")) {
    Write-Host "  $bin imports:"
    $imports = & llvm-objdump -p $bin | Select-String "DLL Name:"
    $imports | ForEach-Object { Write-Host ("    " + $_.Line.Trim()) }
    if ($imports -match "ws2_32|winhttp|wininet|libstdc\+\+|libgcc_s|tdh\.dll") {
        throw "Forbidden network, ETW, or C++ runtime DLL imported by $bin!"
    }
}

Write-Host "`nALL BUILD & VERIFICATION CHECKS PASSED." -ForegroundColor Green
