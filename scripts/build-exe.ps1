[CmdletBinding()]
param(
    [string]$Msys2Root = 'C:\msys64',
    [string]$BuildDirectory = 'build-windows'
)

$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path -Parent $PSScriptRoot
$ucrtDirectory = Join-Path $Msys2Root 'ucrt64'
$toolDirectory = Join-Path $ucrtDirectory 'bin'

function Invoke-BuildTool {
    param([string]$Tool, [string[]]$Arguments)
    & $Tool @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Tool failed with exit code $LASTEXITCODE."
    }
}

$cmake = Join-Path $toolDirectory 'cmake.exe'
$compiler = Join-Path $toolDirectory 'g++.exe'
$ninja = Join-Path $toolDirectory 'ninja.exe'
foreach ($tool in @($cmake, $compiler, $ninja,
    (Join-Path $toolDirectory 'windres.exe'),
    (Join-Path $toolDirectory 'windeployqt.exe'),
    (Join-Path $toolDirectory 'objdump.exe'))) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
        throw "Missing build tool: $tool. Install the MSYS2 UCRT64 dependencies listed in README.md."
    }
}
if (-not [IO.Path]::IsPathRooted($BuildDirectory)) {
    $BuildDirectory = Join-Path $projectDirectory $BuildDirectory
}
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)

$previousPath = $env:PATH
try {
    $env:PATH = "$toolDirectory;$previousPath"
    Invoke-BuildTool $cmake @('-S', $projectDirectory, '-B', $BuildDirectory,
        '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
        "-DCMAKE_PREFIX_PATH=$ucrtDirectory", "-DCMAKE_CXX_COMPILER=$compiler",
        "-DCMAKE_MAKE_PROGRAM=$ninja", '-DKPLAYER_BUILD_TESTS=OFF',
        '-DKPLAYER_DEPLOY_WINDOWS_RUNTIME=ON', '-DKPLAYER_BUILD_MSI=OFF')
    Invoke-BuildTool $cmake @('--build', $BuildDirectory, '--target', 'KPlayer', '--parallel')
    Write-Host "EXE output: $(Join-Path $BuildDirectory 'KPlayer.exe')"
    Write-Host 'Keep the runtime DLLs, plugins, and QML imports beside the executable when moving or sharing it.'
}
finally {
    $env:PATH = $previousPath
}
