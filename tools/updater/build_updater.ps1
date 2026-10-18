Param(
    [string]$Output = "Updater.exe",
    [switch]$UseVcVars
)

$ErrorActionPreference = "Stop"

if ($UseVcVars) {
    Write-Host "==> Initializing Visual Studio environment..." -ForegroundColor Cyan
    $vcvars = $null
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($installPath) {
            $vcvars = Join-Path $installPath "VC\Auxiliary\Build\vcvarsall.bat"
        }
    }
    
    if (-not $vcvars -or -not (Test-Path $vcvars)) {
        throw "Could not locate vcvarsall.bat automatically. Ensure VS is installed."
    }
    Write-Host "   Found vcvarsall.bat at $vcvars"
}

Write-Host "==> Building $Output with admin manifest..." -ForegroundColor Cyan

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
Set-Location $scriptDir

Write-Host "==> Working directory: $scriptDir"

Write-Host "==> Ensuring libbzip2 sources are available..."
$repoRoot = Resolve-Path (Join-Path $scriptDir "..\\..")
$bzip2Dir = Join-Path $repoRoot "_tmp\\bzip2"
$bzip2Marker = Join-Path $bzip2Dir "bzlib.c"
if (-not (Test-Path $bzip2Marker)) {
    if (Test-Path $bzip2Dir) {
        Remove-Item -Recurse -Force $bzip2Dir
    }
    Write-Host "   Cloning bzip2 sources to $bzip2Dir..."
    git clone --depth 1 https://gitlab.com/bzip2/bzip2.git $bzip2Dir
    if (-not (Test-Path $bzip2Marker)) {
        throw "bzip2 sources not found after clone: $bzip2Marker"
    }
} else {
    Write-Host "   bzip2 sources found at $bzip2Dir"
}

$bzVersionSrc = Join-Path $scriptDir "bz_version.h"
$bzVersionDest = Join-Path $bzip2Dir "bz_version.h"
if (Test-Path $bzVersionSrc) {
    if (-not (Test-Path $bzVersionDest)) {
        Write-Host "   Copying bz_version.h to $bzip2Dir..."
        Copy-Item $bzVersionSrc $bzVersionDest
    }
} elseif (-not (Test-Path $bzVersionDest)) {
    Write-Host "   Generating bz_version.h in $bzip2Dir..."
    Set-Content -Path $bzVersionDest -Value "#ifndef BZ_VERSION_H`r`n#define BZ_VERSION_H`r`n#define BZ_VERSION `"1.0.8`"`r`n#endif"
}

Write-Host "==> Ensuring rsrc tool is available..."
$goPath = & go env GOPATH
if (-not $goPath) {
    throw "GOPATH is empty, please ensure Go is installed and go env works."
}

$rsrcPath = Join-Path $goPath "bin\rsrc.exe"

if (-not (Test-Path $rsrcPath)) {
    Write-Host "   rsrc.exe not found, installing via 'go install github.com/akavel/rsrc@latest'..."
    go install github.com/akavel/rsrc@latest
    if (-not (Test-Path $rsrcPath)) {
        throw "rsrc.exe still not found at $rsrcPath after install."
    }
} else {
    Write-Host "   Found rsrc.exe at $rsrcPath"
}

Write-Host "==> Generating updater.syso from updater.manifest and updater.ico..."
& $rsrcPath -manifest "updater.manifest" -ico "updater.ico" -o "updater.syso"

Write-Host "==> Running go build..."
if ($UseVcVars) {
    $cmd = 'call "{0}" x64 && set CGO_ENABLED=1 && go build -ldflags "-H windowsgui -s -w -extldflags=`"-static`"" -o "{1}" .' -f $vcvars, $Output
    cmd.exe /c $cmd
    if ($LASTEXITCODE -ne 0) { throw "go build failed with exit code $LASTEXITCODE" }
} else {
    $env:CGO_ENABLED = "1"
    go build -ldflags "-H windowsgui -s -w -extldflags=`"-static`"" -o $Output .
}

Write-Host "==> Build finished: $Output" -ForegroundColor Green


