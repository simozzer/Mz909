# Mz909 - an emulation of the TR-909 Rhythm Composer
# Created by Simon Moscrop
#
# This program is free software: you can redistribute it and/or modify it under the terms of
# the GNU Affero General Public License as published by the Free Software Foundation, version 3.
# It is distributed WITHOUT ANY WARRANTY; see the LICENSE file for the full terms.

<#
    Build the plugin, after holding the engine to its checks.

        .\build.ps1                  engine check, then the VST3 and the standalone
        .\build.ps1 -NoPlugin        engine check only, for iterating on the DSP
        .\build.ps1 -Wav <dir>       and write demo patterns to <dir> to listen to
        .\build.ps1 -Release         and the files a release attaches, into dist\: the installer,
                                     and a zip each of the VST3 and the standalone

    The check gates the plugin rather than standing in for it: if the engine is out of tune
    or blows up, there is no point wrapping a plugin round it, and if it passes the plugin is
    built from those same sources straight after.

    cl.exe is never on the PATH - vcvars64.bat sets up INCLUDE, LIB and PATH for it, and
    vswhere says where Visual Studio is rather than a versioned path being written down here.
#>
param(
    [switch]$NoPlugin,
    [switch]$Release,
    [string]$Wav = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    Write-Error "No Visual Studio found. Install VS Community with 'Desktop development with C++'."
}

$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                     -property installationPath
if (-not $vsPath) {
    Write-Error "Visual Studio is installed but the C++ tools are not. Add 'Desktop development with C++'."
}

$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"

# ---------------------------------------------------------------- the engine check

$out = Join-Path $env:TEMP "mz909check"
New-Item -ItemType Directory -Force -Path $out | Out-Null

$exe = Join-Path $out "DspCheck.exe"
$sources = @(
    (Join-Path $root "Tests\DspCheck.cpp"),
    (Join-Path $root "Source\TR909\Engine.cpp"),
    (Join-Path $root "Source\TR909\Roms.cpp")
)
$include = Join-Path $root "Source\TR909"
$quoted = ($sources | ForEach-Object { "`"$_`"" }) -join " "

Write-Host "building the engine check"
cmd /c "call `"$vcvars`" >nul 2>&1 && cd /d `"$out`" && cl /nologo /std:c++17 /EHsc /W4 /O2 /fp:fast /I `"$include`" $quoted /Fe:`"$exe`"" |
    Where-Object { $_ -match "error|warning" }
if ($LASTEXITCODE -ne 0) { Write-Error "the engine check did not build" }

if ($Wav -ne "") {
    New-Item -ItemType Directory -Force -Path $Wav | Out-Null
    & $exe --wav (Resolve-Path $Wav)
} else {
    & $exe
}
if ($LASTEXITCODE -ne 0) { Write-Error "the engine failed its checks - not building a plugin around it" }

if ($NoPlugin) { exit 0 }

# ---------------------------------------------------------------- the plugin

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $bundled = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if (Test-Path $bundled) { $cmake = $bundled }
}
if (-not $cmake) { Write-Error "No cmake on the PATH and none inside Visual Studio." }

$buildDir = Join-Path $root "build"

if (-not (Test-Path (Join-Path $buildDir "CMakeCache.txt"))) {
    Write-Host ""
    Write-Host "configuring $buildDir (first time)"
    & $cmake -S $root -B $buildDir
    if ($LASTEXITCODE -ne 0) { Write-Error "cmake configure failed" }
}

Write-Host ""
Write-Host "building the VST3 and the standalone"

# The version and copyright are written down once, in CMakeLists.txt, and the binaries and
# installer are stamped from them. JUCE writes the Windows version resource once and never
# again, so when either changes the old one is thrown away here, or the binaries go on
# saying the old one.
$cmakeLists = Get-Content (Join-Path $root "CMakeLists.txt") -Raw
if ($cmakeLists -notmatch "project\(Mz909 VERSION ([0-9.]+)") { Write-Error "no version in CMakeLists.txt" }
$version = $Matches[1]
if ($cmakeLists -notmatch 'set\(MZ909_COPYRIGHT\s+"([^"]+)"\)') { Write-Error "no MZ909_COPYRIGHT in CMakeLists.txt" }
$copyright = $Matches[1]

$rc = Join-Path $buildDir "Mz909_artefacts\JuceLibraryCode\Mz909_resources.rc"
if ((Test-Path $rc) -and -not ((Select-String -Path $rc -SimpleMatch "`"$version\0`"" -Quiet) -and
                               (Select-String -Path $rc -SimpleMatch "`"$copyright\0`"" -Quiet))) {
    Remove-Item $rc
}

& $cmake --build $buildDir --config Release --target Mz909_VST3 Mz909_Standalone -- /m /v:minimal |
    Where-Object { $_ -match "error|warning C|installed to|could not be" }
if ($LASTEXITCODE -ne 0) { Write-Error "the plugin build failed" }

# Report what is on disk, and when it was written, rather than that the build returned zero.
$artefacts = @(
    (Join-Path $buildDir "Mz909_artefacts\Release\VST3\Mz909.vst3\Contents\x86_64-win\Mz909.vst3"),
    (Join-Path $buildDir "Mz909_artefacts\Release\Standalone\Mz909.exe")
)

$newest = Get-ChildItem (Join-Path $root "Source") -Recurse -Include *.cpp, *.h |
          Sort-Object LastWriteTime -Descending | Select-Object -First 1

Write-Host ""
foreach ($a in $artefacts) {
    if (-not (Test-Path $a)) { Write-Error "expected $a and it is not there" }
    $f = Get-Item $a
    $stale = $f.LastWriteTime -lt $newest.LastWriteTime
    Write-Host ("ok  -  {0,-12} {1,7} KB   {2}{3}" -f $f.Name, [math]::Round($f.Length / 1KB), $f.LastWriteTime,
                $(if ($stale) { "   *** OLDER THAN $($newest.Name) ***" } else { "" })) `
               -ForegroundColor $(if ($stale) { "Red" } else { "Green" })
    if ($stale) { Write-Error "an artefact is older than the sources it was built from" }
}

if (-not $Release) { exit 0 }

# ---------------------------------------------------------------- the installer

$iscc = (Get-Command iscc -ErrorAction SilentlyContinue).Source
if (-not $iscc) {
    $iscc = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe") |
            Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $iscc) { Write-Error "No Inno Setup 6. Install it from https://jrsoftware.org/isdl.php" }

Write-Host ""
Write-Host "building the installer for $version"

& $iscc /Q "/DAppVersion=$version" "/DAppCopyright=$copyright" (Join-Path $root "installer\Mz909.iss")
if ($LASTEXITCODE -ne 0) { Write-Error "the installer build failed" }

# ---------------------------------------------------------------- the zips, for installing by hand

Write-Host "building the zips"

$dist = Join-Path $root "dist"
$stage = Join-Path $buildDir "zip"
$readme = (Get-Content (Join-Path $root "installer\README.txt") -Raw).Replace("@VERSION@", $version).Replace("@COPYRIGHT@", $copyright)
$bundles = @{
    "VST3"       = Join-Path $buildDir "Mz909_artefacts\Release\VST3\Mz909.vst3"
    "Standalone" = Join-Path $buildDir "Mz909_artefacts\Release\Standalone\Mz909.exe"
}

foreach ($kind in $bundles.Keys) {
    $d = Join-Path $stage $kind
    if (Test-Path $d) { Remove-Item $d -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $d | Out-Null
    Copy-Item $bundles[$kind] $d -Recurse
    Copy-Item (Join-Path $root "LICENSE") $d
    Set-Content (Join-Path $d "README.txt") $readme -NoNewline

    $zip = Join-Path $dist "Mz909-$version-win64-$kind.zip"
    if (Test-Path $zip) { Remove-Item $zip }
    Compress-Archive -Path (Join-Path $d "*") -DestinationPath $zip
}

# Report what is on disk, as for the build.
Write-Host ""
foreach ($kind in "Setup.exe", "VST3.zip", "Standalone.zip") {
    $f = Get-Item (Join-Path $dist "Mz909-$version-win64-$kind")
    Write-Host ("ok  -  {0,-34} {1,5:N1} MB   {2}" -f $f.Name, ($f.Length / 1MB), $f.LastWriteTime) -ForegroundColor Green
}

exit 0
