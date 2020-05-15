//%attributes = {}
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