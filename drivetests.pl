#!/usr/athena/bin/perl
use File::Spec;

# in this script, we expect @ARGV[0] to contain the absolute path 
# of a pin tool to be run
$pintool = @ARGV[0];

chomp($pintool);
$localdir = File::Spec->rel2abs( "./" ) ;

#create directory for test produced files
$tempdir = $localdir . "/_temp/";
mkdir($tempdir, 0755);

#create uniquified result directory here
$dirname = localtime();
chomp($dirname);
$dirname =~ s/ /_/g;
$resultdir = $localdir . "/results_" . $dirname . "/";
mkdir($resultdir, 0755);



# Test names basically, an array of arrays. 
# First arg is test name, 
# Second is the directory where the test needs to be executed, mostly ./ works
# Third is the test executable.  
# the remainder are the test args, as would appear in the c argv array
@testcases = (  
        [
        "BZIP 1",
        "./", 
        "/mit/6.823/Spring16/CINT2000/256.bzip2/exe/bzip2_base.gcc4-high-opt",
        "/mit/6.823/Spring16/CINT2000/256.bzip2/data/test/input/input.random", 
        "2"
        ],
        [
        "CRAFTY 1",
        "./_temp",
        "/mit/6.823/Spring16/CINT2000/186.crafty/exe/crafty_base.gcc4-high-opt",
        "<",
        "/mit/6.823/Spring16/CINT2000/186.crafty/data/test/input/crafty.in"
        ],
        [
        "GAP 1",
        "./",
        "/mit/6.823/Spring16/CINT2000/254.gap/exe/gap_base.gcc4-high-opt",  
        "-l",
        "/mit/6.823/Spring16/CINT2000/254.gap/data/all/input",
        "-q",
        "-m",
        "64M",
        "<",
        "/mit/6.823/Spring16/CINT2000/254.gap/data/test/input/test.in"
        ],
        [
        "GCC 1",
        "./",
        "/mit/6.823/Spring16/CINT2000/176.gcc/exe/cc1_base.gcc4-high-opt",
        "/mit/6.823/Spring16/CINT2000/176.gcc/data/test/input/cccp.i",
        "-o",
        "${tempdir}/foo.o"
        ],
        [ 
        "GZIP 1",
        "./",
        "/mit/6.823/Spring16/CINT2000/164.gzip/exe/gzip_base.gcc4-high-opt",
        "/mit/6.823/Spring16/CINT2000/164.gzip/data/test/input/input.compressed", 
        "2"
        ],
        [
        "PARSER 1",
        "/mit/6.823/Spring16/CINT2000/197.parser/data/all/input/",
        "/mit/6.823/Spring16/CINT2000/197.parser/exe/parser_base.gcc4-high-opt",
        "/mit/6.823/Spring16/CINT2000/197.parser/data/all/input/2.1.dict",
        "-batch",
        "<", 
        "/mit/6.823/Spring16/CINT2000/197.parser/data/test/input/test.in"
        ],
        [
        "SWIM 1",
        "./_temp",
        "/afs/athena.mit.edu/course/6/6.823/Spring16/CFP2000/171.swim/exe/swim_base.gcc4-high-opt",
        "<",
        "/afs/athena.mit.edu/course/6/6.823/Spring16/CFP2000/171.swim/data/test/input/swim.in"
        ],
        #[
        # "MGRID 1",
        # "./_temp", 
        # "/afs/athena.mit.edu/course/6/6.823/Spring16/CFP2000/172.mgrid/exe/mgrid_base.gcc4-high-opt",
        # "<",
        # "/afs/athena.mit.edu/course/6/6.823/Spring16/CFP2000/172.mgrid/data/test/input/mgrid.in"
        #],
        [
        "APPLU 1",
        "./_temp",
        "/afs/athena.mit.edu/course/6/6.823/Spring16/CFP2000/173.applu/exe/applu_base.gcc4-high-opt",
        "<",
        "/afs/athena.mit.edu/course/6/6.823/Spring16/CFP2000/173.applu/data/test/input/applu.in"
        ],
        [
        "MESA 1",
        "./_temp",
        "/afs/athena.mit.edu/course/6/6.823/Spring16/CFP2000/177.mesa/exe/mesa_base.gcc4-high-opt",
        "-frames",
        "10",
        "-meshfile",
        "/afs/athena.mit.edu/course/6/6.823/Spring16/CFP2000/177.mesa/data/test/input/mesa.in"
        ],
        [
        "ART 1",
        "./_temp",
        "/afs/athena.mit.edu/course/6/6.823/Spring16/CFP2000/179.art/exe/art_base.gcc4-high-opt",
        "-scanfile",
        "/afs/athena.mit.edu/course/6/6.823/Spring16/CFP2000/179.art/data/test/input/c756hel.in",
        "-trainfile1",
        "/afs/athena.mit.edu/course/6/6.823/Spring16/CFP2000/179.art/data/test/input/a10.img",
        "-stride",
        "2",
        "-startx",
        "134",
        "-starty",
        "220",
        "-endx",
        "139",
        "-endy",
        "225",
        "-objects",
        "1"
        ],
        [
        "EQUAKE 1",
        "./_temp",
        "/afs/athena.mit.edu/course/6/6.823/Spring16/CFP2000/183.equake/exe/equake_base.gcc4-high-opt",
        "<",
        "/afs/athena.mit.edu/course/6/6.823/Spring16/CFP2000/183.equake/data/test/input/inp.in"
        ]
        #[ 
        # "AMMP 1",
        # "/afs/athena.mit.edu/course/6/6.823/Spring16/CFP2000/188.ammp/data/test/input",
        # "/afs/athena.mit.edu/course/6/6.823/Spring16/CFP2000/188.ammp/exe/ammp_base.gcc4-high-opt",
        # "/afs/athena.mit.edu/course/6/6.823/Spring16/CFP2000/188.ammp/data/test/input/ammp.in"
        #]               
        ); 


#run testcases
for($i = 0; $i <  scalar(@testcases); $i=$i+1) {
    #build test command
    $iplus = $i + 1;
    $testname = $testcases[$i][0];
    $numtests = scalar(@testcases);
    print "\n\nTest $iplus of $numtests: $testname\n\n"; 
    $execfile = "";
    $outfile = $resultdir;

    #set the executable
    $execdir = $testcases[$i][1];
    $execfile = $testcases[$i][2];
    @splits = split(/\//, $testcases[$i][2]);
    $outfile = $resultdir .  @splits[-1];

    #pick up the args
    for $j (3 .. $#{$testcases[$i]}) {
        $execfile = $execfile . " " . $testcases[$i][$j]; 
        # for the outfile, we want just the file name,not the path
        @splits = split(/\//, $testcases[$i][$j]);
        $outfile = $outfile ."___". @splits[-1];
    }

    $outfile = $outfile . ".out";   

    #clean < and >
    $outfile =~ s/</_/g;  
    $outfile =~ s/>/_/g;  

    # Now execute.
    $tbegin = time();
    print "Executing: \n\ncd $execdir;/mit/6.823/Spring16/Pin2012/pin -t $pintool -o $outfile -- $execfile\n\n";
    $execout = `cd $execdir;/mit/6.823/Spring16/Pin2012/pin -t $pintool -o "$outfile" -- $execfile`;
    print "Executable output: \n\n$execout\n\n";

    $ttotal = time() - $tbegin;
    print "\n\nTest took approximately: $ttotal seconds\n\n"; 
}

#kill any temporary files
print "\n\nCleaning up temporary files\n\n";
@tempfiles = `ls $tempdir`;
for $filename (@tempfiles) {
    chomp($filename);
    print "Unlinking temporary file: ${tempdir}${filename}\n";
    unlink("${tempdir}${filename}");
}

rmdir($tempdir);

