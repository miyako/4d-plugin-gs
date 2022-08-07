![version](https://img.shields.io/badge/version-18%2B-EB8E5F)
![platform](https://img.shields.io/static/v1?label=platform&message=mac-intel%20|%20mac-arm%20|%20win-64&color=blue)
[![license](https://img.shields.io/github/license/miyako/4d-plugin-gs)](LICENSE)
![downloads](https://img.shields.io/github/downloads/miyako/4d-plugin-gs/total)

### Remarks on [9.21](https://github.com/ArtifexSoftware/ghostpdl-downloads/releases/tag/gs921) compilation for ARM

* include `stdlib.h` in `r_strip.c` for `abs()`
* make `pl_print_usage` conditional in `pctop.c`

```c
/* output the page */
if (gs_debug_c(':')) {
#ifdef DEBUG
    pl_print_usage(pcli->post_page_closure, "parse done :");
#endif
}
```

* ~~change `JERR_HUFF_CLEN_OVERFLOW` to `JERR_HUFF_CLEN_OUTOFBOUNDS` in `jchuff.c`.~~ depends
* surpress `PNG_ZLIB_VERNUM != ZLIB_VERNUM` in `pngpriv.h`
* include `ftadvanc.h` in `afshaper.c` 
* disable libpng ARM optimisation 
_
```
export CFLAGS="-mmacosx-version-min=11.0"
export CXXFLAGS="-mmacosx-version-min=11.0"
export CPPFLAGS="-mmacosx-version-min=11.0 -DPNG_ARM_NEON_OPT=0"
export LDFLAGS="-mmacosx-version-min=11.0"
```

# 4d-plugin-gs
4D implementation of [Ghostscript](https://www.ghostscript.com)

### Build Information

* Notable build flags on Mac

```
--without-x 
--disable-threading 
--without-libidn
--disable-cups
```

added

```
--without-tesseract
```

**Note**: On Mac, need to ``make so`` to build a ``dylib``

https://www.ghostscript.com/doc/9.21/Make.htm#Mac_build

using brew bottle crashed instantly

```
0   libsystem_pthread.dylib       	0x00007fff6ef855ad pthread_mutex_lock + 0
1   libgs.9.53.dylib              	0x000000012994ec41 gp_monitor_enter + 9
2   libgs.9.53.dylib              	0x0000000129b3dbba gs_lib_ctx_init + 203
3   libgs.9.53.dylib              	0x0000000129b39d33 gs_malloc_init_with_context + 34
4   libgs.9.53.dylib              	0x0000000129c3b6b2 psapi_new_instance + 63
5   com.4D.GS                     	0x000000012983733f GS(PluginBlock*) + 1190
6   com.4D.GS                     	0x0000000129836e89 PluginMain + 17
7   com.4D.GS                     	0x0000000129837670 FourDPackex + 60
```

**Issue (fixed)**: When 4D Server.app is quit (not when the structure is closed), the following error is systematically raised in the client manager thread.

```
_pthread_tsd_cleanup
_pthread_exit
pthread_exit
TSExit
YieldToThread
SetThreadState
```

``pthread`` seems to be used for ``CUPS``; let's __disable it__.

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
