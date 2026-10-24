#Requires -Version 7
<#
.SYNOPSIS
Makes the distributable folder dist\pdftweak-<version> from the Release build.

.DESCRIPTION
The folder holds pdftweak.exe and pdftweak.com, the DLLs they import (qpdf30.dll, tiff.dll, and the
C++ runtime from the Build Tools redistributable folder), README.md and LICENSE, the licences of
every library the program links under licences, and under sources the pinned source archives of the
libraries with an archive of PDF Tweak's own source at HEAD. The script builds nothing: run deps.ps1
and the Release build of pdftweak.sln first. It then checks that every DLL the program imports is in
the folder or comes with Windows, and that pdftweak.com runs from the folder with a PATH of the
Windows folders only.
#>
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true

$repo = Split-Path $PSScriptRoot
$versionHeader = Get-Content (Join-Path $repo 'src\core\version.h') -Raw
if ($versionHeader -notmatch 'PDFTWEAK_VERSION_STRING "([^"]+)"') { throw 'No version in version.h' }
$version = $Matches[1]
$release = Join-Path $repo 'build\x64\Release'
$pins = Import-PowerShellDataFile (Join-Path $repo 'deps\pins.psd1')
$sources = Join-Path $repo 'deps\src'
$dist = Join-Path $repo "dist\pdftweak-$version"

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if (-not $vs) { throw 'No Visual Studio installation with the C++ tools was found.' }
$build = Join-Path $vs 'VC\Auxiliary\Build'
$toolsVersion = (Get-Content (Join-Path $build 'Microsoft.VCToolsVersion.default.txt')).Trim()
$redistVersion = (Get-Content (Join-Path $build 'Microsoft.VCRedistVersion.default.txt')).Trim()
$dumpbin = Join-Path $vs "VC\Tools\MSVC\$toolsVersion\bin\Hostx64\x64\dumpbin.exe"
$crt = Get-ChildItem (Join-Path $vs "VC\Redist\MSVC\$redistVersion\x64") -Directory `
    -Filter 'Microsoft.VC*.CRT' |
    Select-Object -First 1
if (-not $crt) { throw "No C++ runtime in the redistributable folder $redistVersion" }

$program = 'pdftweak.exe', 'pdftweak.com', 'qpdf30.dll', 'tiff.dll'
foreach ($file in $program) {
    if (-not (Test-Path (Join-Path $release $file))) {
        throw "Missing ${file}: build the Release configuration first"
    }
}

if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
New-Item -ItemType Directory -Force $dist, (Join-Path $dist 'licences'), (Join-Path $dist 'sources') |
    Out-Null

# The program and the DLLs it imports. A DLL of the C++ runtime ships even where the machine that
# packs has it installed, as the machine that runs the program may not.
foreach ($file in $program) { Copy-Item (Join-Path $release $file) $dist }
Copy-Item (Join-Path $repo 'README.md'), (Join-Path $repo 'LICENSE') $dist

function Get-Imports([string]$Path) {
    & $dumpbin /nologo /dependents $Path | Where-Object { $_ -match '^\s+(\S+\.dll)\s*$' } |
        ForEach-Object { $Matches[1] }
}

$checked = @{}
$pending = [System.Collections.Generic.Queue[string]]::new()
foreach ($file in $program) { $pending.Enqueue((Join-Path $dist $file)) }
while ($pending.Count -gt 0) {
    $path = $pending.Dequeue()
    foreach ($import in Get-Imports $path) {
        $key = $import.ToLowerInvariant()
        if ($checked.ContainsKey($key)) { continue }
        $checked[$key] = $true
        $inFolder = Join-Path $dist $import
        $runtime = Join-Path $crt.FullName $import
        if (Test-Path $inFolder) { continue }
        if (Test-Path $runtime) {
            Copy-Item $runtime $dist
            $pending.Enqueue($inFolder)
        } elseif ($key -like 'api-ms-win-*' -or
                  (Test-Path (Join-Path $env:SystemRoot "System32\$import"))) {
            # a part of Windows, the Universal C Runtime included
        } else {
            $importer = Split-Path -Leaf $path
            throw "$import, which $importer imports, is neither in the folder nor part of Windows"
        }
    }
}

# The licences, one folder per library.
$mupdf = Join-Path $sources $pins.MuPDF.Folder
$licences = [ordered]@{
    'mupdf'          = @("$mupdf\COPYING")
    'brotli'         = @("$mupdf\thirdparty\brotli\LICENSE")
    'cmark-gfm'      = @("$mupdf\thirdparty\cmark-gfm\COPYING")
    'extract'        = @("$mupdf\thirdparty\extract\COPYING")
    'freetype'       = @("$mupdf\thirdparty\freetype\LICENSE.TXT",
                         "$mupdf\thirdparty\freetype\docs\FTL.TXT")
    'gumbo-parser'   = @("$mupdf\thirdparty\gumbo-parser\COPYING")
    'harfbuzz'       = @("$mupdf\thirdparty\harfbuzz\COPYING")
    'jbig2dec'       = @("$mupdf\thirdparty\jbig2dec\COPYING", "$mupdf\thirdparty\jbig2dec\LICENSE")
    'lcms2'          = @("$mupdf\thirdparty\lcms2\LICENSE")
    'libjpeg'        = @("$mupdf\thirdparty\libjpeg\README")
    'leptonica'      = @("$mupdf\thirdparty\leptonica\leptonica-license.txt")
    'mujs'           = @("$mupdf\thirdparty\mujs\COPYING")
    'openjpeg'       = @("$mupdf\thirdparty\openjpeg\LICENSE")
    'tesseract'      = @("$mupdf\thirdparty\tesseract\LICENSE")
    'zint'           = @("$mupdf\thirdparty\zint\LICENSE")
    'zxing-cpp'      = @("$mupdf\thirdparty\zxing-cpp\LICENSE")
    'qpdf'           = @("$sources\$($pins.Qpdf.Folder)\LICENSE.txt",
                         "$sources\$($pins.Qpdf.Folder)\NOTICE.md")
    'libtiff'        = @("$sources\$($pins.LibTiff.Folder)\LICENSE.md")
    'zlib'           = @("$sources\$($pins.Zlib.Folder)\LICENSE")
    'libjpeg-turbo'  = @("$sources\$($pins.LibJpegTurbo.Folder)\LICENSE.md",
                         "$sources\$($pins.LibJpegTurbo.Folder)\README.ijg")
}
foreach ($name in $licences.Keys) {
    $folder = Join-Path $dist "licences\$name"
    New-Item -ItemType Directory -Force $folder | Out-Null
    foreach ($file in $licences[$name]) {
        if (-not (Test-Path $file)) { throw "Missing the licence file $file" }
        Copy-Item $file $folder
    }
}

# The sources: the archives deps.ps1 verified, checked again, and PDF Tweak at HEAD.
foreach ($pin in $pins.Values) {
    $archive = Join-Path $sources (Split-Path -Leaf ([Uri]$pin.Url).AbsolutePath)
    if (-not (Test-Path $archive)) { throw "Missing the source archive ${archive}: run deps.ps1" }
    if ((Get-FileHash -Algorithm SHA256 $archive).Hash -ne $pin.Sha256.ToUpperInvariant()) {
        throw "$archive does not match its SHA-256 in pins.psd1"
    }
    Copy-Item $archive (Join-Path $dist 'sources')
}
# The tar of the commit's tree, with LF line endings, the modes 0644 and 0755, and the commit time on
# every entry, is copied into a ustar archive without owner names, as git names the owner root. The
# same commit gives the same bytes.
$commitTime = [DateTimeOffset]::FromUnixTimeSeconds([long](git -C $repo log -1 --format=%ct HEAD))
$plainTar = Join-Path $dist 'sources\pdftweak-src.tar'
git -C $repo -c tar.umask=022 -c core.autocrlf=false archive --format=tar --prefix="pdftweak-$version/" `
    --mtime="@$($commitTime.ToUnixTimeSeconds())" -o $plainTar 'HEAD^{tree}'
$reader = [System.Formats.Tar.TarReader]::new([IO.File]::OpenRead($plainTar))
$sourceArchive = Join-Path $dist "sources\pdftweak-$version-src.tar.gz"
$gzip = [IO.Compression.GZipStream]::new([IO.File]::Create($sourceArchive),
    [IO.Compression.CompressionLevel]::SmallestSize)
$writer = [System.Formats.Tar.TarWriter]::new($gzip, [System.Formats.Tar.TarEntryFormat]::Ustar, $false)
try {
    while ($null -ne ($entry = $reader.GetNextEntry())) {
        $copy = [System.Formats.Tar.UstarTarEntry]::new($entry.EntryType, $entry.Name)
        $copy.Mode = $entry.Mode
        $copy.ModificationTime = $commitTime
        if ($null -ne $entry.DataStream) { $copy.DataStream = $entry.DataStream }
        $writer.WriteEntry($copy)
    }
} finally {
    $writer.Dispose()
    $gzip.Dispose()
    $reader.Dispose()
}
Remove-Item $plainTar

# The folder runs on its own: pdftweak.com finds pdftweak.exe and every DLL beside it.
$savedPath = $env:PATH
try {
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    $output = & (Join-Path $dist 'pdftweak.com') -version
} finally {
    $env:PATH = $savedPath
}
if ($output -ne "PDF Tweak $version") { throw "pdftweak.com -version printed: $output" }
Write-Host "Packaged $dist"
