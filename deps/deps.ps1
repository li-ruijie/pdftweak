#Requires -Version 7
<#
.SYNOPSIS
Downloads, verifies, and builds the native libraries of PDF Tweak.

.DESCRIPTION
Each source archive listed in pins.psd1 is downloaded into deps\src, checked against its SHA-256, and
unpacked there. zlib and libjpeg-turbo build as static libraries that link privately into tiff.dll
and qpdf30.dll. MuPDF builds as static libraries from its own Visual Studio solution. The results
go to deps\out\<Configuration>\{include,lib,bin}, which deps.props points the solution at.

.PARAMETER Configuration
The configurations to build, Debug, Release, or both (the default).
#>
param(
    [ValidateSet('Debug', 'Release')]
    [string[]]$Configuration = @('Debug', 'Release')
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true
$ProgressPreference = 'SilentlyContinue'

$pins = Import-PowerShellDataFile (Join-Path $PSScriptRoot 'pins.psd1')
$src = Join-Path $PSScriptRoot 'src'
$bld = Join-Path $PSScriptRoot 'build'
$out = Join-Path $PSScriptRoot 'out'
New-Item -ItemType Directory -Force $src, $bld, $out | Out-Null

# Tools. The cmake on PATH is the MinGW build from Rtools, so the copy in Build Tools is called by
# its full path.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if (-not $vs) { throw 'No Visual Studio installation with the C++ tools was found.' }
$msbuild = Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe'
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
foreach ($tool in $msbuild, $cmake) {
    if (-not (Test-Path $tool)) { throw "Missing $tool" }
}
$nasm = (Get-Command nasm.exe -ErrorAction SilentlyContinue)?.Source
$tar = Join-Path $env:SystemRoot 'System32\tar.exe'

# CMake reads some cache values back as CMake code, where a backslash starts an escape sequence.
function ConvertTo-CMakePath([string]$Path) { $Path.Replace('\', '/') }

function Get-Source([hashtable]$Pin) {
    $archive = Join-Path $src (Split-Path $Pin.Url -Leaf)
    if (-not (Test-Path $archive)) {
        Write-Host "Downloading $($Pin.Url)"
        Invoke-WebRequest -Uri $Pin.Url -OutFile $archive
    }
    $hash = (Get-FileHash -Algorithm SHA256 $archive).Hash
    if ($hash -ne $Pin.Sha256) {
        Remove-Item $archive
        throw "SHA-256 mismatch for $archive. Expected $($Pin.Sha256), got $hash."
    }
    $folder = Join-Path $src $Pin.Folder
    if (-not (Test-Path $folder)) { & $tar -xzf $archive -C $src | Out-Host }
    $folder
}

function Invoke-CMake([string]$Name, [string]$Source, [string]$Config, [string[]]$Options,
                      [string]$Target = '') {
    $tree = Join-Path $bld "$Name-$Config"
    & $cmake -S $Source -B $tree -G 'Visual Studio 18 2026' -A x64 @Options | Out-Host
    $targetArgs = if ($Target) { @('--target', $Target) } else { @() }
    & $cmake --build $tree --config $Config @targetArgs -- /m /nologo /v:minimal | Out-Host
    $tree
}

$sources = @{}
foreach ($name in $pins.Keys) { $sources[$name] = Get-Source $pins[$name] }

# A PATH without MinGW, Cygwin, or Perl, so that CMake finds no foreign pkg-config or compiler.
$savedPath = $env:PATH
$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
try {
    foreach ($c in $Configuration) {
        Write-Host "=== $c ==="
        $stage = Join-Path $bld "stage-$c"
        $dest = Join-Path $out $c
        foreach ($d in 'include', 'lib', 'bin') { New-Item -ItemType Directory -Force (Join-Path $dest $d) | Out-Null }

        $tree = Invoke-CMake zlib $sources.Zlib $c @(
            '-DZLIB_BUILD_SHARED=OFF', '-DZLIB_BUILD_STATIC=ON', '-DZLIB_BUILD_TESTING=OFF')
        & $cmake --install $tree --config $c --prefix $stage | Out-Host

        $simd = if ($nasm) {
            @('-DWITH_SIMD=ON', '-DREQUIRE_SIMD=ON', "-DCMAKE_ASM_NASM_COMPILER=$(ConvertTo-CMakePath $nasm)")
        } else {
            Write-Warning 'NASM not found, so libjpeg-turbo builds without SIMD.'
            @('-DWITH_SIMD=OFF')
        }
        $tree = Invoke-CMake jpeg $sources.LibJpegTurbo $c (@(
            '-DENABLE_SHARED=OFF', '-DENABLE_STATIC=ON', '-DWITH_TURBOJPEG=OFF', '-DWITH_TOOLS=OFF',
            '-DWITH_TESTS=OFF', '-DWITH_CRT_DLL=ON') + $simd)
        & $cmake --install $tree --config $c --prefix $stage | Out-Host

        $suffix = if ($c -eq 'Debug') { 'd' } else { '' }
        $zlibLib = ConvertTo-CMakePath (Join-Path $stage "lib\zs$suffix.lib")
        $jpegLib = ConvertTo-CMakePath (Join-Path $stage 'lib\jpeg-static.lib')
        $stageInclude = ConvertTo-CMakePath (Join-Path $stage 'include')
        foreach ($f in $zlibLib, $jpegLib) { if (-not (Test-Path $f)) { throw "Missing $f" } }

        $tree = Invoke-CMake tiff $sources.LibTiff $c @(
            '-DBUILD_SHARED_LIBS=ON', '-Dtiff-tools=OFF', '-Dtiff-tests=OFF', '-Dtiff-contrib=OFF',
            '-Dtiff-docs=OFF', '-Dtiff-cxx=OFF', '-Dzlib=ON', '-Djpeg=ON', '-Dold-jpeg=OFF',
            '-Djbig=OFF', '-Dlerc=OFF', '-Dlzma=OFF', '-Dzstd=OFF', '-Dwebp=OFF', '-Dlibdeflate=OFF',
            '-Dpixarlog=OFF', '-DCMAKE_DISABLE_FIND_PACKAGE_libjpeg-turbo=ON',
            "-DZLIB_INCLUDE_DIR=$stageInclude", "-DZLIB_LIBRARY=$zlibLib",
            "-DJPEG_INCLUDE_DIR=$stageInclude", "-DJPEG_LIBRARY=$jpegLib")
        & $cmake --install $tree --config $c --prefix $dest | Out-Host

        $tree = Invoke-CMake qpdf $sources.Qpdf $c @(
            '-DBUILD_SHARED_LIBS=ON', '-DBUILD_STATIC_LIBS=OFF', '-DUSE_IMPLICIT_CRYPTO=OFF',
            '-DREQUIRE_CRYPTO_NATIVE=ON', '-DBUILD_DOC=OFF',
            "-DZLIB_H_PATH=$stageInclude", "-DZLIB_LIB_PATH=$zlibLib",
            "-DLIBJPEG_H_PATH=$stageInclude", "-DLIBJPEG_LIB_PATH=$jpegLib") libqpdf
        # Only the library target is built, so the files are copied instead of installed.
        Copy-Item -Recurse -Force (Join-Path $sources.Qpdf 'include\qpdf') (Join-Path $dest 'include')
        $qpdfBin = Join-Path $tree "libqpdf\$c"
        Copy-Item (Join-Path $qpdfBin 'qpdf.lib') (Join-Path $dest 'lib')
        Copy-Item (Join-Path $qpdfBin 'qpdf30.*') (Join-Path $dest 'bin') -Exclude '*.exp', '*.lib'

        # Building through the solution builds bin2coff, which libresources needs, as Win32.
        & $msbuild (Join-Path $sources.MuPDF 'platform\win32\mupdf.sln') /t:libmupdf `
            /p:Configuration=$c /p:Platform=x64 /p:PlatformToolset=v145 /m /nologo /v:minimal | Out-Host
        Copy-Item -Recurse -Force (Join-Path $sources.MuPDF 'include\mupdf') (Join-Path $dest 'include')
        Copy-Item (Join-Path $sources.MuPDF "platform\win32\x64\$c\*.lib") (Join-Path $dest 'lib')
        # The solution builds libresources, which holds only data, as Release in every configuration.
        Copy-Item (Join-Path $sources.MuPDF 'platform\win32\x64\Release\libresources.lib') (Join-Path $dest 'lib')
    }
} finally {
    $env:PATH = $savedPath
}
Write-Host "Libraries are in $out"
