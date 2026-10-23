# PDF Tweak

PDF Tweak crops, rotates, scales, shuffles (n-up and booklets), watermarks, numbers, and bookmarks the
pages of PDF files, edits page labels, attachments, viewer preferences, and document information,
encrypts and signs documents, bursts them into single pages, and exports pages as images. It is a C++
port of jPDF Tweak 1.2.0 with a native Windows interface, and it keeps jPDF Tweak's command-line
syntax and file formats.

This version is under development. The command line accepts only `-version` until milestone 2.

## Building

Building needs Visual Studio 2026 or its Build Tools with the C++ desktop tools and the CMake
component, PowerShell 7, and optionally NASM for faster JPEG code.

```
pwsh deps\deps.ps1
msbuild pdftweak.sln -p:Configuration=Release -p:Platform=x64
build\x64\Release\pdftweak-tests.exe unit main
```

`deps\deps.ps1` downloads the sources of MuPDF, qpdf, libtiff, zlib, and libjpeg-turbo, checks each
against the SHA-256 in `deps\pins.psd1`, and builds them. It takes about an hour for Debug and Release
together.

## Licence

PDF Tweak is free software under the GNU Affero General Public License, version 3 or later. See
LICENSE.

## ChangeLog

### Unreleased

- Build the native libraries from pinned sources and add `pdftweak.exe` with the `pdftweak.com`
  console relay
