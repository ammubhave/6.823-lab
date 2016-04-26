#!/usr/bin/env python

import string, time, os, stat, sys
from runSimple import latencyDict, commands, cfg_tmp, proc_tmp, cfg, dumpInstrs 

timestamp = time.strftime('%Y-%m-%d-%H:%M:%S')

benchmarks_dir = '/mit/6.823/Spring16/Lab4Benchmarks'
# Change the following two to your absolute path
zsim = '../../zsim/build/opt/zsim'
out_dir = '../run-mixes-' + timestamp

L2KB = int(sys.argv[1])
LLCMB = int(sys.argv[2])
KB = 1024
MB = KB*KB
assert(L2KB * KB * 4 + LLCMB * MB == 8388608) # total size should be 8 MB

nsamples = 5

mixesList = [ ['bzip2', 'gcc', 'sphinx3', 'cactusADM'],
              ['calculix', 'gcc', 'hmmer', 'cactusADM'],
              ['bzip2', 'calculix', 'sphinx3', 'hmmer']]

run_script = string.Template('#!/bin/bash' + '\n' + ('%s zsim.cfg && mv zsim-ev.h5 ../result-$timestamp-$benchmarksName-L2-%dKB-LLC-%dMB.h5' % (zsim, L2KB, LLCMB)))
dir_tmp = string.Template('$out_dir/run-$timestamp-$benchmarksName-L2-%dKB-LLC-%dMB' % (L2KB, LLCMB))
run_fn_tmp = string.Template('$out_dir/run-mixes-L2-%dKB-LLC-%dMB-$timestamp.sh' % (L2KB, LLCMB))

def mk_rundir(cfg):
    if len(cfg['benchmarks']) != cfg['numCores']:
        raise Exception('# benchmarks != # cores')
    rundir = dir_tmp.substitute(cfg)
    os.makedirs(rundir)
    for f in os.listdir(benchmarks_dir):
        os.symlink(os.path.join(benchmarks_dir, f), os.path.join(rundir, f))
    cfg_txt = cfg_tmp.substitute(cfg)
    for c,b in zip(range(0,cfg['numCores']), cfg['benchmarks']):
        cfg['core'] = c
        cfg['command'] = commands[b]
        cfg['dumpInstrs'] = dumpInstrs[b]
        cfg_txt += proc_tmp.substitute(cfg)
    with open(os.path.join(rundir, 'zsim.cfg'), 'w') as f:
        f.write(cfg_txt)
    with open(os.path.join(rundir, 'run.sh'), 'w') as f:
        f.write(run_script.substitute(cfg))
    os.chmod(os.path.join(rundir, 'run.sh'), stat.S_IRWXU)
    return rundir


if __name__ == "__main__":
    rundirs = []
    for m in mixesList :
        cfg['out_dir'] = out_dir
        cfg['numCores'] = 4
        cfg['benchmarks'] = m;
        cfg['benchmarksName'] = '-'.join(m);
        rundirs += [mk_rundir(cfg)]

    with open(run_fn_tmp.substitute(cfg), 'w') as f:
        print >>f, '#!/bin/bash'
        for rd in rundirs:
            print >>f, 'sh -c \'cd %s; sh run.sh; cd ..; rm %s -rf\'' % (rd, rd)
    os.chmod(run_fn_tmp.substitute(cfg), stat.S_IRWXU)

