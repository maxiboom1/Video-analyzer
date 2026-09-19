[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$Rebuild
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$openCvBuild = 'C:\opencv\build'
$openCvSuffix = if ($Configuration -eq 'Debug') { 'd' } else { '' }
$openCvWorld = "opencv_world4120$openCvSuffix"

# These paths match the Visual Studio project.
foreach ($relativePath in @(
    'include\opencv2\core\version.hpp',
    "x64\vc16\lib\$openCvWorld.lib",
    "x64\vc16\bin\$openCvWorld.dll",
    'x64\vc16\bin\opencv_videoio_ffmpeg4120_64.dll'
)) {
    if (-not (Test-Path -LiteralPath (Join-Path $openCvBuild $relativePath))) {
        throw "Missing OpenCV 4.12.0 file: $openCvBuild\$relativePath. See docs\LOCAL_BUILD.md."
    }
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Visual Studio 2022 C++ Build Tools are missing. See docs\LOCAL_BUILD.md.'
}
$msbuild = & $vswhere -latest -products '*' -version '[17.0,18.0)' `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) {
    throw 'No Visual Studio 2022 installation with C++ Build Tools was found. See docs\LOCAL_BUILD.md.'
}

$target = if ($Rebuild) { 'Rebuild' } else { 'Build' }
& $msbuild (Join-Path $repoRoot 'video-analyzer.sln') /nologo /m /verbosity:minimal `
    "/t:$target" "/p:Configuration=$Configuration" /p:Platform=x64
if ($LASTEXITCODE -ne 0) {
    throw "MSBuild failed with exit code $LASTEXITCODE."
}

# Keep the executable runnable without changing the machine-wide PATH.
$outputDir = Join-Path $repoRoot "x64\$Configuration"
foreach ($dllName in @("$openCvWorld.dll", 'opencv_videoio_ffmpeg4120_64.dll')) {
    Copy-Item -LiteralPath (Join-Path $openCvBuild "x64\vc16\bin\$dllName") -Destination $outputDir -Force
}
Write-Host "Built: $(Join-Path $outputDir 'video-analyzer.exe')"
