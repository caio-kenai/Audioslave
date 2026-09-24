# Builds Audioslave (CMake + Ninja + MSVC), runs the unit tests and creates
# the release artefacts in dist\.
#
#   powershell -ExecutionPolicy Bypass -File scripts\build.ps1 [-Debug] [-SkipTests] [-Integration] [-NoDist]
#
# -Integration also runs the read-only Windows audio integration tests.
param(
    [switch]$Debug,
    [switch]$SkipTests,
    [switch]$Integration,
    [switch]$NoDist
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$buildType = if ($Debug) { "Debug" } else { "Release" }
$buildDir = Join-Path $root ("build-" + $buildType.ToLower())

if (-not (Test-Path (Join-Path $root "external\JUCE\CMakeLists.txt"))) {
    Write-Host "Fetching the JUCE submodule..."
    git -C $root submodule update --init --recursive
    if ($LASTEXITCODE -ne 0) { throw "git submodule update failed." }
}

# Visual Studio 2022 (any edition, or Build Tools) provides cl, rc, cmake and ninja.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = $null
if (Test-Path $vswhere) {
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (-not $vs) { throw "Visual Studio 2022 with the C++ workload was not found." }
$vcvars = "$vs\VC\Auxiliary\Build\vcvars64.bat"
$tools = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake"
$env:PATH = "$tools\CMake\bin;$tools\Ninja;$env:PATH"

function Invoke-VS([string]$cmd) {
    cmd /c "call `"$vcvars`" >nul 2>&1 && $cmd"
    if ($LASTEXITCODE -ne 0) { throw "Command failed: $cmd" }
}

Push-Location $root
try {
    Invoke-VS "cmake -G Ninja -S . -B `"$buildDir`" -DCMAKE_BUILD_TYPE=$buildType"
    Invoke-VS "cmake --build `"$buildDir`""

    if (-not $SkipTests) {
        $testArgs = @()
        if ($Integration) { $testArgs += "--integration" }
        & "$buildDir\bin\audioslave_tests.exe" @testArgs
        if ($LASTEXITCODE -ne 0) { throw "Tests failed." }
    }

    if (-not $NoDist) {
        Invoke-VS "cmake --build `"$buildDir`" --target dist"
    }

    Write-Host ""
    Write-Host "Audioslave ($buildType)"
    Write-Host "  $buildDir\bin\Audioslave.exe          tray + service + CLI"
    Write-Host "  $buildDir\bin\Audioslave-Setup.exe    installer"
    if (-not $NoDist) { Write-Host "  dist\                                   release artefacts + SHA256SUMS.txt" }
} finally {
    Pop-Location
}
