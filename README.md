![version](https://img.shields.io/badge/version-18%2B-EB8E5F)
![platform](https://img.shields.io/static/v1?label=platform&message=mac-intel%20|%20mac-arm%20|%20win-64&color=blue)
[![license](https://img.shields.io/github/license/miyako/4d-plugin-gs)](LICENSE)
![downloads](https://img.shields.io/github/downloads/miyako/4d-plugin-gs/total)

### Dependencies and Licensing

* the source code of this plugin developed using the [4D Plug-in SDK](https://github.com/4d/4D-Plugin-SDK) is licensed under the [MIT license](https://github.com/miyako/4d-plugin-gs/blob/master/LICENSE).
* the licensing of **ghostscript** (shared library) [is exclusively handled by Artifex Software, Inc.](https://www.ghostscript.com/licensing/index.html)
* the licensing of the binary product of this plugin is subject to the licensing of all its dependencies.

`GS` is a thin 4D wrapper around [Ghostscript](https://www.ghostscript.com)'s C API (`gsapi_new_instance` / `gsapi_init_with_args` / `gsapi_exit`). It takes the same command-line switches you'd pass to the `gs` executable — as a 4D text array instead of a shell command line — and drives the bundled Ghostscript library (currently `ghostscript-10.05.1`) to process a PostScript or PDF file: converting formats, rasterizing pages, or running any other operation Ghostscript itself supports.

Command | Returns | Purpose
------------|------------|----
[`GS`](#gs) | `LONGINT` | Runs Ghostscript with the given command-line-style options.

**Platforms:** macOS (Intel & Apple Silicon), Windows 64-bit.

---

## Requirements & platform notes

- **Ghostscript supports exactly one instance per process.** This is Ghostscript's own restriction, not a 4D limitation — its C API uses a static, process-wide instance counter. `manifest.json` currently declares `GS` as `"threadSafe": true`, but if two calls to `GS` genuinely overlap (e.g. two 4D processes/threads calling it at the same moment), the second call's Ghostscript instance creation will fail by design. **Until this is resolved at the plugin level, avoid calling `GS` concurrently from more than one process/thread at a time** — serialize calls if your workflow could otherwise overlap them.
- **Always include `-dNOPAUSE`, `-dBATCH`, and `-dQUIET`.** Ghostscript's command-line interpreter is designed for interactive use by default; without `-dNOPAUSE`/`-dBATCH` it can sit waiting for input that 4D has no way to provide, which will look like a hang from your 4D code rather than a clean error. `-dSAFER` is also strongly recommended — it sandboxes file access (see the `-dSAFER` note below).
- **Do not put a placeholder program name in position 1 of the options array.** Unlike a typical C `main(argc, argv)`, the array passed to `GS` is Ghostscript's *entire* argument list — position 1 should be your first real switch (e.g. `-dNOPAUSE`), exactly as shown in every example below.
- **`-dSAFER` restricts file access to what you explicitly permit.** If your PostScript/PDF input references other files (fonts, ICC profiles, included scripts), you must add a `--permit-file-read=` (or `--permit-file-write=`) entry for each directory those files live in, or Ghostscript will refuse to read them. See the ZUGFeRD example below.
- **File paths must be POSIX-style**, even on Windows — every sample converts native 4D paths with `Convert path system to POSIX` before adding them to the array.
- This build was compiled with `--disable-threading`, `--without-tesseract`, and `--disable-cups`. Devices or operations that depend on Ghostscript's threading, embedded Tesseract OCR, or CUPS printing are **not available** in this binary. `--disable-cups` specifically works around a crash 4D Server otherwise raises in its client-manager thread on quit (see the plugin's README for the exact stack trace).
- Exact non-zero error codes returned by Ghostscript are defined in `gserrors.h` (not included with this plugin's source) — `GS` passes Ghostscript's raw return code straight through, so consult Ghostscript's own error-code documentation for the meaning of a specific non-zero value. `0` always means success.

---

## GS

### Syntax

```4d
error:=GS(options)
```

Parameter | Type | Description
------------|------------|----
`options` | ARRAY TEXT | Command-line-style Ghostscript switches, one per array element, in the exact order Ghostscript should see them. Mandatory — must contain at least one element (see note below).
Result | LONGINT | `0` on success. A negative value is a Ghostscript error code (see `gserrors.h`) or, going forward, `-1` if Ghostscript couldn't even be started for this call (see Description).

### Description

`GS` copies every element of `options` into an argument list and hands it directly to Ghostscript's `gsapi_init_with_args`, exactly as if you'd run `gs <options>` from a shell — nothing is added, reordered, or defaulted on your behalf. That means every switch Ghostscript's command-line reference documents (device selection with `-sDEVICE=`, output path with `-sOutputFile=`, resolution with `-r`, page ranges, etc.) is available by adding the matching element(s) to `options`, in the order Ghostscript expects them.

Text encoding is handled for you: on macOS the array is passed through as UTF-8; on Windows it's passed through as UTF-16LE. Non-ASCII file paths and arguments work on both platforms without extra handling on your part.

> **Forward-looking note:** an empty `options` array and a failure to start a Ghostscript instance (for example, a second overlapping call while one is already running — see Requirements above) currently have undefined/crash-risk behavior in the shipped binary. Once the plugin is rebuilt from the current source, both cases instead return `-1` immediately without attempting to run Ghostscript. This paragraph describes the *fixed* source's behavior, not necessarily the binary you currently have installed — check your build.

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

- **Ghostscript appears to "hang."** Almost always a missing `-dNOPAUSE`/`-dBATCH` — Ghostscript's interpreter defaults to an interactive mode that waits for stdin input 4D can't supply. Add both switches, as in every example above.
- **`error` is non-zero.** The value is Ghostscript's own raw return code from `gsapi_init_with_args` (`0` = success). Look it up against Ghostscript's `gserrors.h`/error-code documentation for the specific meaning; this plugin does not translate or re-map it.
- **A file referenced by your PostScript/PDF script (font, ICC profile, included `.ps` library) fails to read, but the main input/output paths work fine.** You're likely running with `-dSAFER` (recommended) without a matching `--permit-file-read=` entry for that file's directory. See the ZUGFeRD example's three `--permit-file-read=` entries.
- **Calling `GS` again while a previous call is still running fails or behaves unexpectedly.** Ghostscript allows only one instance per process; despite the plugin manifest marking `GS` as thread-safe, concurrent/overlapping calls are not currently safe at the Ghostscript level. Serialize calls to `GS` until this is addressed in the plugin.
- **4D Server raises a `pthread`-related error in the client-manager thread when quitting (not on structure close).** This is a known, documented issue tied to CUPS threading and is unrelated to any specific `GS` call — see the plugin's README for the exact stack trace and the `--disable-cups` build flag used to avoid it.
- **An empty `options` array, or a call made while another `GS` call is already using Ghostscript's single instance.** In the currently fixed source, this returns `-1` without attempting to run Ghostscript; older/unpatched binaries may crash instead — confirm which source your installed binary was built from if you see a crash rather than a `-1` return in this situation.
- **OCR-, threading-, or CUPS-dependent Ghostscript devices/options don't work.** This build was compiled with `--without-tesseract`, `--disable-threading`, and `--disable-cups` — those features are unavailable regardless of the switches you pass.

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
