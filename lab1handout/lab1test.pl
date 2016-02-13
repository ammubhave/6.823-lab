#!/usr/athena/bin/perl
use File::Spec;

print "\nLab 1 Testing Starting\n\n";

$tool = File::Spec->rel2abs( "./caches.so" ) ;

$tbegin = time();
#invoke test script in this directory
open TB, "/mit/6.823/Spring16/drivetests.pl $tool 2>&1 |";
while(<TB>) {
  print $_;
}

$ttotal = time() - $tbegin;

print "\n\nLab 1 Testing Complete in $ttotal seconds\n\n";
