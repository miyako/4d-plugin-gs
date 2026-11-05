![version](https://img.shields.io/badge/version-18%2B-EB8E5F)
![platform](https://img.shields.io/static/v1?label=platform&message=mac-intel%20|%20mac-arm%20|%20win-64&color=blue)
[![license](https://img.shields.io/github/license/miyako/4d-plugin-gs)](LICENSE)
![downloads](https://img.shields.io/github/downloads/miyako/4d-plugin-gs/total)

# 4d-plugin-gs

`GS` is a thin 4D wrapper around [Ghostscript](https://www.ghostscript.com)'s C API (`gsapi_new_instance` / `gsapi_init_with_args` / `gsapi_exit`). It takes the same command-line switches you'd pass to the `gs` executable — as a 4D text array instead of a shell command line — and drives the bundled Ghostscript library (currently `ghostscript-10.05.1`) to process a PostScript or PDF file: converting formats, rasterizing pages, or running any other operation Ghostscript itself supports.

Command | Returns | Purpose
------------|------------|----
[`GS`](#gs) | `LONGINT` | Runs Ghostscript with the given command-line-style options.

**Platforms:** macOS 15 or later (Intel & Apple Silicon), Windows 64-bit.

---

## Requirements & platform notes

- **Ghostscript supports exactly one instance per process.** This is Ghostscript's own restriction, not a 4D limitation — its C API uses a static, process-wide instance counter. `manifest.json` declares `GS` as `"threadSafe": true`, and as of the current source the plugin honors that: an internal mutex serializes overlapping calls so a second call made while a first is still running **waits its turn instead of failing.** The practical effect: `GS` is safe to call from multiple threads at once, but two overlapping calls never actually run in parallel — the second one queues behind the first for as long as Ghostscript takes to finish. Plan for that if you're calling `GS` from several concurrent processes on a busy 4D Server.
- **Always include `-dNOPAUSE`, `-dBATCH`, and `-dQUIET`.** Ghostscript's command-line interpreter is designed for interactive use by default; without `-dNOPAUSE`/`-dBATCH` it can sit waiting for input that 4D has no way to provide, which will look like a hang from your 4D code rather than a clean error. File access is sandboxed by default (see the safer-mode note below).
- **macOS 15 or later.** The bundled `libgs.10.05.dylib` is built with a minimum macOS version of 15.0 (both slices), even though the plugin binary itself targets 11.5. On an older macOS the library, and so the plugin, is expected to fail to load.
- **Start your switches at element 1, not element 0.** `GS` passes the whole array, element 0 included, to Ghostscript. Element 0 lands in the slot of a C program's `argv[0]` (the program name), which Ghostscript ignores. Leave `$args{0}` empty and put your first real switch (e.g. `-dNOPAUSE`) in `$args{1}`, as every example below does. A switch placed in element 0 is silently dropped.
- **Safer mode is on by default.** Ghostscript 9.50 and later run with `-dSAFER` unless you pass `-dNOSAFER`, so passing `-dSAFER` (as `TEST_1`/`TEST_2` do) is harmless but not required. File access outside the input and output paths is refused: if your PostScript/PDF job reads other files (fonts, ICC profiles, included scripts), add a `--permit-file-read=` (or `--permit-file-write=`) entry for each directory those files live in. See the ZUGFeRD example below.
- **Ghostscript's messages go to the 4D process's own stdout/stderr.** `GS` installs no output callbacks, so banner text, warnings and error messages are not returned to 4D. Use the result code, and `-dQUIET` to keep the console quiet.
- **File paths must be POSIX-style**, even on Windows — every sample converts native 4D paths with `Convert path system to POSIX` before adding them to the array.
- **The two platforms ship different Ghostscript builds.** **On macOS**, `libgs.10.05.dylib` is a custom build without threading, Tesseract OCR or CUPS (the library has no CUPS, OCR or `pthread_create` symbols). CUPS was left out because, with it, 4D Server raised a `pthread_exit` error in its client-manager thread when quitting. **On Windows**, `gsdll64.dll` is Artifex's own 10.05.1 build, which does include the OCR devices (`pdfocr8`, `pdfocr24`, `pdfocr32`, `ocr`, `hocr`). A job that uses those devices works on Windows only.
- **Error codes** are Ghostscript's own, defined in `include/gserrors.h` in this repository. `GS` passes the return value of `gsapi_init_with_args` through unchanged, except for its own `-1` (see [GS](#gs)).

---

## GS

### Syntax

```4d
error:=GS(options)
```

Parameter | Type | Description
------------|------------|----
`options` | ARRAY TEXT | Command-line-style Ghostscript switches, one per array element, in the exact order Ghostscript should see them. Mandatory — must contain at least one element (see note below).
Result | LONGINT | `0` on success. Otherwise a Ghostscript code from `gserrors.h` (`-101` means the job ended with `quit`, which is not an error), or `-1` if Ghostscript was not run at all (see Description).

### Description

`GS` copies every element of `options` into an argument list and hands it directly to Ghostscript's `gsapi_init_with_args`, exactly as if you'd run `gs <options>` from a shell — nothing is added, reordered, or defaulted on your behalf. That means every switch Ghostscript's command-line reference documents (device selection with `-sDEVICE=`, output path with `-sOutputFile=`, resolution with `-r`, page ranges, etc.) is available by adding the matching element(s) to `options`, in the order Ghostscript expects them.

Element 0 of `options` is passed too, as Ghostscript's program-name slot, and is ignored (see Requirements above).

Text encoding is handled for you: **on macOS** the array is passed as UTF-8, **on Windows** as UTF-16LE. Non-ASCII file paths and arguments work on both platforms without extra handling.

After the job, `GS` always calls `gsapi_exit` (which finishes writing the output file) and deletes the Ghostscript instance. The result is the code from `gsapi_init_with_args` only: an error reported by `gsapi_exit` itself is not returned.

Result codes you are likely to see:

Value | Meaning
------------|----
`0` | Success.
`-100` | Fatal error (`gs_error_Fatal`), e.g. the input file does not exist or cannot be read.
`-101` | The job ended with `quit` (`gs_error_Quit`), e.g. `-c quit`. Not an error.
`-1` | Ghostscript was not run: `options` had no element after element 0, or is not a text array, or a Ghostscript instance could not be created, or an exception was caught. Ghostscript's own `gs_error_unknownerror` is also `-1`, so the two cannot be told apart.

> Changed after 2.4.6 (current source, not in a release yet): an empty `options` array returns `-1` without running Ghostscript. Earlier builds passed it on, and Ghostscript then ran with no arguments, printed its banner and returned `0`.

### Example

From the plugin's own test method (`TEST_1.4dm`) — convert a PDF page to a PNG:

```4d
  //pdf2png example

$input:=Get 4D folder:C485(Current resources folder:K5:16)+"image - Converting a PDF to PNG - Stack Overflow.pdf"
$output:=System folder:C487(Desktop:K41:16)+"test.png"

C_BLOB:C604($in;$out)
ARRAY TEXT:C222($args;9)

$args{1}:="-dNOPAUSE"  //important, we can't interact with the cli
$args{2}:="-dBATCH"
$args{3}:="-dSAFER"
$args{4}:="-dQUIET"

  //output (1 file per page)
$args{5}:="-sDEVICE=pngalpha"
$args{6}:="-sOutputFile="+Convert path system to POSIX:C1106($output)

$args{7}:="-r144"  //for decent quality

  //input
$args{8}:="-f"
$args{9}:=Convert path system to POSIX:C1106($input)

GS ($args)
```

From the plugin's own test method (`TEST_2.4dm`) — convert PostScript to PDF:

```4d
  //ps2pdf example

$input:=Get 4D folder:C485(Current resources folder:K5:16)+"How to use Ghostscript.ps"
$output:=System folder:C487(Desktop:K41:16)+"test.pdf"

C_BLOB:C604($in;$out)
ARRAY TEXT:C222($args;10)

$args{1}:="-dNOPAUSE"  //important, we can't interact with the cli
$args{2}:="-dBATCH"
$args{3}:="-dSAFER"
$args{4}:="-dQUIET"

  //output (1 file per page)
$args{5}:="-sDEVICE=pdfwrite"
$args{6}:="-sOutputFile="+Convert path system to POSIX:C1106($output)

  //command
$args{7}:="-c"
$args{8}:=".setpdfwrite"

  //input
$args{9}:="-f"
$args{10}:=Convert path system to POSIX:C1106($input)

GS ($args)
```

From the plugin's own test method (`TEST_3.4dm`) — produce a PDF/A-3 (ZUGFeRD) invoice, using `--permit-file-read` because the run needs `-dSAFER` sandboxing plus access to an ICC profile and a PostScript library file outside the input/output paths themselves:

```4d
$f:={resolve: Formula:C1597(OB Class:C1730($1).new($1.platformPath; fk platform path:K87:2))}

$input:=File:C1566("/RESOURCES/invoice.pdf")
$invoice:=File:C1566("/RESOURCES/invoice.xml")
$zugferd:=File:C1566("/RESOURCES/lib/zugferd.ps")
$iccprofile:=File:C1566("/RESOURCES/iccprofiles/default_rgb.icc")

$input:=$f.resolve($input)
$invoice:=$f.resolve($invoice)
$zugferd:=$f.resolve($zugferd)
$iccprofile:=$f.resolve($iccprofile)

$output:=Folder:C1567(fk desktop folder:K87:19).file("invoice.pdf")

C_BLOB:C604($in; $out)
ARRAY TEXT:C222($args; 17)

$args{1}:="-dNOPAUSE"

$args{2}:="--permit-file-read="+$iccprofile.parent.path
$args{3}:="--permit-file-read="+$zugferd.parent.path
$args{4}:="--permit-file-read="+$input.parent.path

$args{5}:="-sDEVICE=pdfwrite"
$args{6}:="-dPDFA=3"
$args{7}:="-sColorConversionStrategy=RGB"
$args{8}:="-sZUGFeRDXMLFile="+$invoice.path
$args{9}:="-sZUGFeRDProfile="+$iccprofile.path
$args{10}:="-sZUGFeRDVersion=2p1"
$args{11}:="-sZUGFeRDConformanceLevel=BASIC"
$args{12}:="-o"
$args{13}:=$output.path

//input (1)
$args{14}:="-f"
$args{15}:=$zugferd.path

//input (2)
$args{16}:="-f"
$args{17}:=$input.path

$error:=GS($args)
```

A minimal generic call, built only from the pattern above:

```4d
ARRAY TEXT($args;6)
$args{1}:="-dNOPAUSE"
$args{2}:="-dBATCH"
$args{3}:="-dQUIET"
$args{4}:="-sDEVICE=pdfwrite"
$args{5}:="-sOutputFile="+Convert path system to POSIX($outputPath)
$args{6}:=Convert path system to POSIX($inputPath)

$error:=GS($args)
If ($error#0)
   ALERT("Ghostscript failed with code: "+String($error))
End if
```

---

## Error handling & troubleshooting

- **Ghostscript appears to "hang."** Almost always a missing `-dNOPAUSE`/`-dBATCH` (or `-o`, which implies both). Without them Ghostscript stops after each page with "press <return> to continue" and reads stdin. Whether that blocks depends on how 4D was started; add both switches, as in every example above.
- **The first switch seems to be ignored.** It is probably in element 0 of the array, which Ghostscript treats as the program name. Start at `$args{1}`.
- **`error` is non-zero.** The value is Ghostscript's own return code from `gsapi_init_with_args`, defined in `include/gserrors.h`; the plugin does not translate it. `-101` (`quit`) is not an error. Ghostscript's own messages go to the 4D process's stdout/stderr, not to 4D.
- **`error` is `-1`.** Either Ghostscript was not run (no switches after element 0, wrong array type, instance creation failed) or Ghostscript reported `gs_error_unknownerror`. Check that you pass a filled `ARRAY TEXT`.
- **A file referenced by your PostScript/PDF script (font, ICC profile, included `.ps` library) fails to read, but the main input/output paths work fine.** Safer mode (the default) needs a `--permit-file-read=` entry for that file's directory. See the ZUGFeRD example's three `--permit-file-read=` entries.
- **`error` is `0` but the output file is incomplete.** Errors raised while Ghostscript finishes writing the output (`gsapi_exit`) are not reported. Check the output file.
- **A call to `GS` takes noticeably longer than expected under concurrent load.** Ghostscript allows only one instance per process, so the current source serializes overlapping calls internally — a second call made while a first is still running waits for it to finish rather than running alongside it or failing. This is expected: `GS` is safe to call concurrently, but not parallel.
- **4D Server raises a `pthread_exit` error in the client-manager thread when quitting (not on structure close).** This was seen with a macOS Ghostscript build that included CUPS; the bundled macOS library is built without CUPS to avoid it.
- **An empty `options` array returns `-1`** in the current source. Older binaries ran Ghostscript with no arguments and returned `0`.
- **OCR devices (`pdfocr8`, `ocr`, `hocr`, …) fail on macOS but work on Windows.** The macOS library is built without Tesseract; the Windows DLL is Artifex's standard build.
- **The plugin does not load on macOS 14 or earlier.** The bundled `libgs.10.05.dylib` requires macOS 15.0.

---

## Quick reference

```4d
ARRAY TEXT($args;N)
$args{1}:="-dNOPAUSE"
$args{2}:="-dBATCH"
$args{3}:="-dQUIET"
$args{4}:="-dSAFER"
$args{5}:="-sDEVICE=<device>"                 //e.g. pdfwrite, pngalpha
$args{6}:="-sOutputFile="+Convert path system to POSIX($outputPath)
$args{7}:="-r144"                             //optional, raster devices only
$args{8}:="-f"
$args{9}:=Convert path system to POSIX($inputPath)

$error:=GS($args)
If ($error#0)
   //non-zero = Ghostscript error code; see gserrors.h
End if
```
