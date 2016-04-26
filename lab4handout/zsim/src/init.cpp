/** $lic$
 * Copyright (C) 2012 by Massachusetts Institute of Technology
 * Copyright (C) 2010-2012 by The Board of Trustees of Stanford University
 *
 * This file is part of zsim, the zcache simulator. zsim was mainly
 * developed at MIT and Stanford by Daniel Sanchez <sanchez@csail.mit.edu>.
 *
 * If you use this software in your research, we request that you reference
 * the zcache paper ("The ZCache: Decoupling ways and Associativity", Sanchez
 * and Kozyrakis, MICRO-44, December 2010) as the source of the simulator in
 * any publications that use this software, and that you send us a citation of
 * your work.
 *
 * Until we release zsim, only MIT and Stanford students and faculty using
 * university equipment are allowed to use zsim. We will release zsim under
 * a GPLv2 license on 2013, upon publication of a paper about it, currently
 * under submission. Until then, you are required to keep this within MIT
 * and/or Stanford. 
 *
 * zsim is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.
 */

#include <list>
#include <cstdlib>
#include <sys/time.h>
#include <sstream>
#include <string.h>

#include "debug_zsim.h"
#include "galloc.h"
#include "zsim.h"
#include "init.h"

#include "config.h"
#include "log.h"
#include "pin_cmd.h"

#include "constants.h"
#include "locks.h"

#include "event_queue.h"
#include "scheduler.h"

#include "stats.h"
#include "profile_stats.h"

#include "core.h"
#include "tracing_core.h"
#include "cache.h"
#include "hash.h"

#include "process_tree.h"
#include "port_virt.h"

#include "main_memory.h"
#include "cache.h"

#include "tracing_driver.h"

using namespace std;

extern void EndOfPhaseActions(); //in zsim.cpp

/* zsim should be initialized in a deterministic and logical order, to avoid re-reading config vars
 * all over the place and give a predictable global state to constructors. Ideally, this should just
 * follow the layout of zinfo, top-down.
 */

static void InitSystem(Config& config) {
    //Instantiate the cores
    uint32_t numCores = zinfo->numCores;
    TracingCore* tracingCores = gm_memalign<TracingCore>(CACHE_LINE_BYTES, numCores);
    g_vector<Cache *> caches;

    // Memory hierarchy
    Memory *sharedMem = new MainMemory("mem", config.get<uint32_t>("memory.main.latency"));
    //SharedCache * sharedCache = nullptr;
    Cache * sharedCache = nullptr;
    uint32_t lineSize = config.get<uint32_t>("memory.lineSize");
    assert_msg(config.exists("memory.llc"), "LLC does not exist!");

    if (config.exists("memory.llc") && config.get<uint32_t>("memory.llc.size") != 0) {
        uint32_t nBytes = config.get<uint32_t>("memory.llc.size");
        info("LLC size in MB: %d", nBytes / 1024 / 1024);
        uint32_t nWays = config.get<uint32_t>("memory.llc.ways");
        assert(nBytes % (lineSize * nWays) == 0);
        uint32_t latency = config.get<uint32_t>("memory.llc.latency");
        uint32_t nLines = nBytes / (lineSize * nWays);
        sharedCache = new Cache("llc", nLines, nWays, latency, false, sharedMem);
    }

    for (uint32_t j = 0; j < numCores; j++) {
        Memory *mem = sharedCache;
        Cache* L2 = nullptr;
        if (config.exists("memory.l2") && config.get<uint32_t>("memory.l2.size") != 0) {
            stringstream ss;
            ss << "l2-" << j;
            g_string name(ss.str().c_str());
            uint32_t nBytes = config.get<uint32_t>("memory.l2.size");
            info("L2 size in KB: %d", nBytes / 1024);
            uint32_t nWays = config.get<uint32_t>("memory.l2.ways");
            assert(nBytes % (lineSize * nWays) == 0);
            uint32_t nLines = nBytes / (lineSize * nWays);
            uint32_t latency = config.get<uint32_t>("memory.l2.latency");
            L2 = new Cache(name, nLines, nWays, latency, false, mem);
            caches.push_back(L2);
            sharedCache->addChild(L2);
            mem = L2;
        }

        Memory *l1i, *l1d;
        if (config.exists("memory.l1i") && config.get<uint32_t>("memory.l1i.size") != 0) {
            stringstream ss;
            ss << "l1i-" << j;
            g_string name(ss.str().c_str());
            uint32_t nBytes = config.get<uint32_t>("memory.l1i.size");
            uint32_t nWays = config.get<uint32_t>("memory.l1i.ways");
            assert(nBytes % (lineSize * nWays) == 0);
            uint32_t nLines = nBytes / (lineSize * nWays);
            uint32_t latency = config.get<uint32_t>("memory.l1i.latency");
            Cache *c = new Cache(name, nLines, nWays, latency, true, L2);
            caches.push_back(c);
            L2->addChild(c);
            l1i = c;
        } else {
            l1i = mem;
	}
        if (config.exists("memory.l1d") && config.get<uint32_t>("memory.l1d.size") != 0) {
            stringstream ss;
            ss << "l1d-" << j;
            g_string name(ss.str().c_str());
            uint32_t nBytes = config.get<uint32_t>("memory.l1d.size");
            uint32_t nWays = config.get<uint32_t>("memory.l1d.ways");
            assert(nBytes % (lineSize * nWays) == 0);
            uint32_t nLines = nBytes / (lineSize * nWays);
            uint32_t latency = config.get<uint32_t>("memory.l1d.latency");
            Cache *c = new Cache(name, nLines, nWays, latency, true, L2);
            caches.push_back(c);
            L2->addChild(c);
            l1d = c;
        } else {
            l1d = mem;
	}
        {
            stringstream ss;
            ss << "core-" << j;
            g_string name(ss.str().c_str());
            new(&tracingCores[j]) TracingCore(name, l1i, l1d);
        }
    }

    //Populate global core info
    zinfo->cores = gm_memalign<Core*>(CACHE_LINE_BYTES, zinfo->numCores);
    for (uint32_t j = 0; j < numCores; j++) zinfo->cores[j] = &tracingCores[j];

    //Init stats
    AggregateStat* coresStat = new AggregateStat(true);
    coresStat->init("core", "Core stats");
    for (uint32_t j = 0; j < numCores; j++) tracingCores[j].initStats(coresStat);
    zinfo->rootStat->append(coresStat);

    AggregateStat* cachesStat = new AggregateStat();
    cachesStat->init("cache", "Cache stats");
    for (auto c : caches) c->initStats(cachesStat);
    sharedCache->initStats(cachesStat);
    zinfo->rootStat->append(cachesStat);

    info("Initialized system");
}

static void PreInitStats() {
    zinfo->rootStat = new AggregateStat();
    zinfo->rootStat->init("root", "Stats");
}

static void PostInitStats(bool perProcessDir) {
    zinfo->rootStat->makeImmutable();
    zinfo->trigger = 15000;

    string pathStr = zinfo->outputDir;
    pathStr += "/";
    
    // Absolute paths for stats files. Note these must be in the global heap.
    const char* pStatsFile = gm_strdup((pathStr + "zsim.h5").c_str());
    const char* evStatsFile = gm_strdup((pathStr + "zsim-ev.h5").c_str());
    const char* cmpStatsFile = gm_strdup((pathStr + "zsim-cmp.h5").c_str());
    const char* statsFile = gm_strdup((pathStr + "zsim.out").c_str());

    if (zinfo->statsPhaseInterval) {
       zinfo->periodicStatsBackend = new HDF5Backend(pStatsFile, zinfo->rootStat, (1<<20) /* 1MB chunks */, zinfo->skipStatsVectors, zinfo->compactPeriodicStats);
       zinfo->periodicStatsBackend->dump(true); //must have a first sample

       class PeriodicStatsDumpEvent : public Event {
           public:
               PeriodicStatsDumpEvent(uint32_t period) : Event(period) {};
               void callback() {
                   zinfo->trigger = 10000;
                   zinfo->periodicStatsBackend->dump(true /*buffered*/);    
               }
       };

       zinfo->eventQueue->insert(new PeriodicStatsDumpEvent(zinfo->statsPhaseInterval));

    } else {
        zinfo->periodicStatsBackend = NULL;
    }
    
    zinfo->eventualStatsBackend = new HDF5Backend(evStatsFile, zinfo->rootStat, (1<<17) /* 128KB chunks */, zinfo->skipStatsVectors, false /* don't sum regular aggregates*/);
    zinfo->eventualStatsBackend->dump(true); //must have a first sample
    
    if (zinfo->maxMinInstrs) {
        class CoreInstrsStatsDumpEvent : public Event {
            private:
                uint64_t targetInstrs;
                uint32_t cid;
                //enum {MAX_IPC = 4};
            public:
                CoreInstrsStatsDumpEvent(uint64_t _targetInstrs, uint32_t _cid) : Event(0), targetInstrs(_targetInstrs), cid(_cid) {
                    period = targetInstrs/MAX_IPC/zinfo->phaseLength;
                }

                //This will fire a bunch of times, we adjust the period to get the exact phase
                void callback() {
                    uint64_t instrs = zinfo->cores[cid]->getInstrs();
                    if (instrs >= targetInstrs) {
                        info("Dumping eventual stats for core %d", cid);
                        zinfo->trigger = cid;
                        zinfo->eventualStatsBackend->dump(true /*buffered*/);

                        period = 0; //event queue will dispose of us
                    } else {
                        period = (targetInstrs-instrs)/MAX_IPC/zinfo->phaseLength;
                        if (period == 0) period = 1;
                    }
                    //info("Callback, core %d, instrs %ld, phase %ld, period %ld", cid, instrs, zinfo->numPhases, period);
                }
        };

        for (uint32_t i = 0; i < zinfo->numCores; i++) {
            zinfo->eventQueue->insert(new CoreInstrsStatsDumpEvent(zinfo->maxMinInstrs, i));
        }
    }

    zinfo->compactStatsBackend = new HDF5Backend(cmpStatsFile, zinfo->rootStat, 0 /* no aggregation, this is just 1 record */, zinfo->skipStatsVectors, true); //don't dump a first sample.

    zinfo->statsBackend = new TextBackend(statsFile, zinfo->rootStat);
}

static void InitGlobalStats() {
    zinfo->profSimTime = new TimeBreakdownStat();
    const char* stateNames[] = {"init", "bound", "weave", "ff"};
    zinfo->profSimTime->init("time", "Simulator time breakdown", 4, stateNames);
    zinfo->rootStat->append(zinfo->profSimTime);

    ProxyStat* triggerStat = new ProxyStat();
    triggerStat->init("trigger", "Reason for this stats dump", &zinfo->trigger);
    zinfo->rootStat->append(triggerStat);

    ProxyStat* phaseStat = new ProxyStat();
    phaseStat->init("phase", "Simulated phases", &zinfo->numPhases);
    zinfo->rootStat->append(phaseStat);
}


void SimInit(const char* configFile, const char* outputDir, uint32_t shmid) {
    srandom(time(NULL));

    zinfo = gm_calloc<GlobSimInfo>();
    zinfo->outputDir = gm_strdup(outputDir);

    Config config(configFile);

    //Debugging
    //NOTE: This should be as early as possible, so that we can attach to the debugger before initialization.
    zinfo->attachDebugger = config.get<bool>("sim.attachDebugger", false);
    zinfo->harnessPid = getppid();
    getLibzsimAddrs(&zinfo->libzsimAddrs);
    
    if (zinfo->attachDebugger) {
        gm_set_secondary_ptr(&zinfo->libzsimAddrs);
        notifyHarnessForDebugger(zinfo->harnessPid);
    }

    PreInitStats();

    //Get the number of cores
    //TODO: There is some duplication with the core creation code. This should be fixed evantually.
    uint32_t numCores = config.get<uint32_t>("sys.numCores");
    if (numCores == 0) panic("sys.numCores must be > 0");
    zinfo->numCores = numCores;
    assert(numCores <= MAX_THREADS); //TODO: Is there any reason for this limit?

    // Global simulation values
    zinfo->numPhases = 0;

    zinfo->phaseLength = config.get<uint32_t>("sim.phaseLength", 10000);
    zinfo->statsPhaseInterval = config.get<uint32_t>("sim.statsPhaseInterval", 100);
    zinfo->freqMHz = config.get<uint32_t>("sys.frequency", 2000);

    //Maxima/termination conditions
    zinfo->maxPhases = config.get<uint64_t>("sim.maxPhases", 0);
    zinfo->maxMinInstrs = config.get<uint64_t>("sim.maxMinInstrs", 0);
    zinfo->maxTotalInstrs = config.get<uint64_t>("sim.maxTotalInstrs", 0);
    zinfo->maxProcEventualDumps = config.get<uint32_t>("sim.maxProcEventualDumps", 0);
    uint64_t maxSimTime = config.get<uint32_t>("sim.maxSimTime", 0);
    zinfo->maxSimTimeNs = maxSimTime*1000L*1000L*1000L;

    zinfo->skipStatsVectors = config.get<bool>("sim.skipStatsVectors", false);
    zinfo->compactPeriodicStats = config.get<bool>("sim.compactPeriodicStats", false);

    //Fast-forwarding and magic ops
    zinfo->ignoreHooks = config.get<bool>("sim.ignoreHooks", false);
    zinfo->ffReinstrument = config.get<bool>("sim.ffReinstrument", false);
    if (zinfo->ffReinstrument) warn("sim.ffReinstrument = true, switching fast-forwarding on a multi-threaded process may be unstable");

    zinfo->registerThreads = config.get<bool>("sim.registerThreads", false);
    zinfo->globalPauseFlag = config.get<bool>("sim.startInGlobalPause", false);

    zinfo->eventQueue = new EventQueue(); //must be instantiated before the memory hierarchy

    //Build the scheduler
    uint32_t parallelism = config.get<uint32_t>("sim.parallelism", 2*sysconf(_SC_NPROCESSORS_ONLN));
    if (parallelism < zinfo->numCores) info("Limiting concurrent threads to %d", parallelism);
    assert(parallelism > 0); //jeez...
    
    uint32_t schedQuantum = config.get<uint32_t>("sim.schedQuantum", 10000); //phases
    zinfo->sched = new Scheduler(EndOfPhaseActions, parallelism, zinfo->numCores, schedQuantum);

    InitGlobalStats();

    //Core stats (initialized here for cosmetic reasons, to be above cache stats)
    AggregateStat* allCoreStats = new AggregateStat(false);
    allCoreStats->init("core", "Core stats");
    zinfo->rootStat->append(allCoreStats);

    //Process tree needs this initialized, even though it is part of the memory hierarchy
    zinfo->lineSize = config.get<uint32_t>("sys.lineSize", 64);
    assert(zinfo->lineSize > 0);

    //Port virtualization
    for (uint32_t i = 0; i < MAX_PORT_DOMAINS; i++) zinfo->portVirt[i] = new PortVirtualizer();

    //Process hierarchy
    //NOTE: Due to partitioning, must be done before initializing memory hierarchy
    CreateProcessTree(config);
    zinfo->procArray[0]->notifyStart(); //called here so that we can detect end-before-start races 

    zinfo->pinCmd = new PinCmd(&config, NULL /*don't pass config file to children --- can go either way, it's optional*/, outputDir, shmid);

    //Caches, cores, memory controllers
    InitSystem(config);

    //Sched stats (deferred because of circular deps)
    zinfo->sched->initStats(zinfo->rootStat);

    //Tracing driver (process-local; has to be done post InitSystem)
    zinfo->tracingDriver = new TracingDriver();

    bool perProcessDir = config.get<bool>("sim.perProcessDir", false);
    PostInitStats(perProcessDir);

    //Odds and ends
    bool printMemoryStats = config.get<bool>("sim.printMemoryStats", false);
    if (printMemoryStats) {
        gm_stats();
    }

    //EOP locks
    futex_init(&zinfo->eopWait);
    futex_init(&zinfo->eopSignal);
    futex_lock(&zinfo->eopWait);
    futex_lock(&zinfo->eopSignal);

    //HACK: Read all variables that are read in the harness but not in init
    //This avoids warnings on those elements
    config.get<uint32_t>("sim.gmMBytes", (1<<10));
    if (!zinfo->attachDebugger) config.get<bool>("sim.deadlockDetection", true);

    //Write config out
    bool strictConfig = config.get<bool>("sim.strictConfig", true); //if true, panic on unused variables
    config.writeAndClose((string(zinfo->outputDir) + "/out.cfg").c_str(), strictConfig);

    info("Initialization complete");

    //Causes every other process to wake up
    gm_set_glob_ptr(zinfo);
}

