#!/usr/bin/env python

import string, time, os, stat, sys
timestamp = time.strftime('%Y-%m-%d-%H:%M:%S')

benchmarks_dir = '/mit/6.823/Spring16/Lab4Benchmarks'
# Change the following two to your absolute path
zsim = '../../zsim/build/opt/zsim'
out_dir = '../run-simple-' + timestamp

L2KB = int(sys.argv[1])
LLCMB = int(sys.argv[2])
KB = 1024
MB = KB*KB
assert(L2KB * KB * 4 + LLCMB * MB == 8388608) # total size should be 8 MB

latencyDict =     {str(256 * KB):'7',
                   str(512 * KB):'11',
                   str(1   * MB):'15',
                   str(2   * MB):'18',
                   str(4   * MB):'21',
                   str(6   * MB):'23',
                   str(7   * MB):'24'}

nsamples = 5
commands = dict(bzip2='./401.bzip2 input.source 64',
                gcc='./403.gcc -C scilab.i -o out.o',
                bwaves='./410.bwaves',
                mcf='./429.mcf inp.in',
                calculix='./454.calculix -i hyperviscoplastic',
                libquantum='./462.libquantum 1397 8',
                lbm='./470.lbm 3000 reference.dat 0 0 100_100_130_ldc.of',
                sphinx3='./482.sphinx3 ctlfile.le . args.an4',
                cactusADM='./436.cactusADM benchADM.par',
                hmmer='./456.hmmer nph3.hmm swiss41',
                xalancbmk='./483.xalancbmk -v t5.xml xalanc.xsl')

dumpInstrs = dict(bzip2='564270626L',
                gcc='555358428L',
                bwaves='685266992L',
                mcf='271630494L',
                calculix='562887276L',
                libquantum='198948984L',
                lbm='200536917L',
                sphinx3='360593408L',
                hmmer='593357498L',
                cactusADM='411207921L',
                xalancbmk='467302231L')

cfg_tmp = string.Template('''
sys = {
    numCores = $numCores;
    frequency = 1500; //MHz
};

memory = {
    lineSize = $lineSize; //bytes
    l1d = {
        size = $l1dSize; // bytes
        ways = $l1dWays;
        latency = $l1dLatency;
    };
    l1i = {
        size = $l1iSize; // bytes
        ways = $l1iWays;
        latency = $l1iLatency;
    };
    l2 = { // optional
        size = $l2Size; // bytes
        ways = $l2Ways;
        latency = $l2Latency;
    };
    llc = { // optional
        size = $llcSize; // bytes
        ways = $llcWays;
        latency = $llcLatency;
    };
    main = {
        latency = $memLatency;
    };
};

sim = {
    gmMBytes = 32;
    phaseLength = 10000; //cycles
    maxProcEventualDumps = $numCores;
    // maxMinInstrs = 400000000L; // constant work 400M
    // maxTotalInstrs = 5000000000L; // variable work
};

''')

proc_tmp = string.Template('''
process$core = {
    command = "$command";
    mask = "$core"; //where to schedule this
    dumpInstrs = $dumpInstrs;
    startFastForwarded = true;
    ffiPoints = "10000000000 10000000000000"; // fast-forward 1e9 instructions
};
''')

run_script = string.Template('#!/bin/bash' + '\n' + ('%s zsim.cfg && mv zsim-ev.h5 ../result-$timestamp-$benchmarksName-L2-%dKB-LLC-%dMB.h5' % (zsim, L2KB, LLCMB)))

dir_tmp = string.Template('$out_dir/run-$timestamp-$benchmarksName-L2-%dKB-LLC-%dMB' % (L2KB, LLCMB))
run_fn_tmp = string.Template('$out_dir/run-simple-L2-%dKB-LLC-%dMB-$timestamp.sh' % (L2KB, LLCMB))

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

cfg = dict(numCores = 1, lineSize = 64, 
           l1dSize = 32*KB, l1dWays = 4, l1dLatency = 0, # L1 latency absorbed
           l1iSize = 32*KB, l1iWays = 4, l1iLatency = 0, # by core pipeline
           l2Size = L2KB*KB, l2Ways = 8, l2Latency = latencyDict[str(L2KB*KB)], # no private L2
           llcSize = LLCMB*MB, llcWays = 16, llcLatency = latencyDict[str(LLCMB*MB)],
           memLatency = 250,
           benchmarks = ['bzip2'],
           benchmarksName = 'bzip2',
           out_dir = out_dir,
           timestamp = timestamp)


if __name__ == "__main__":
    rundirs = []
    rundirs += [mk_rundir(cfg)]
    with open(run_fn_tmp.substitute(cfg), 'w') as f:
        print >>f, '#!/bin/bash'
        for rd in rundirs:
            print >>f, 'sh -c \'cd %s; sh run.sh; cd ..; rm %s -rf\'' % (rd, rd)
    os.chmod(run_fn_tmp.substitute(cfg), stat.S_IRWXU)

