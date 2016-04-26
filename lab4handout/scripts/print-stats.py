#!/usr/bin/env python

import h5py # presents HDF5 files as numpy arrays
import sys, os
import numpy

def print_stats(fn):
    # Open stats file
    f = h5py.File(fn, 'r')
    # Get the single dataset in the file
    dset = f['stats']['root']
    IPCs = [0.0, 0.0, 0.0, 0.0]
    print 'file %s' % fn
    numCores = 4
    for sample in dset:
        if sample['trigger'] >= numCores: continue
        core = sample['trigger']
        instrs = sample['core'][core]['instrs']
        cycles = sample['core'][core]['cycles']
        print '  core %d: instrs %d cycles %d IPC %.2f' % (core, instrs, cycles, float(instrs)/cycles)
        IPCs[core] = float(instrs)/cycles
        try:
            l1d_hits = sample['cache']['l1d-%d' % core]['hits']
            l1d_misses = sample['cache']['l1d-%d' % core]['misses']
            l1d_invs = sample['cache']['l1d-%d' % core]['invalidations']
	    print '  l1d %d: hits %d misses %d invalidations %d MPKI %.2f' % (core, l1d_hits, l1d_misses, l1d_invs, (l1d_misses*1000.0)/instrs)
        except IndexError: pass # cache not defined
        try:
            l1i_hits = sample['cache']['l1i-%d' % core]['hits']
            l1i_misses = sample['cache']['l1i-%d' % core]['misses']
	    print '  l1i %d: hits %d misses %d MPKI %.2f' % (core, l1i_hits, l1i_misses, (l1i_misses*1000.0)/instrs)
        except IndexError: pass # cache not defined
        try:
            l2_hits = sample['cache']['l2-%d' % core]['hits']
            l2_misses = sample['cache']['l2-%d' % core]['misses']
            l2_invs = sample['cache']['l2-%d' % core]['invalidations']
	    print '  l2 %d: hits %d misses %d invalidations %d MPKI %.2f' % (core, l2_hits, l2_misses, l2_invs, (l2_misses*1000.0)/instrs)
        except IndexError: pass # cache not defined

    llc_hits = sample['cache']['llc']['hits']
    llc_misses = sample['cache']['llc']['misses']
    print '  llc: hit rate %.2f' % (llc_hits*1.0/(llc_hits+llc_misses))

for fn in sorted(os.listdir(sys.argv[1])):
    if 'h5' not in fn : continue
    print_stats(os.path.join(sys.argv[1],fn))
