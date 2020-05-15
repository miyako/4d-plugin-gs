//%attributes = {}
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