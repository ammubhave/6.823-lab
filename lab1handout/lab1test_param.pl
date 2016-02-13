#!/usr/athena/bin/perl
use File::Spec;

if (scalar(@ARGV) != 6)
{
#    print scalar(@ARGV);
    print "\nUsage: ./lab1test_param.pl -b <logBlockSize> -r <logNumRows> -a <associativity>\n";
    exit();
}

print "\nLab 1 Testing Starting\n\n";

$tool = File::Spec->rel2abs( "./caches.so" ) ;

$read_r = "0";
$read_a = "0";
$read_b = "0";

$logNumRows = 0;
$assoc = 0;
$logBlockSize = 0;

foreach $arg(@ARGV)
{
    if ($read_b == "1") {
        $logBlockSize = $arg;
        $read_b = "0";
        print "\nlogBlockSize = $arg";
    }

    if ($read_r == "1") {
        $logNumRows = $arg;
        $read_r = "0";
        print "\nlogNumRows = $arg";
    }

    if ($read_a == "1") {
        $assoc = $arg;
        $read_a = "0";
        print "\nAssociativity = $arg";
    }


    if ($arg =~ m/-r/) {
        $read_r = "1";
        $read_a = "0";
        $read_b = "0";
    }

    if ($arg =~ m/-a/) {
        $read_a = "1";
        $read_r = "0";
        $read_b = "0";
    }

    if ($arg =~ m/-b/) {
        $read_b = "1";
        $read_r = "0";
        $read_a = "0";
    }
}

$tbegin = time();
#invoke test script in this directory
open TB, "./drivetests_caches.pl $tool -b $logBlockSize -r $logNumRows -a $assoc 2>&1 |";
while(<TB>) {
    print $_;
}

$ttotal = time() - $tbegin;

print "\n\nLab 1 Testing Complete in $ttotal seconds\n\n";
