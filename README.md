![version](https://img.shields.io/badge/version-18%2B-EB8E5F)
![platform](https://img.shields.io/static/v1?label=platform&message=mac-intel%20|%20mac-arm%20|%20win-64&color=blue)
[![license](https://img.shields.io/github/license/miyako/4d-plugin-gs)](LICENSE)
![downloads](https://img.shields.io/github/downloads/miyako/4d-plugin-gs/total)

### Dependencies and Licensing

* the source code of this plugin developed using the [4D Plug-in SDK](https://github.com/4d/4D-Plugin-SDK) is licensed under the [MIT license](https://github.com/miyako/4d-plugin-gs/blob/master/LICENSE).
* the licensing of **ghostscript** (shared library) [is exclusively handled by Artifex Software, Inc.](https://www.ghostscript.com/licensing/index.html)
* the licensing of the binary product of this plugin is subject to the licensing of all its dependencies.

## Update (June 2025)

- [x] `ghostscript-10.05.1`

# 4d-plugin-gs
4D implementation of [Ghostscript](https://www.ghostscript.com)

### Build Information

> [!WARNING]
> homebrew bottles will crash; need to build from source

### `make so`

> [!NOTE]
> option to build `.dylib`

### `LDFLAGS="-lexpat"`

> [!NOTE]
> `fontconfig` and `gs` links with `expat`
 
### ` --disable-threading --without-tesseract `

> [!NOTE]
> Tesseract OCR relies on threading

### `--disable-cups`

> [!WARNING]
> When 4D Server.app is quit (not when the structure is closed), the following error is systematically raised in the client manager thread
> `pthread` seems to be used for `cups`

```
_pthread_tsd_cleanup
_pthread_exit
pthread_exit
TSExit
YieldToThread
SetThreadState
```

---

## Syntax

```
error:=GS(options)
```

Parameter|Type|Description
------------|------------|----
options|ARRAY TEXT|
error|LONGINT|

## Examples

```
  //ps2pdf example

$input:=Get 4D folder(Current resources folder)+"How to use Ghostscript.ps"
$output:=System folder(Desktop)+"How to use Ghostscript%2d.pdf"

C_BLOB($in;$out)
ARRAY TEXT($args;10)

$args{1}:="-dNOPAUSE"  //important, we can't interact with the cli
$args{2}:="-dBATCH"
$args{3}:="-dSAFER"
$args{4}:="-dQUIET"

  //output (1 file per page)
$args{5}:="-sDEVICE=pdfwrite"
$args{6}:="-sOutputFile="+Convert path system to POSIX($output)

  //command
$args{7}:="-c"
$args{8}:=".setpdfwrite"

  //input
$args{9}:="-f"
$args{10}:=Convert path system to POSIX($input)

GS ($args)
```

```
  //pdf2png example

$input:=Get 4D folder(Current resources folder)+"image - Converting a PDF to PNG - Stack Overflow.pdf"
$output:=System folder(Desktop)+"image - Converting a PDF to PNG - Stack Overflow%2d.png"

C_BLOB($in;$out)
ARRAY TEXT($args;9)

$args{1}:="-dNOPAUSE"  //important, we can't interact with the cli
$args{2}:="-dBATCH"
$args{3}:="-dSAFER"
$args{4}:="-dQUIET"

  //output (1 file per page)
$args{5}:="-sDEVICE=pngalpha"
$args{6}:="-sOutputFile="+Convert path system to POSIX($output)

$args{7}:="-r144"  //for decent quality

  //input
$args{8}:="-f"
$args{9}:=Convert path system to POSIX($input)

GS ($args)
```
