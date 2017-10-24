# 4d-plugin-gs
4D implementation of [Ghostscript](https://www.ghostscript.com)

### Platform

| carbon | cocoa | win32 | win64 |
|:------:|:-----:|:---------:|:---------:|
|<img src="https://cloud.githubusercontent.com/assets/1725068/22371562/1b091f0a-e4db-11e6-8458-8653954a7cce.png" width="24" height="24" />|<img src="https://cloud.githubusercontent.com/assets/1725068/22371562/1b091f0a-e4db-11e6-8458-8653954a7cce.png" width="24" height="24" />|<img src="https://cloud.githubusercontent.com/assets/1725068/22371562/1b091f0a-e4db-11e6-8458-8653954a7cce.png" width="24" height="24" />|<img src="https://cloud.githubusercontent.com/assets/1725068/22371562/1b091f0a-e4db-11e6-8458-8653954a7cce.png" width="24" height="24" />|

### Version

<img src="https://cloud.githubusercontent.com/assets/1725068/18940649/21945000-8645-11e6-86ed-4a0f800e5a73.png" width="32" height="32" /> <img src="https://cloud.githubusercontent.com/assets/1725068/18940648/2192ddba-8645-11e6-864d-6d5692d55717.png" width="32" height="32" />

### Build Information

* Notable build flags on Mac

```
--without-x 
--disable-threading 
--without-libidn
--disable-cups
```

**Note**: On Mac, need to ``make so`` to build a ``dylib``

https://www.ghostscript.com/doc/9.21/Make.htm#Mac_build

**Issue**: When 4D Server.app is quit (not when the structure is closed), the following error is systematically raised in the client manager thread.

```
_pthread_tsd_cleanup
_pthread_exit
pthread_exit
TSExit
YieldToThread
SetThreadState
```

``pthread`` seems to be used for ``CUPS``; let's disable it.

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
