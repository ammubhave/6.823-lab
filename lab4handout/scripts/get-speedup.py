#!/usr/bin/env python

import h5py # presents HDF5 files as numpy arrays
import sys
import numpy
import os

def get_IPCs(fn):
    print 'file %s' % fn

    # Open stats file
    f = h5py.File(fn, 'r')
    
    # Get the single dataset in the file
    dset = f['stats']['root']
    IPCs = [0.0, 0.0, 0.0, 0.0]
    numCores = 4
    for sample in dset:
        if sample['trigger'] >= numCores: continue
        core = sample['trigger']
        instrs = sample['core'][core]['instrs']
        cycles = sample['core'][core]['cycles']
        IPCs[core] = float(instrs)/cycles
    return IPCs

print "Baseline"
baselineIPCs = []
for fn in sorted(os.listdir(sys.argv[1])):
    if ('.h5' not in fn):  continue
    baselineIPCs.append(get_IPCs(os.path.join(sys.argv[1],fn)))

print "Implementation"
IPCs = []
for fn in sorted(os.listdir(sys.argv[2])):
    if ('.h5' not in fn): continue
    IPCs.append(get_IPCs(os.path.join(sys.argv[2],fn)))

assert(len(baselineIPCs) == len(IPCs))

ws = []
for i in range(len(IPCs)):
    ws.append(0)
    for j in range(len(IPCs[i])):
        ws[i] += (IPCs[i][j]/baselineIPCs[i][j])
    ws[i] /= len(IPCs[i])

gmeanWS = 1
for w in ws:
    gmeanWS *= w;
print "Weighed speedup (Geometric mean)", (gmeanWS ** (1.0/len(ws)))*100, "%"

