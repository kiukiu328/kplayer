[CmdletBinding()]
param(
    [string]$Msys2Root = 'C:\msys64',
    [string]$BuildDirectory = 'build-windows-msi',
    [string]$WixRoot
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
$cpack = Join-Path $toolDirectory 'cpack.exe'
$compiler = Join-Path $toolDirectory 'g++.exe'
$ninja = Join-Path $toolDirectory 'ninja.exe'
foreach ($tool in @($cmake, $cpack, $compiler, $ninja)) {
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
    if ($WixRoot) {
        $WixRoot = (Resolve-Path -LiteralPath $WixRoot).Path
        $env:PATH = "$WixRoot;$env:PATH"
    }
    $wix = Get-Command wix.exe -CommandType Application -ErrorAction SilentlyContinue
    if (-not $wix) {
        throw 'WiX 4 is required. Install it using the Windows MSI instructions in README.md, or pass -WixRoot.'
    }
    $wixVersion = & $wix.Source --version
    if ($LASTEXITCODE -ne 0 -or "$wixVersion" -notmatch '^4\.') {
        throw 'MSI packaging requires a working WiX 4 installation.'
    }

    Invoke-BuildTool $cmake @('-S', $projectDirectory, '-B', $BuildDirectory,
        '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
        "-DCMAKE_PREFIX_PATH=$ucrtDirectory", "-DCMAKE_CXX_COMPILER=$compiler",
        "-DCMAKE_MAKE_PROGRAM=$ninja", '-DKPLAYER_BUILD_TESTS=OFF',
        '-DKPLAYER_DEPLOY_WINDOWS_RUNTIME=ON', '-DKPLAYER_BUILD_MSI=ON',
        "-DCPACK_WIX_ROOT:PATH=$([IO.Path]::GetDirectoryName($wix.Source))")
    Invoke-BuildTool $cmake @('--build', $BuildDirectory, '--target', 'KPlayer', '--parallel')
    Invoke-BuildTool $cpack @('--config', (Join-Path $BuildDirectory 'CPackConfig.cmake'),
        '-C', 'Release', '-G', 'WIX')
    Write-Host "MSI output: $(Join-Path $BuildDirectory 'msi')"
}
finally {
    $env:PATH = $previousPath
}
