//%attributes = {}
/*

based on example found at

https://ghostscript.readthedocs.io/en/gs10.05.1/ZUGFeRD.html#what-is-zugferd

*/

/*

utility function to resolve file system paths e.g. /RESOURCES/

*/

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
