[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$msbuild = & $vswhere -latest -products '*' -version '[17.0,18.0)' `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) { throw 'Visual Studio 2022 C++ Build Tools were not found.' }

& $msbuild (Join-Path $repoRoot 'tests\feature-tests.vcxproj') /nologo /m /verbosity:minimal `
    /p:Configuration=Release /p:Platform=x64
if ($LASTEXITCODE -ne 0) { throw "Test build failed with exit code $LASTEXITCODE." }

$testDir = Join-Path $repoRoot 'x64\Tests'
Copy-Item -LiteralPath 'C:\opencv\build\x64\vc16\bin\opencv_world4120.dll' -Destination $testDir -Force
Push-Location $testDir
try {
    $testProcess = Start-Process -FilePath (Join-Path $testDir 'feature-tests.exe') `
        -WorkingDirectory $testDir -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $testDir 'test-stdout.log') `
        -RedirectStandardError (Join-Path $testDir 'test-stderr.log')
    if (-not $testProcess.WaitForExit(30000)) {
        Stop-Process -Id $testProcess.Id
        throw 'Feature tests exceeded the 30-second runtime limit.'
    }
    Get-Content (Join-Path $testDir 'test-stdout.log')
    Get-Content (Join-Path $testDir 'test-stderr.log')
    if ($testProcess.ExitCode -ne 0) { throw "Feature tests failed with exit code $($testProcess.ExitCode)." }
} finally {
    Pop-Location
}
