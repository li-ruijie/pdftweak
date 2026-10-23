# PDF Tweak

PDF Tweak crops, rotates, scales, shuffles (n-up and booklets), watermarks, numbers, and bookmarks the
pages of PDF files, edits page labels, attachments, viewer preferences, and document information,
encrypts and signs documents, bursts them into single pages, and exports pages as images. It is a C++
port of jPDF Tweak 1.2.2 with a native Windows interface, and it keeps jPDF Tweak's command-line
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
against the SHA-256 in `deps\pins.psd1`, and builds them. Debug and Release together take 30 to 40
minutes, mostly for MuPDF.

## Licence

PDF Tweak is free software under the GNU Affero General Public License, version 3 or later. See
LICENSE.

## ChangeLog

### Unreleased

- Build the native libraries from pinned sources and add `pdftweak.exe` with the `pdftweak.com`
  console relay
- Port the command line with input files, page ranges, aliases, interleaving, output flags, burst,
  `-info`, `-moreinfo`, `-savebookmarks`, `-docinfo`, and encryption
- Exit with code 1 on failure, printing "Error: " and the message where Java printed a stack trace,
  and with 0 on success, where Java always returned 0. A run that only queries a file with `-info`,
  `-moreinfo`, or `-savebookmarks` exits 0 without an output file
- Mark the Producer read by `-info` as "modified by PDF Tweak 2.0.0 (based on qpdf and MuPDF)", and
  give documents without a Producer that name, where Java wrote iText's name and a "modified using
  iText" note
- Keep the form fields of every input file when merging, where Java kept those of the first file
- Keep every catalog entry and annotation when optimising for size, where Java rebuilt the document
- Grant ScreenReaders under revision 4 encryption and treat HQPrinting without Printing as no
  printing, as ISO 32000-2 requires, where Java wrote the bits as given
