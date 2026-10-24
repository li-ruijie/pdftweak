# PDF Tweak

PDF Tweak crops, rotates, scales, shuffles (n-up and booklets), watermarks, numbers, and bookmarks the
pages of PDF files, edits page labels, attachments, viewer preferences, and document information,
encrypts and signs documents, bursts them into single pages, and exports pages as images. It is a C++
port of jPDF Tweak 1.3.2 with a native Windows interface, and it keeps jPDF Tweak's command-line
syntax and file formats.

This version is under development. The command line and the window are ported, and the program
ships as a folder that runs without an installer.

## Installing

The folder `pdftweak-2.0.0` runs on 64-bit Windows 10 version 1703 or later. Copy it anywhere and
run `pdftweak.exe` from it, or add it to PATH to run `pdftweak` in a console. It holds the program,
the DLLs it needs with the C++ runtime, the licences of the libraries it links under `licences`, and
the sources of those libraries and of PDF Tweak under `sources`. Deleting the folder removes the
program.

## Using the window

`pdftweak.exe` without arguments opens the window, as `jpdftweak.jar` did. Its eleven tabs follow
jPDF Tweak's order: Input, Page Size, Watermark, Shuffle/N-up, Page Numbers, Bookmarks, Attachments,
Interaction, Document Info, Encrypt/Sign, and Output. Choose the input on the Input tab, set the
changes on the tabs in between, name the output on the Output tab, and press Run.

A run works in the background. While it runs, a panel in place of the Run and Quit buttons names the
file, the tab, and the stage, with a bar that the taskbar button repeats. Cancel stops the run at
the next page or stage, deletes the output being written, and keeps the outputs of files a batch
finished before. Closing the window during a run cancels it.

With arguments, `pdftweak.exe` runs the command line. In a console, `pdftweak.com` relays its
output, and `pdftweak -help` lists the options.

## Building

Building needs Visual Studio 2026 or its Build Tools with the C++ desktop tools and the CMake
component, PowerShell 7, and optionally NASM for faster JPEG code.

```
pwsh deps\deps.ps1
msbuild pdftweak.sln -p:Configuration=Release -p:Platform=x64
build\x64\Release\pdftweak-tests.exe unit main gui
pwsh tools\package.ps1
```

`deps\deps.ps1` downloads the sources of MuPDF, qpdf, libtiff, zlib, and libjpeg-turbo, checks each
against the SHA-256 in `deps\pins.psd1`, and builds them. Debug and Release together take 30 to 40
minutes, mostly for MuPDF. `tools\package.ps1` makes the folder `dist\pdftweak-2.0.0` from the
Release build and checks that it runs with a PATH of the Windows folders only.

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
- Port cropping, rotation, scaling, and shuffling. As in jPDF Tweak 1.3.0, these keep bookmarks,
  named destinations, attachments, viewer settings, the open action, the language, optional content,
  and (except shuffling) page labels, with destinations moved to the first page that shows their
  target, and links with null coordinates no longer fail
- Port the background, text watermark, page numbers, page labels, bookmarks, attachments, page
  transitions, and viewer preferences
- Port the export of pages as JPG, PNG, GIF, PAM, PNM, BMP, and TIFF images, and the multipage TIFF.
  GIF and BMP files come from the Windows Imaging Component, whose GIF palette for a colour page can
  differ from the one Java's ImageIO chose
- Port signing. Keys come from PKCS#12 files or from the Windows certificate store
  (`cert:CurrentUser\My`, with the alias as friendly name or thumbprint), where Java read Java
  keystores, and a Java keystore is refused with the keytool command that converts it. The visible
  signature shows MuPDF's appearance, and the CMS signature carries a signing time
- Sign an encrypted output in the same run, where Java refused to sign and encrypt together
- Port the window with its eleven tabs, the six tables, and the shuffle preview. A run works on a
  separate thread and shows its file, tab, stage, and progress, also on the taskbar button, with a
  Cancel button, where Java's window stopped responding until the run ended
- Package the program as a folder with the C++ runtime, the licences of every linked library, and
  the sources of the libraries and of PDF Tweak
