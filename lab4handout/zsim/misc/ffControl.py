#!/usr/bin/python

import os, sys, subprocess
from optparse import OptionParser

parser = OptionParser()
parser.add_option("--procIdx", type="int", default=0, dest="procIdx", help="Process index to signal") 
parser.add_option("--lineMatch", default=" ROI", dest="lineMatch", help="Matching line to stdin will trigger signal")
parser.add_option("--maxMatches", type="int", default=0, dest="maxMatches", help="Exit after this many matches (0 to disable)")
parser.add_option("--fftogglePath", default="./build/opt", dest="fftogglePath", help="")
(opts, args) = parser.parse_args()

targetShmid = -1
matches = 0
while matches < opts.maxMatches or opts.maxMatches <= 0:
    try:
        line = sys.stdin.readline()
    except:
        print "stdin done, exiting"
        break
   
    if line.startswith("[H] Global segment shmid = "):
        targetShmid = int(line.split("=")[1].lstrip().rstrip())
        print "Target shmid is", targetShmid

    if line.find(opts.lineMatch) >= 0:
        if targetShmid >= 0:
            print "Match, calling fftoggle"
            matches += 1
            subprocess.call([os.path.join(opts.fftogglePath, "fftoggle"), str(targetShmid), str(opts.procIdx)])
        else:
            print "Match but shmid is not valid, not sending signal (are you sure you specified procIdx correctly? it's not the PID)"
print "Done, %d matches" % matches

