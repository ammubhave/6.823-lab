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

/* The Pin-facing part of the simulator */

#include <bits/signum.h>
#include <execinfo.h>
#include <fstream>
#include <iostream>
#include <sched.h>
//#include <signal.h> //can't include this, conflicts with PIN's
#include <sstream>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>

#include "pin.H"

#include "constants.h"
#include "core.h"
#include "debug_zsim.h"
#include "event_queue.h"
#include "galloc.h"
#include "init.h"
#include "log.h"
#include "memory_hierarchy.h"
#include "pin_cmd.h"
#include "process_tree.h"
#include "profile_stats.h"
#include "scheduler.h"
#include "stats.h"
#include "syscall_funcs.h"
#include "zsim.h"

#include "tracing_driver.h"

#include <dlfcn.h>

/* Command-line switches (used to pass info from harness that cannot be passed through the config file, most config is file-based) */

KNOB<INT32> KnobProcIdx(KNOB_MODE_WRITEONCE, "pintool",
        "procIdx","0", "zsim process idx (internal)");

KNOB<INT32> KnobShmid(KNOB_MODE_WRITEONCE, "pintool",
        "shmid","0", "SysV IPC shared memory id used when running in multi-process mode");

KNOB<string> KnobConfigFile(KNOB_MODE_WRITEONCE, "pintool",
        "config", "zsim.cfg", "config file name (only needed for the first simulated process)");

//We need to know these as soon as we start, otherwise we could not log anything until we attach and read the config
KNOB<bool> KnobLogToFile(KNOB_MODE_WRITEONCE, "pintool",
        "logToFile", "false", "true if all messages should be logged to a logfile instead of stdout/err");

KNOB<string> KnobOutputDir(KNOB_MODE_WRITEONCE, "pintool",
        "outputDir", "./", "absolute path to write output files into");



/* ===================================================================== */

INT32 Usage() {
    cerr << "zsim simulator pintool" << endl;
    cerr << KNOB_BASE::StringKnobSummary();
    cerr << endl;
    return -1;
}

/* Global Variables */

GlobSimInfo* zinfo;

/* Per-process variables */

int procIdx;
int lineBits; //process-local for performance, but logically global
Address procMask;

static ProcessTreeNode* procTreeNode;

//tid to cid translation
#define INVALID_CID ((uint32_t)-1)
#define UNINITIALIZED_CID ((uint32_t)-2) //Value set at initialization

static uint32_t cids[MAX_THREADS];

//Per TID core pointers (TODO: Need to phase out cid/tid crap)
Core* cores[MAX_THREADS];

static inline void clearCid(uint32_t tid) {
    assert(tid < MAX_THREADS);
    assert(cids[tid] != INVALID_CID);
    cids[tid] = INVALID_CID;
    cores[tid] = NULL;
}

static inline void setCid(uint32_t tid, uint32_t cid) {
    assert(tid < MAX_THREADS);
    assert(cids[tid] == INVALID_CID);
    assert(cid < zinfo->numCores);
    cids[tid] = cid;
    cores[tid] = zinfo->cores[cid];
}

uint32_t getCid(uint32_t tid) {
    //assert(tid < MAX_THREADS); //these assertions are fine, but getCid is called everywhere, so they are expensive!
    uint32_t cid = cids[tid];
    //assert(cid != INVALID_CID);
    return cid;
}

bool isInstrAccess(uint32_t tid) {
    panic("Outdated, FIXME!");
    return false;
}

// Internal function declarations
void EnterFastForward();
void ExitFastForward();

VOID SimThreadStart(THREADID tid);
VOID SimThreadFini(THREADID tid);
VOID SimEnd();

VOID HandleMagicOp(THREADID tid, ADDRINT op);

VOID FakeCPUIDPre(THREADID tid, REG eax);
VOID FakeCPUIDPost(THREADID tid, ADDRINT* eax, ADDRINT* ebx, ADDRINT* ecx, ADDRINT* edx); //REG* eax, REG* ebx, REG* ecx, REG* edx);

VOID FakeRDTSCPost(THREADID tid, REG* eax, REG* edx);

VOID VdsoInstrument(INS ins);

/* Indirect analysis calls to work around PIN's synchronization 
 *
 * NOTE(dsm): Be extremely careful when modifying this code. It is simple, but it runs VERY frequently.
 * For example, with 24-byte structs on a fairly unoptimized L1 cache, this code introduced a 4%
 * overhead, down to 2% with 32-byte structs. Also, be aware that a miss or unpredictable indirect jump
 * is about the worst kind of pain you can inflict on an ooo core, so ensure that 1) there's no false sharing,
 * and 2) these pointers are modified sparingly.
 */

InstrFuncPtrs fPtrs[MAX_THREADS] __attribute__(( aligned(CACHE_LINE_BYTES) )); //minimize false sharing

VOID PIN_FAST_ANALYSIS_CALL IndirectLoadSingle(THREADID tid, ADDRINT addr) {
    fPtrs[tid].loadPtr(tid, addr);
}

VOID PIN_FAST_ANALYSIS_CALL IndirectStoreSingle(THREADID tid, ADDRINT addr) {
    fPtrs[tid].storePtr(tid, addr);
}

VOID PIN_FAST_ANALYSIS_CALL IndirectBasicBlock(THREADID tid, ADDRINT bblAddr, BblInfo* bblInfo) {
    fPtrs[tid].bblPtr(tid, bblAddr, bblInfo);
}

VOID PIN_FAST_ANALYSIS_CALL IndirectRecordBranch(THREADID tid, ADDRINT branchPc, BOOL taken, ADDRINT takenNpc, ADDRINT notTakenNpc) {
    fPtrs[tid].branchPtr(tid, branchPc, taken, takenNpc, notTakenNpc);
}

VOID PIN_FAST_ANALYSIS_CALL IndirectPredLoadSingle(THREADID tid, ADDRINT addr, BOOL pred) {
    fPtrs[tid].predLoadPtr(tid, addr, pred);
}

VOID PIN_FAST_ANALYSIS_CALL IndirectPredStoreSingle(THREADID tid, ADDRINT addr, BOOL pred) {
    fPtrs[tid].predStorePtr(tid, addr, pred);
}


//Non-simulation variants of analysis functions

// Join variants: Call join on the next instrumentation poin and return to analysis code
void Join(uint32_t tid) {
    assert(fPtrs[tid].type == FPTR_JOIN);
    uint32_t cid = zinfo->sched->join(procIdx, tid); //can block
    setCid(tid, cid);
    fPtrs[tid] = cores[tid]->GetFuncPtrs(); //back to normal pointers
}

VOID JoinAndLoadSingle(THREADID tid, ADDRINT addr) {
    Join(tid);
    fPtrs[tid].loadPtr(tid, addr);
}

VOID JoinAndStoreSingle(THREADID tid, ADDRINT addr) {
    Join(tid);
    fPtrs[tid].storePtr(tid, addr);
}

VOID JoinAndBasicBlock(THREADID tid, ADDRINT bblAddr, BblInfo* bblInfo) {
    Join(tid);
    fPtrs[tid].bblPtr(tid, bblAddr, bblInfo);
}

VOID JoinAndRecordBranch(THREADID tid, ADDRINT branchPc, BOOL taken, ADDRINT takenNpc, ADDRINT notTakenNpc) {
    Join(tid);
    fPtrs[tid].branchPtr(tid, branchPc, taken, takenNpc, notTakenNpc);
}

VOID JoinAndPredLoadSingle(THREADID tid, ADDRINT addr, BOOL pred) {
    Join(tid);
    fPtrs[tid].predLoadPtr(tid, addr, pred);
}

VOID JoinAndPredStoreSingle(THREADID tid, ADDRINT addr, BOOL pred) {
    Join(tid);
    fPtrs[tid].predStorePtr(tid, addr, pred);
}

// NOP variants: Do nothing
VOID NOPLoadStoreSingle(THREADID tid, ADDRINT addr) {}
VOID NOPBasicBlock(THREADID tid, ADDRINT bblAddr, BblInfo* bblInfo) {}
VOID NOPRecordBranch(THREADID tid, ADDRINT addr, BOOL taken, ADDRINT takenNpc, ADDRINT notTakenNpc) {}
VOID NOPPredLoadStoreSingle(THREADID tid, ADDRINT addr, BOOL pred) {}

// FF is basically NOP except for basic blocks
VOID FFBasicBlock(THREADID tid, ADDRINT bblAddr, BblInfo* bblInfo) {
    if (unlikely(!procTreeNode->isInFastForward())) {
        SimThreadStart(tid);
    }
}

// FFI is instruction-based fast-forwarding; ONLY FOR SINGLE-THREADED PROCESSES
/* FFI works as follows: when in fast-forward, we install a special FF BBL func ptr that counts instructions
 * and checks whether we have reached the switch point. Then, it exits FF, and queues an event that counts
 * the instructions where the app should be scheduled. That event cannot access any local state, so when it
 * hits the limit, it just makes the process enter FF. On that entry, we install a special handler that advances
 * to the next FFI point and installs the normal FFI handlers (pretty much like joins work).
 */

// FFI state
static bool ffiEnabled;
static uint32_t ffiPoint;
static uint64_t ffiInstrsDone;
static uint64_t ffiInstrsLimit;
static uint32_t ffiCore;
static bool ffiNFF;
static uint64_t ffiNFFStartInstrs;

static const InstrFuncPtrs& GetFFPtrs();

VOID FFITrackNFFInterval() {
    class FFIEvent : public Event {
        private:
            uint64_t startInstrs;
            uint64_t targetInstrs;
            uint32_t cid;
            uint32_t pid;
            //enum {MAX_IPC = 4};
        public:
            FFIEvent(uint64_t _targetInstrs, uint32_t _cid, uint32_t _pid) : Event(0), targetInstrs(_targetInstrs), cid(_cid), pid(_pid) {
                startInstrs = zinfo->cores[cid]->getInstrs();
                period = targetInstrs/MAX_IPC/zinfo->phaseLength;
            }

            // This will fire a bunch of times, we adjust the period to get the exact phase
            // Gets called from an arbitrary process, cannot touch any proc-local state (including FFI)
            void callback() {
                uint64_t instrs = zinfo->cores[cid]->getInstrs() - startInstrs;
                if (instrs >= targetInstrs) {
                    info("FFI: Entering fast-forward for process %d", pid);
                    /* Note this is sufficient due to the lack of reinstruments on FF, and this way we do not need to touch global state */
                    futex_lock(&zinfo->ffLock);
                    assert(!zinfo->procArray[pid]->isInFastForward());
                    zinfo->procArray[pid]->enterFastForward();
                    futex_unlock(&zinfo->ffLock);

                    period = 0; //event queue will dispose of us
                } else {
                    period = (targetInstrs-instrs)/MAX_IPC/zinfo->phaseLength;
                    if (period == 0) period = 1;
                }
                //info("FFIEvent callback(): pid %d cid %d target %ld instrs %ld period %ld", pid, cid, targetInstrs, instrs, period);
            }
    };

    assert(!procTreeNode->isInFastForward());
    assert(ffiInstrsDone < ffiInstrsLimit); //unless you have ~10-instr FFWds, this does not happen
    zinfo->eventQueue->insert(new FFIEvent(ffiInstrsLimit - ffiInstrsDone, ffiCore, procIdx));
    ffiNFFStartInstrs = zinfo->cores[ffiCore]->getInstrs();;
    ffiNFF = true;
}

// Called on process start
VOID FFIInit() {
    const g_vector<uint64_t>& ffiPoints = procTreeNode->getFFIPoints();
    if (!ffiPoints.empty()) {
        if (zinfo->ffReinstrument) panic("FFI and reinstrumenting on FF switches are incompatible");
        ffiEnabled = true;
        ffiPoint = 0;
        ffiInstrsDone = 0;
        ffiInstrsLimit = ffiPoints[0];
        //Find the core by looking at the first mask
        const g_vector<bool>& mask = procTreeNode->getMask();
        ffiCore = mask.size();
        for (uint32_t i = 0 ; i < mask.size(); i++) {
            if (mask[i]) {
                if (ffiCore != mask.size()) panic("More than 1 bit set in process mask, FFI mode requires exact mask");
                ffiCore = i;
            }
        }
        assert(ffiCore < mask.size());

        ffiNFFStartInstrs = 0;
        ffiNFF = false;
        info("FFI mode initialized, %ld ffiPoints", ffiPoints.size());
        if (!procTreeNode->isInFastForward()) FFITrackNFFInterval();
    } else {
        ffiEnabled = false;
    }
}

//Set the next ffiPoint, or finish
VOID FFIAdvance() {
    const g_vector<uint64_t>& ffiPoints = procTreeNode->getFFIPoints();
    ffiPoint++;
    if (ffiPoint >= ffiPoints.size()) {
        info("Last ffiPoint reached, %ld instrs, limit %ld", ffiInstrsDone, ffiInstrsLimit);
        SimEnd();
    } else {
        info("ffiPoint reached, %ld instrs, limit %ld", ffiInstrsDone, ffiInstrsLimit);
        ffiInstrsLimit += ffiPoints[ffiPoint];
    }
}

VOID FFIBasicBlock(THREADID tid, ADDRINT bblAddr, BblInfo* bblInfo) {
    ffiInstrsDone += bblInfo->instrs;
    if (unlikely(ffiInstrsDone >= ffiInstrsLimit)) {
        FFIAdvance();
        assert(procTreeNode->isInFastForward());
        futex_lock(&zinfo->ffLock);
        info("FFI: Exiting fast-forward");
        ExitFastForward();
        futex_unlock(&zinfo->ffLock);
        FFITrackNFFInterval();

        SimThreadStart(tid);
    }
}

// One-off, called after we go from NFF to FF
VOID FFIEntryBasicBlock(THREADID tid, ADDRINT bblAddr, BblInfo* bblInfo) {
    ffiInstrsDone += zinfo->cores[ffiCore]->getInstrs() - ffiNFFStartInstrs;
    FFIAdvance();
    assert(ffiNFF);
    ffiNFF = false;
    fPtrs[tid] = GetFFPtrs();
    FFIBasicBlock(tid, bblAddr, bblInfo);
}

// Non-analysis pointer vars
static const InstrFuncPtrs joinPtrs = {JoinAndLoadSingle, JoinAndStoreSingle, JoinAndBasicBlock, JoinAndRecordBranch, JoinAndPredLoadSingle, JoinAndPredStoreSingle, FPTR_JOIN};
static const InstrFuncPtrs nopPtrs = {NOPLoadStoreSingle, NOPLoadStoreSingle, NOPBasicBlock, NOPRecordBranch, NOPPredLoadStoreSingle, NOPPredLoadStoreSingle, FPTR_NOP};
static const InstrFuncPtrs ffPtrs = {NOPLoadStoreSingle, NOPLoadStoreSingle, FFBasicBlock, NOPRecordBranch, NOPPredLoadStoreSingle, NOPPredLoadStoreSingle, FPTR_NOP};

static const InstrFuncPtrs ffiPtrs = {NOPLoadStoreSingle, NOPLoadStoreSingle, FFIBasicBlock, NOPRecordBranch, NOPPredLoadStoreSingle, NOPPredLoadStoreSingle, FPTR_NOP};
static const InstrFuncPtrs ffiEntryPtrs = {NOPLoadStoreSingle, NOPLoadStoreSingle, FFIEntryBasicBlock, NOPRecordBranch, NOPPredLoadStoreSingle, NOPPredLoadStoreSingle, FPTR_NOP};

static const InstrFuncPtrs& GetFFPtrs() {
    return ffiEnabled? (ffiNFF? ffiEntryPtrs : ffiPtrs) : ffPtrs;
}

//Fast-forwarding
void EnterFastForward() {
    assert(!procTreeNode->isInFastForward());
    procTreeNode->enterFastForward();
    __sync_synchronize(); //Make change globally visible

    //Re-instrument; VM/client lock are not needed
    if (zinfo->ffReinstrument) {
        PIN_RemoveInstrumentation();
    }
    //Transition to FF; we have the ff lock, this should be safe with end of phase code. This avoids profiling the end of a simulation as bound time
    //NOTE: Does not work well with multiprocess runs
    zinfo->profSimTime->transition(PROF_FF);
}


void ExitFastForward() {
    assert(procTreeNode->isInFastForward());

    TimeVirt_CaptureClocks(true /*exiting ffwd*/);

    procTreeNode->exitFastForward();
    __sync_synchronize(); //make change globally visible

    //Re-instrument; VM/client lock are not needed
    if (zinfo->ffReinstrument) {
        PIN_RemoveInstrumentation();
    }
}



//Termination
volatile uint32_t perProcessEndFlag;

VOID SimEnd();

VOID CheckForTermination() {
    assert(zinfo->terminationConditionMet == false);
    if (zinfo->maxPhases && zinfo->numPhases >= zinfo->maxPhases) {
        zinfo->terminationConditionMet = true;
        info("Max phases reached (%ld)", zinfo->numPhases);
        return;
    }

    if (zinfo->maxMinInstrs) {
        uint64_t minInstrs = zinfo->cores[0]->getInstrs();
        for (uint32_t i = 1; i < zinfo->numCores; i++) {
            uint64_t coreInstrs = zinfo->cores[i]->getInstrs();
            if (coreInstrs < minInstrs && coreInstrs > 0) {
                minInstrs = coreInstrs;
            }
        }

        if (minInstrs >= zinfo->maxMinInstrs) {
            zinfo->terminationConditionMet = true;
            info("Max min instructions reached (%ld)", minInstrs);
            return;
        } 
    }

    if (zinfo->maxTotalInstrs) {
        uint64_t totalInstrs = 0;
        for (uint32_t i = 0; i < zinfo->numCores; i++) {
            totalInstrs += zinfo->cores[i]->getInstrs();
        }

        if (totalInstrs >= zinfo->maxTotalInstrs) {
            zinfo->terminationConditionMet = true;
            info("Max total (aggregate) instructions reached (%ld)", totalInstrs);
            return;
        }
    }

    if (zinfo->maxSimTimeNs) {
        uint64_t simNs = zinfo->profSimTime->count(PROF_BOUND) + zinfo->profSimTime->count(PROF_WEAVE);
        if (simNs >= zinfo->maxSimTimeNs) {
            zinfo->terminationConditionMet = true;
            info("Max simulation time reached (%ld ns)", simNs);
            return;
        }
    }

    if (zinfo->externalTermPending) {
        zinfo->terminationConditionMet = true;
        info("Terminating due to external notification");
        return;
    }
}

/* This is called by the scheduler at the end of a phase. At that point, zinfo->numPhases
 * has not incremented, so it denotes the END of the current phase
 */
VOID EndOfPhaseActions() {
    zinfo->profSimTime->transition(PROF_WEAVE);
    if (zinfo->globalPauseFlag) {
        info("Simulation entering global pause");
        while (zinfo->globalPauseFlag) usleep(20*1000);
        info("Global pause DONE");
    }

    // Done before tick() to avoid deadlock in most cases when entering synced ffwd (can we still deadlock with sleeping threads?)
    if (unlikely(zinfo->globalSyncedFFProcs)) {
        info("Simulation paused due to synced fast-forwarding");
        while (zinfo->globalSyncedFFProcs) usleep(20*1000);
        info("Synced fast-forwarding done, resuming simulation");
    }

    //Signal & wait for EOPThread
    futex_unlock(&zinfo->eopSignal);
    futex_lock_nospin(&zinfo->eopWait);

    zinfo->profSimTime->transition(PROF_BOUND);
}

// Run in process0, avoids multiproc/gm_malloc in ContentionSim/EventQueue, TD sim
void EOPThread(void* arg) {
    info("EOP control Thread TID %ld", syscall(SYS_gettid));
    while(true) {
        futex_lock_nospin(&zinfo->eopSignal);
        CheckForTermination();
        zinfo->eventQueue->tick();
        zinfo->tracingDriver->simPhase();
        futex_unlock(&zinfo->eopWait);
    }
}


uint32_t TakeBarrier(uint32_t tid, uint32_t cid) {
    uint32_t newCid = zinfo->sched->sync(procIdx, tid, cid);
    clearCid(tid); //this is after the sync for a hack needed to make EndOfPhase reliable
    setCid(tid, newCid);

    if (procTreeNode->isInFastForward()) {
        info("Thread %d entering fast-forward", tid);
        clearCid(tid);
        zinfo->sched->leave(procIdx, tid, newCid);
        SimThreadFini(tid);
        fPtrs[tid] = GetFFPtrs();
    }

    if (zinfo->terminationConditionMet) {
        info("Termination condition met, exiting");
        SimEnd(); //need to call this on a per-process basis...
    }

    return newCid;
}

/* ===================================================================== */

#if 0
static void PrintIp(THREADID tid, ADDRINT ip) {
    if (zinfo->globPhaseCycles > 1000000000L /*&& zinfo->globPhaseCycles < 1000030000L*/) { 
        info("[%d] %ld 0x%lx", tid, zinfo->globPhaseCycles, ip);
    }
}
#endif

VOID Instruction(INS ins) {
    //Uncomment to print an instruction trace
    //INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)PrintIp, IARG_THREAD_ID, IARG_REG_VALUE, REG_INST_PTR, IARG_END);

    if (!procTreeNode->isInFastForward() || !zinfo->ffReinstrument) {
        AFUNPTR LoadFuncPtr = (AFUNPTR) IndirectLoadSingle;
        AFUNPTR StoreFuncPtr = (AFUNPTR) IndirectStoreSingle;

        AFUNPTR PredLoadFuncPtr = (AFUNPTR) IndirectPredLoadSingle;
        AFUNPTR PredStoreFuncPtr = (AFUNPTR) IndirectPredStoreSingle;

        if (INS_IsMemoryRead(ins)) {
            if (!INS_IsPredicated(ins)) {
                INS_InsertCall(ins, IPOINT_BEFORE, LoadFuncPtr, IARG_FAST_ANALYSIS_CALL, IARG_THREAD_ID, IARG_MEMORYREAD_EA, IARG_END);
            } else {
                INS_InsertCall(ins, IPOINT_BEFORE, PredLoadFuncPtr, IARG_FAST_ANALYSIS_CALL, IARG_THREAD_ID, IARG_MEMORYREAD_EA, IARG_EXECUTING, IARG_END);
            }
        }            

        if (INS_HasMemoryRead2(ins)) {
            if (!INS_IsPredicated(ins)) {
                INS_InsertCall(ins, IPOINT_BEFORE, LoadFuncPtr, IARG_FAST_ANALYSIS_CALL, IARG_THREAD_ID, IARG_MEMORYREAD2_EA, IARG_END);
            } else {
                INS_InsertCall(ins, IPOINT_BEFORE, PredLoadFuncPtr, IARG_FAST_ANALYSIS_CALL, IARG_THREAD_ID, IARG_MEMORYREAD2_EA, IARG_EXECUTING, IARG_END);
            }
        }

        if (INS_IsMemoryWrite(ins)) {
            if (!INS_IsPredicated(ins)) {
                INS_InsertCall(ins, IPOINT_BEFORE,  StoreFuncPtr, IARG_FAST_ANALYSIS_CALL, IARG_THREAD_ID, IARG_MEMORYWRITE_EA, IARG_END);
            } else {
                INS_InsertCall(ins, IPOINT_BEFORE,  PredStoreFuncPtr, IARG_FAST_ANALYSIS_CALL, IARG_THREAD_ID, IARG_MEMORYWRITE_EA, IARG_EXECUTING, IARG_END);
            }
        }

        // Instrument only conditional branches
        if (INS_Category(ins) == XED_CATEGORY_COND_BR) {
            INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) IndirectRecordBranch, IARG_FAST_ANALYSIS_CALL, IARG_THREAD_ID,
                    IARG_INST_PTR, IARG_BRANCH_TAKEN, IARG_BRANCH_TARGET_ADDR, IARG_FALLTHROUGH_ADDR, IARG_END);
        }
    }

    //Intercept and process magic ops
    /* xchg %rcx, %rcx is our chosen magic op. It is effectively a NOP, but it
     * is never emitted by any x86 compiler, as they use other (recommended) nop
     * instructions or sequences.
     */
    if (INS_IsXchg(ins) && INS_OperandReg(ins, 0) == REG_RCX && INS_OperandReg(ins, 1) == REG_RCX) {
        //info("Instrumenting magic op");
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) HandleMagicOp, IARG_THREAD_ID, IARG_REG_VALUE, REG_ECX, IARG_END);
    }

    if (INS_Opcode(ins) == XED_ICLASS_CPUID) {
       INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) FakeCPUIDPre, IARG_THREAD_ID, IARG_REG_VALUE, REG_EAX, IARG_END);
       INS_InsertCall(ins, IPOINT_AFTER, (AFUNPTR) FakeCPUIDPost, IARG_THREAD_ID, IARG_REG_REFERENCE, REG_EAX,
               IARG_REG_REFERENCE, REG_EBX, IARG_REG_REFERENCE, REG_ECX, IARG_REG_REFERENCE, REG_EDX, IARG_END); 
    }

    if (INS_IsRDTSC(ins)) {
        //No pre; note that this also instruments RDTSCP
        INS_InsertCall(ins, IPOINT_AFTER, (AFUNPTR) FakeRDTSCPost, IARG_THREAD_ID, IARG_REG_REFERENCE, REG_EAX, IARG_REG_REFERENCE, REG_EDX, IARG_END);
    }
    
    //Must run for every instruction
    VdsoInstrument(ins);
}


VOID Trace(TRACE trace, VOID *v) {
    if (!procTreeNode->isInFastForward() || !zinfo->ffReinstrument) {
        // Visit every basic block in the trace
        for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
            BblInfo* bblInfo = Decoder::decodeBbl(bbl);
            BBL_InsertCall(bbl, IPOINT_BEFORE /*could do IPOINT_ANYWHERE if we redid load and store simulation in OOO*/, (AFUNPTR)IndirectBasicBlock, IARG_FAST_ANALYSIS_CALL,
                 IARG_THREAD_ID, IARG_ADDRINT, BBL_Address(bbl), IARG_PTR, bblInfo, IARG_END);
        }
    }

    //Instruction instrumentation now here to ensure proper ordering
    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
        for (INS ins = BBL_InsHead(bbl); INS_Valid(ins); ins = INS_Next(ins)) {
            Instruction(ins);
        }
    }
}

/***** vDSO instrumentation and patching code *****/

// Helper function to find section address
// adapted from http://outflux.net/aslr/aslr.c
struct Section {
    void* start;
    void* end;
};

static Section FindSection(const char* sec) {
    /* locate the vdso from the maps file */
    char buf[129];
    buf[128] = '\0';
    FILE * fp = fopen("/proc/self/maps","r");
    void* start = 0x0;
    void* end = 0x0;
    if (fp) {
        while (fgets(buf,128,fp)) {
            if (strstr(buf,sec)) {
                char * dash = strchr(buf,'-');
                if (dash) {
                    *dash='\0';
                    start = (void*)strtoul(buf,NULL,16);
                    end   = (void*)strtoul(dash+1,NULL,16);
                }
            }
        }
    }
    Section res = {start, end};

    //Uncomment to print maps
    //fseek(fp, 0, SEEK_SET);
    //while (fgets(buf,128,fp)) info("%s", buf);
    return res;
}

// Initialization code and global per-process data

enum VdsoFunc {VF_CLOCK_GETTIME, VF_GETTIMEOFDAY, VF_TIME, VF_GETCPU};

static std::unordered_map<ADDRINT, VdsoFunc> vdsoEntryMap;
static ADDRINT vdsoStart;
static ADDRINT vdsoEnd;

//Used to warn
static ADDRINT vsyscallStart;
static ADDRINT vsyscallEnd;
static bool vsyscallWarned = false;

void VdsoInsertFunc(IMG vi, const char* fName, VdsoFunc func) {
    ADDRINT baseAddr = IMG_LowAddress(vi);
    RTN rtn = RTN_FindByName(vi, fName);
    if (rtn == RTN_Invalid()) {
        warn("Did not find %s in vDSO", fName);
    } else {
        ADDRINT rtnAddr = RTN_Address(rtn) - baseAddr + vdsoStart;
        vdsoEntryMap[rtnAddr] = func;
    }
}

void VdsoInit() {
    Section vdso = FindSection("vdso");
    vdsoStart = (ADDRINT)vdso.start;
    vdsoEnd = (ADDRINT)vdso.end;

    if (!vdsoEnd) {
        warn("vDSO no found"); //this is non-fatal, but should not happen --- even static binaries get vDSO AFAIK
    }

    //Write it out
    std::stringstream file_ss;
    file_ss << zinfo->outputDir << "/vdso.dso." << procIdx;
    const char* file = file_ss.str().c_str();
    FILE* vf = fopen(file, "w");
    fwrite(vdso.start, 1, vdsoEnd-vdsoStart, vf);
    fclose(vf);

    //Load it and analyze it
    IMG vi = IMG_Open(file);
    if (!IMG_Valid(vi)) {
        panic("Loaded vDSO not valid");
    }

    VdsoInsertFunc(vi, "clock_gettime", VF_CLOCK_GETTIME);
    VdsoInsertFunc(vi, "__vdso_clock_gettime", VF_CLOCK_GETTIME);

    VdsoInsertFunc(vi, "gettimeofday", VF_GETTIMEOFDAY);
    VdsoInsertFunc(vi, "__vdso_gettimeofday", VF_GETTIMEOFDAY);

    VdsoInsertFunc(vi, "time", VF_TIME);
    VdsoInsertFunc(vi, "__vdso_time", VF_TIME);

    VdsoInsertFunc(vi, "getcpu", VF_GETCPU);
    VdsoInsertFunc(vi, "__vdso_getcpu", VF_GETCPU);

    info("vDSO info initialized");
    IMG_Close(vi);
    remove(file);

    Section vsyscall = FindSection("vsyscall");
    vsyscallStart = (ADDRINT)vsyscall.start;
    vsyscallEnd = (ADDRINT)vsyscall.end;
    if (!vsyscallEnd) warn("vsyscall page not found"); //this could actually happen in the future as vsyscall is phased out, consider skipping the warn
}

//Register hooks to intercept and virtualize time-related vsyscalls and vdso syscalls, as they do not show up as syscalls!
//NOTE: getcpu is also a VDSO syscall, but is not patched for now

// Per-thread VDSO data
struct VdsoPatchData {
    //Input arguments --- must save them because they are not caller-saved
    //Careful: REG is 32 bits; PIN_REGISTER, which is the actual type of the pointer, is 64 bits but opaque. We just use ADDRINT, it works
    ADDRINT arg0, arg1; 
    VdsoFunc func;
    uint32_t level; //if 0, invalid. Used for VDSO-internal calls
};
VdsoPatchData vdsoPatchData[MAX_THREADS];

// Analysis functions

VOID VdsoEntryPoint(THREADID tid, uint32_t func, ADDRINT arg0, ADDRINT arg1) {
    if (vdsoPatchData[tid].level) {
        //common, in Ubuntu 11.10 several vdso functions jump back to the callpoint
        //info("vDSO function (%d) called from vdso (%d), level %d, skipping", func, vdsoPatchData[tid].func, vdsoPatchData[tid].level);
    } else {
        vdsoPatchData[tid].arg0 = arg0;
        vdsoPatchData[tid].arg1 = arg1;
        vdsoPatchData[tid].func = (VdsoFunc)func;
        vdsoPatchData[tid].level++;
    }
}

VOID VdsoCallPoint(THREADID tid) {
    assert(vdsoPatchData[tid].level);
    vdsoPatchData[tid].level++;
    //info("vDSO internal callpoint, now level %d", vdsoPatchData[tid].level); //common
}

VOID VdsoRetPoint(THREADID tid, REG* raxPtr) {
    if (vdsoPatchData[tid].level == 0) {
        warn("vDSO return without matching call --- did we instrument all the functions?");
        return;
    }
    vdsoPatchData[tid].level--;
    if (vdsoPatchData[tid].level) {
        //info("vDSO return post level %d, skipping ret handling", vdsoPatchData[tid].level); //common
        return;
    }
    if (fPtrs[tid].type != FPTR_NOP) {
        //info("vDSO patching for func %d", vdsoPatchData[tid].func); //common
        ADDRINT arg0 = vdsoPatchData[tid].arg0;
        ADDRINT arg1 = vdsoPatchData[tid].arg1;
        switch(vdsoPatchData[tid].func) {
            case VF_CLOCK_GETTIME:
                patchClock_gettime(tid, arg0, arg1);
                break;
            case VF_GETTIMEOFDAY:
                patchGettimeofday(tid, arg0);
                break;
            case VF_TIME:
                patchTime(tid, raxPtr, arg0);
                break;
            case VF_GETCPU:
                patchGetcpu(tid, arg0, arg1);
                break;
            default:
                panic("vDSO garbled func %d", vdsoPatchData[tid].func);
        }
    }
}

// Instrumentation function, called for EVERY instruction
VOID VdsoInstrument(INS ins) {
    ADDRINT insAddr = INS_Address(ins);
    if (unlikely(insAddr >= vdsoStart && insAddr < vdsoEnd)) {
        if (vdsoEntryMap.find(insAddr) != vdsoEntryMap.end()) {
            VdsoFunc func = vdsoEntryMap[insAddr];
            INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) VdsoEntryPoint, IARG_THREAD_ID, IARG_UINT32, (uint32_t)func, IARG_REG_VALUE, REG_RDI, IARG_REG_VALUE, REG_RSI, IARG_END);
        } else if (INS_IsCall(ins)) {
            INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) VdsoCallPoint, IARG_THREAD_ID, IARG_END);
        } else if (INS_IsRet(ins)) {
            INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) VdsoRetPoint, IARG_THREAD_ID, IARG_REG_REFERENCE, REG_RAX /* return val */, IARG_END);        
        }
    }

    //Warn if 
    if (unlikely(insAddr >= vsyscallStart && insAddr < vsyscallEnd && !vsyscallWarned)) {
        warn("Instrumenting vsyscall page code --- this process executes vsyscalls, which zsim does not virtualize!");
        vsyscallWarned = true; //output once per process
    }

}

/* ===================================================================== */


bool activeThreads[MAX_THREADS]; //set in ThreadStart, reset in ThreadFini, we need this for exec() (see FollowChild)
bool inSyscall[MAX_THREADS]; //set in SyscallEnter, reset in SyscallExit, regardless of state. We MAY need this for ContextChange

void SimThreadStart(THREADID tid) {
    info("Thread %d starting", tid);
    if (tid > MAX_THREADS) panic("tid > MAX_THREADS");
    //zinfo->sched->start(procIdx, tid, procTreeNode->getMask());
    activeThreads[tid] = true;

    //Pinning
#if 0
    if (true) {
        uint32_t nprocs = sysconf(_SC_NPROCESSORS_ONLN);
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(tid % nprocs, &cpuset);
        //HMM, can we do this? I doubt it
        //int result = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
        //Since we're running multiprocess, this suffices for now:
        int result = sched_setaffinity(getpid(), sizeof(cpu_set_t), &cpuset);
        assert(result == 0);
    }
#endif

    //Initialize this thread's process-local data
    //fPtrs[tid] = joinPtrs; //delayed, MT-safe barrier join
    //clearCid(tid); //just in case, set an invalid cid
    //
    fPtrs[tid] = cores[tid]->GetFuncPtrs(); //back to normal pointers
}

VOID ThreadStart(THREADID tid, CONTEXT *ctxt, INT32 flags, VOID *v) {
    /* This should only fire for the first thread; I know this is a callback, everything is serialized etc;
     * that's the point, we block everything. It's here and not in main() because that way the auxiliary
     * threads can start.
     */
    if (procTreeNode->isInPause()) {
        futex_lock(&zinfo->pauseLocks[procIdx]); //initialize
        info("Pausing until notified");
        futex_lock(&zinfo->pauseLocks[procIdx]); //block
        procTreeNode->exitPause();
        info("Unpaused");
    }

    zinfo->sched->start(procIdx, tid, procTreeNode->getMask());
    uint32_t cid = zinfo->sched->join(procIdx, tid); //can block
    clearCid(tid);
    setCid(tid, cid);

    if (procTreeNode->isInFastForward()) {
        info("FF thread %d starting", tid);
        fPtrs[tid] = GetFFPtrs();
    } else if (zinfo->registerThreads) {
        info("Shadow thread %d starting", tid);
        fPtrs[tid] = nopPtrs;
    } else {
        //Start normal thread
        SimThreadStart(tid);
    }
}

VOID SimThreadFini(THREADID tid) {
    uint32_t cid = getCid(tid);
    clearCid(tid);//set an invalid cid, ours is property of the scheduler now!
    zinfo->sched->leave(procIdx, tid, cid); //exit syscall (SyscallEnter) already leaves (dsm: but not enabled!)
    zinfo->sched->finish(procIdx, tid);
    activeThreads[tid] = false;
    cids[tid] = UNINITIALIZED_CID; //clear this cid, it might get reused
}

VOID ThreadFini(THREADID tid, const CONTEXT *ctxt, INT32 flags, VOID *v) {
    //NOTE: Thread has no valid cid here!
    if (false /*dsm: Never with the changes for completely synced ffwd*/ && fPtrs[tid].type == FPTR_NOP) {
        info("Shadow/NOP thread %d finished", tid);
        return;
    } else {
        SimThreadFini(tid);
        info("Thread %d finished", tid);
    }
}

//Need to remove ourselves from running threads in case the syscall is blocking
VOID SyscallEnter(THREADID tid, CONTEXT *ctxt, SYSCALL_STANDARD std, VOID *v) { 
    bool isNopThread = fPtrs[tid].type == FPTR_NOP;
    syscallPatchPre(tid, ctxt, std, procTreeNode->getPatchRoot(), isNopThread);
    
    assert(!inSyscall[tid]); inSyscall[tid] = true;

    if (isNopThread) return;

    /* NOTE: It is possible that we take 2 syscalls back to back with any intervening instrumentation,
     * so we need to check. In that case, this is treated as a single syscall scheduling-wise (no second leave without join).
     */
    if (fPtrs[tid].type != FPTR_JOIN) {
        uint32_t cid = getCid(tid);
        clearCid(tid);//set an invalid cid, ours is property of the scheduler now!
        zinfo->sched->leave(procIdx, tid, cid);
        fPtrs[tid] = joinPtrs; //will join at the next instr point
        //info("SyscallEnter %d", tid);
    }
}

VOID SyscallExit(THREADID tid, CONTEXT *ctxt, SYSCALL_STANDARD std, VOID *v) {
    assert(inSyscall[tid]); inSyscall[tid] = false;

    PostPatchAction ppa = syscallPatchPost(tid, ctxt, std);
    if (ppa == PPA_USE_JOIN_PTRS) {
        fPtrs[tid] = joinPtrs;
    } else if (ppa == PPA_USE_NOP_PTRS) {
        fPtrs[tid] = nopPtrs;
    } else {
        assert(ppa == PPA_NOTHING);
    }

    //Avoid joining at all if we are in FF! 
    if (fPtrs[tid].type == FPTR_JOIN && procTreeNode->isInFastForward()) {
        assert(activeThreads[tid]);
        info("Thread %d entering fast-forward (from syscall exit)", tid);
        //We are not in the scheduler, and have no cid assigned. So, no need to leave()
        SimThreadFini(tid);
        fPtrs[tid] = GetFFPtrs();
    }


    if (zinfo->terminationConditionMet) {
        info("Caught termination condition on syscall exit, exiting");
        SimEnd();
    }
}

/* NOTE: We may screw up programs with frequent signals / SIG on syscall. If you see this warning and simulations misbehave,
 * it's time to do some testing to figure out how to make syscall post-patching work in this case.
 */
VOID ContextChange(THREADID tid, CONTEXT_CHANGE_REASON reason, const CONTEXT* from, CONTEXT* to, INT32 info, VOID* v) {
    const char* reasonStr = "?";
    switch (reason) {
        case CONTEXT_CHANGE_REASON_FATALSIGNAL:
            reasonStr = "FATAL_SIGNAL";
            break;
        case CONTEXT_CHANGE_REASON_SIGNAL:
            reasonStr = "SIGNAL";
            break;
        case CONTEXT_CHANGE_REASON_SIGRETURN:
            reasonStr = "SIGRETURN";
            break;
        case CONTEXT_CHANGE_REASON_APC:
            reasonStr = "APC";
            break;
        case CONTEXT_CHANGE_REASON_EXCEPTION:
            reasonStr = "EXCEPTION";
            break;
        case CONTEXT_CHANGE_REASON_CALLBACK:
            reasonStr = "CALLBACK";
            break;
    }
    
    warn("[%d] ContextChange, reason %s, inSyscall %d", tid, reasonStr, inSyscall[tid]);
    if (inSyscall[tid]) {
        SyscallExit(tid, to, SYSCALL_STANDARD_IA32E_LINUX, NULL);
    }
   
    if (reason == CONTEXT_CHANGE_REASON_FATALSIGNAL) {
        info("[%d] Fatal signal caught, finishing", tid);
        SimEnd();
    }

    //If this is an issue, we might need to call syscallexit on occasion. I very much doubt it
    //SyscallExit(tid, to, SYSCALL_STANDARD_IA32E_LINUX, NULL); //NOTE: For now it is safe to do spurious syscall exits, but careful...
}

/* Fork and exec instrumentation */

//For funky macro stuff
#define QUOTED_(x) #x
#define QUOTED(x) QUOTED_(x)

// Pre-exec
BOOL FollowChild(CHILD_PROCESS childProcess, VOID * userData) {
    //Finish all threads in this process w.r.t. the global scheduler
    uint32_t activeCount = 0;
    for (uint32_t i = 0; i < MAX_THREADS; i++) {
        if (activeThreads[i]) {
            activeCount++;
            zinfo->sched->finish(procIdx, i);
        }   
    }

    if (activeCount > 1) warn("exec() of a multithreaded process! (%d live threads)", activeCount);

    // You can always run process0 = { command = "ls"; startPaused = True; startFastForwarded = True; }; to avoid this
    if (procIdx == 0) panic("process0 cannot exec(), it spawns globally needed internal threads (scheduler and contention); run a dummy process0 instead!");

    //Set up Pin command
    //NOTE: perProcessDir may be active, we don't care much... run in the same dir as parent process
    //NOTE: we recycle our own procIdx on an exec, but fork() changed it so we need to update Pin's command line
    g_vector<g_string> args = zinfo->pinCmd->getPinCmdArgs(procIdx);
    uint32_t numArgs = args.size();
    const char* pinArgs[numArgs];
    for (uint32_t i = 0; i < numArgs; i++) pinArgs[i] = args[i].c_str();
    CHILD_PROCESS_SetPinCommandLine(childProcess, numArgs, pinArgs);
    
    //As a convenience, print the command we are going to execute
    const char* const* cArgv;
    int cArgc;
    CHILD_PROCESS_GetCommandLine(childProcess, &cArgc, &cArgv);

    std::string childCmd = cArgv[0];
    for (int i = 1; i < cArgc; i++) {
        childCmd += " ";
        childCmd += cArgv[i];
    }

    info("Following exec(): %s\n", childCmd.c_str());

    return true; //always follow
}

static ProcessTreeNode* forkedChildNode = NULL;

VOID BeforeFork(THREADID tid, const CONTEXT* ctxt, VOID * arg) {
    forkedChildNode = procTreeNode->getNextChild();
    info("Thread %d forking, child procIdx=%d", tid, forkedChildNode->getProcIdx());
}

VOID AfterForkInParent(THREADID tid, const CONTEXT* ctxt, VOID * arg) {
    forkedChildNode = NULL;
}

VOID AfterForkInChild(THREADID tid, const CONTEXT* ctxt, VOID * arg) {
    assert(forkedChildNode);
    procTreeNode = forkedChildNode;
    procIdx = procTreeNode->getProcIdx();
    bool wasNotStarted = procTreeNode->notifyStart();
    assert(wasNotStarted); //it's a fork, should be new
    procMask = ((uint64_t)procIdx) << (64-lineBits);

    char header[64];
    sprintf(header, "[S %dF] ", procIdx); //append an F to distinghish forked from fork/exec'd
    std::stringstream logfile_ss;
    logfile_ss << zinfo->outputDir << "/zsim.log." << procIdx;
    InitLog(header, KnobLogToFile.Value()? logfile_ss.str().c_str() : NULL);

    info("Forked child (tid %d/%d), PID %d, parent PID %d", tid, PIN_ThreadId(), PIN_GetPid(), getppid());
    
    //Initialize process-local per-thread state, even if ThreadStart does so later
    for (uint32_t i = 0; i < MAX_THREADS; i++) {
        fPtrs[i] = joinPtrs;
        cids[i] = UNINITIALIZED_CID;
        activeThreads[i] = false;
        inSyscall[i] = false;
        cores[i] = NULL;
    }

    ThreadStart(tid, NULL, 0, NULL);
}

/** Finalization **/

VOID Fini(int code, VOID * v) {
    info("Finished, code %d", code);
    //NOTE: In fini, it appears that info() and writes to stdout in general won't work; warn() and stderr still work fine.
    SimEnd();
}

VOID SimEnd() {
    if (__sync_bool_compare_and_swap(&perProcessEndFlag, 0, 1) == false) { //failed, note DEPENDS ON STRONG CAS
        while(true) { //sleep until thread that won exits for us
            struct timespec tm;
            tm.tv_sec = 1;
            tm.tv_nsec = 0;
            nanosleep(&tm, NULL);
        }
    }

    //at this point, we're in charge of exiting our whole process, but we still need to race for the stats

    //per-process
#ifdef BBL_PROFILING
    Decoder::dumpBblProfile();
#endif

    //global
    bool lastToFinish = procTreeNode->notifyEnd();
    (void) lastToFinish;

    //Done to preserve the scheduler and contention simulation internal threads; also, always write stats from process 0
    if (procIdx == 0) {
        while (zinfo->globalActiveProcs) usleep(20*1000);
        info("All processes done, terminating");

        zinfo->trigger = 20000;
        if (zinfo->periodicStatsBackend) zinfo->periodicStatsBackend->dump(false); //write last phase to periodic backend
        zinfo->statsBackend->dump(false);
        zinfo->eventualStatsBackend->dump(false);
        zinfo->compactStatsBackend->dump(false);

        zinfo->sched->notifyTermination();
    }

    exit(0);
}


// Magic ops interface
/* TODO: In the future, we might want to return values to the program.
 * This is definitely doable, but there is no use for it now.
 */
#define ZSIM_MAGIC_OP_ROI_BEGIN         (1025)
#define ZSIM_MAGIC_OP_ROI_END           (1026)
#define ZSIM_MAGIC_OP_REGISTER_THREAD   (1027)
#define ZSIM_MAGIC_OP_HEARTBEAT         (1028)

VOID HandleMagicOp(THREADID tid, ADDRINT op) {
    switch (op) {
        case ZSIM_MAGIC_OP_ROI_BEGIN:
            if (!zinfo->ignoreHooks) {
                //TODO: Test whether this is thread-safe
                futex_lock(&zinfo->ffLock);
                if (procTreeNode->isInFastForward()) {
                    info("ROI_BEGIN, exiting fast-forward");
                    ExitFastForward();
                } else {
                    warn("Ignoring ROI_BEGIN magic op, not in fast-forward");
                }
                futex_unlock(&zinfo->ffLock);
            }
            return;
        case ZSIM_MAGIC_OP_ROI_END:
            if (!zinfo->ignoreHooks) {
                //TODO: Test whether this is thread-safe
                futex_lock(&zinfo->ffLock);
                if (!procTreeNode->isInFastForward()) {
                    info("ROI_END, entering fast-forward");
                    EnterFastForward();
                } else {
                    warn("Ignoring ROI_END magic op, already in fast-forward");
                }
                futex_unlock(&zinfo->ffLock);
            }
            return;
        case ZSIM_MAGIC_OP_REGISTER_THREAD:
            if (!zinfo->registerThreads) {
                info("Thread %d: Treating REGISTER_THREAD magic op as NOP", tid);
            } else {
                if (fPtrs[tid].type == FPTR_NOP) {
                    SimThreadStart(tid);
                } else {
                    warn("Thread %d: Treating REGISTER_THREAD magic op as NOP, thread already registered", tid);
                }
            }
            return;
        case ZSIM_MAGIC_OP_HEARTBEAT:
            procTreeNode->heartbeat(); //heartbeats are per process for now
            return;
        default:
            panic("Thread %d issued unknown magic op %ld!", tid, op);
    }
}

//CPUIID faking
//These are from an Intel Nocona. Hopefully this is not used to get the number of cores, because it's a mess. I'm pretty sure sysconf and anyone with a bit of sense uses /proc instead
#if 0
const uint32_t cpuidVals[][4] = {
    {0x0000000a, 0x756e6547, 0x6c65746e, 0x49656e69}, //eax=0x0; idx 0
    {0x000006f7, 0x00040800, 0x0004e33d, 0xbfebfbff},
    {0x05b0b101, 0x005657f0, 0000000000, 0x2cb43049},
    {0000000000, 0000000000, 0000000000, 0000000000},
    {0000000000, 0000000000, 0000000000, 0000000000},
    {0x00000040, 0x00000040, 0x00000003, 0x00000020},
    {0x00000001, 0x00000002, 0x00000001, 0000000000},
    {0000000000, 0000000000, 0000000000, 0000000000},
    {0x00000400, 0000000000, 0000000000, 0000000000},
    {0000000000, 0000000000, 0000000000, 0000000000},
    {0x07280202, 0000000000, 0000000000, 0000000000}, //0xa
    {0x80000008, 0000000000, 0000000000, 0000000000}, //0x80000000; idx 11
    {0000000000, 0000000000, 0x00000001, 0x20100800},
    {0x65746e49, 0x2952286c, 0x6f655820, 0x2952286e},
    {0x55504320, 0x20202020, 0x20202020, 0x45202020},
    {0x35333335, 0x20402020, 0x30302e32, 0x007a4847},
    {0000000000, 0000000000, 0000000000, 0000000000},
    {0000000000, 0000000000, 0x10008040, 0000000000},
    {0000000000, 0000000000, 0000000000, 0000000000},
    {0x00003024, 0000000000, 0000000000, 0000000000}, //0x80000008
    {0x07280202, 0000000000, 0000000000, 0000000000}, //returned on anything else; idx 20
};
#else

// Westmere CPUID vals from more modern runs 
const uint32_t cpuidVals[][4] = {
    {0x0000000b, 0x756e6547, 0x6c65746e, 0x49656e69},
    {0x000206c2, 0x02200800, 0x029ee3ff, 0xbfebfbff},
    {0x55035a01, 0x00f0b0ff, 0000000000, 0x00ca0000},
    {0000000000, 0000000000, 0000000000, 0000000000},
    //{0x3c004121, 0x01c0003f, 0x0000003f, 0x00000000}, //eax 4, one fo the next in sequence
    {0000000000, 0000000000, 0000000000, 0000000000}, //eax 4, 1st in sequence
    {0x00000040, 0x00000040, 0x00000003, 0x00001120},
    {0x00000007, 0x00000002, 0x00000009, 0000000000},
    {0000000000, 0000000000, 0000000000, 0000000000},
    {0000000000, 0000000000, 0000000000, 0000000000},
    {0000000000, 0000000000, 0000000000, 0000000000},
    {0x07300403, 0x00000004, 0000000000, 0x00000603}, //0xa
    {0000000000, 0000000000, 0x00000004, 0x00000002}, //0xb (OOB ones ones give 0xb)
    {0x80000008, 0000000000, 0000000000, 0000000000}, //0x80000000
    {0000000000, 0000000000, 0x00000001, 0x2c100800},
    {0x65746e49, 0x2952286c, 0x6f655820, 0x2952286e},
    {0x55504320, 0x20202020, 0x20202020, 0x58202020},
    {0x30353635, 0x20402020, 0x37362e32, 0x007a4847},
    {0000000000, 0000000000, 0000000000, 0000000000},
    {0000000000, 0000000000, 0x01006040, 0000000000},
    {0000000000, 0000000000, 0000000000, 0x00000100 & 0x0}, //Fn8000_0007_EDX[8] === TSC invariant; FAKE, we don't want programs using TSC as a clock source
    {0x00003028, 0000000000, 0000000000, 0000000000}, //0x80000008
};
#endif

static uint32_t cpuidEax[MAX_THREADS];

VOID FakeCPUIDPre(THREADID tid, REG eax) {
    //info("%d precpuid", tid);
    cpuidEax[tid] = eax;
}

VOID FakeCPUIDPost(THREADID tid, ADDRINT* eax, ADDRINT* ebx, ADDRINT* ecx, ADDRINT* edx) {
    /* I give up. Virtualizing CPUID causes deadlock, and I don't know why;
     * we already virtualize NumCPUs and CPUCoreId, and ic is all westmeres
     * it seems, so screw it, do not overwrite CPUID */
    return;
    uint32_t idx;
    uint32_t inEax = cpuidEax[tid];
    info("[%d] postcpuid, inEax 0x%x, pre 0x%lx 0x%lx 0x%lx 0x%lx", tid, cpuidEax[tid], *eax, *ebx, *ecx, *edx);
#if 0 //Nocona
    if (inEax <= 0x0000000a) {
        idx = inEax;
    } else if (inEax >= 0x80000000 && inEax <= 0x80000008) {
        idx = inEax + 11 - 0x80000000;
    } else {
        idx = 20;
    }
#else //Westmere
    if (inEax <= 0xb) {
        idx = inEax;
    } else if (inEax >= 0x80000000 && inEax <= 0x80000008) {
        idx = inEax + 0xc - 0x80000000;
    } else {
        idx = 0xb;
    }

    //if (inEax == 4) return; //screw it, EAX=4 does a sequence. Just don't virtualize this, ever
#endif

    //Preserve high bits
    *(uint32_t*)eax = cpuidVals[idx][0];
    *(uint32_t*)ebx = cpuidVals[idx][1];
    *(uint32_t*)ecx = cpuidVals[idx][2];
    *(uint32_t*)edx = cpuidVals[idx][3];
    info("[%d] postcpuid, inEax 0x%x, post 0x%lx 0x%lx 0x%lx 0x%lx", tid, inEax, *eax, *ebx, *ecx, *edx);
}


//RDTSC faking

VOID FakeRDTSCPost(THREADID tid, REG* eax, REG* edx) {
    if (fPtrs[tid].type == FPTR_NOP) return; //avoid virtualizing NOP threads.
    
    uint32_t cid = getCid(tid);
    uint64_t curCycle = TimeVirt_GetPhaseRDTSC();
    if (cid < zinfo->numCores) {
        curCycle += zinfo->cores[cid]->getPhaseCycles();
    }

    uint32_t lo = (uint32_t)curCycle;
    uint32_t hi = (uint32_t)(curCycle >> 32);

    assert((((uint64_t)hi)<<32) + lo == curCycle);

    //uint64_t origTSC = (((uint64_t)*edx)<<32) + (uint32_t)*eax;
    //info("[t%d/c%d] Virtualizing RDTSC, pre = %x %x (%ld), post = %x %x (%ld)", tid, cid, *edx, *eax, origTSC, hi, lo, curCycle);

    *eax = (REG)lo;
    *edx = (REG)hi;
}

/* Fast-forward control */


// Helper class, enabled the FFControl thread to sync with the phase end code
class SyncEvent: public Event {
    private:
        lock_t arrivalLock;
        lock_t leaveLock;
    public:
        SyncEvent() : Event(0 /*one-shot*/) {
            futex_init(&arrivalLock);
            futex_init(&leaveLock);

            futex_lock(&arrivalLock);
            futex_lock(&leaveLock);
        }

        // Blocks until callback()
        void wait() {
            futex_lock(&arrivalLock);
        }

        // Unblocks thread that called wait(), blocks until signal() called
        // Resilient against callback-wait races (wait does not block if it's
        // called afteer callback)
        void callback() {
            futex_unlock(&arrivalLock);
            futex_lock(&leaveLock);
        }

        // Unblocks thread waiting in callback()
        void signal() {
            futex_unlock(&leaveLock);
        }
};

VOID FFThread(VOID* arg) {
    futex_lock(&zinfo->ffToggleLocks[procIdx]); //initialize
    info("FF control Thread TID %ld", syscall(SYS_gettid));

    while(true) {
        futex_lock(&zinfo->ffToggleLocks[procIdx]); //block ourselves until someone wakes us up with an unlock.
        
        futex_lock(&zinfo->ffLock);
        if (procTreeNode->isInFastForward()) {
            GetVmLock(); //like a callback. This disallows races on all syscall instrumentation, etc.
            info("Exiting fast forward");
            ExitFastForward();
            ReleaseVmLock();
        } else {
            SyncEvent* syncEv = new SyncEvent();
            zinfo->eventQueue->insert(syncEv); //will run on next phase
            info("Pending fast-forward entry, waiting for end of phase (%ld phases)", zinfo->numPhases);

            futex_unlock(&zinfo->ffLock);
            syncEv->wait();
            //At this point the thread thet triggered the end of phase is blocked inside of EndOfPhaseActions
            futex_lock(&zinfo->ffLock);
            if (!procTreeNode->isInFastForward()) {
                info("End of phase %ld, entering FF", zinfo->numPhases);
                EnterFastForward();
            } else {
                info("FF control thread called on end of phase, but someone else (program?) already entered ffwd");
            }
            syncEv->signal(); //unblock thread in EndOfPhaseActions
        } 
        futex_unlock(&zinfo->ffLock);
    }
    panic("Should not be reached!");
}



/* Internal Exception Handler */
//When firing a debugger was an easy affair, this was not an issue. Now it's not so easy, so let's try to at least capture the backtrace and print it out

//Use unlocked output, who knows where this happens.
static EXCEPT_HANDLING_RESULT InternalExceptionHandler(THREADID tid, EXCEPTION_INFO *pExceptInfo, PHYSICAL_CONTEXT *pPhysCtxt, VOID *) {
    
    fprintf(stderr, "%s[%d] Internal exception detected:\n", logHeader, tid);
    fprintf(stderr, "%s[%d]  Code: %d\n", logHeader, tid, PIN_GetExceptionCode(pExceptInfo));
    fprintf(stderr, "%s[%d]  Address: 0x%lx\n", logHeader, tid, PIN_GetExceptionAddress(pExceptInfo));
    fprintf(stderr, "%s[%d]  Description: %s\n", logHeader, tid, PIN_ExceptionToString(pExceptInfo).c_str());

    ADDRINT faultyAccessAddr;
    if (PIN_GetFaultyAccessAddress(pExceptInfo, &faultyAccessAddr)) {
        const char* faultyAccessStr = "";
        FAULTY_ACCESS_TYPE fat = PIN_GetFaultyAccessType (pExceptInfo);
        if (fat == FAULTY_ACCESS_READ) faultyAccessStr = "READ ";
        else if (fat == FAULTY_ACCESS_WRITE) faultyAccessStr = "WRITE ";
        else if (fat == FAULTY_ACCESS_EXECUTE) faultyAccessStr = "EXECUTE ";

        fprintf(stderr, "%s[%d]  Caused by invalid %saccess to address 0x%lx\n", logHeader, tid, faultyAccessStr, faultyAccessAddr);
    }

    void* array[40];
    size_t size = backtrace(array, 40);
    char** strings = backtrace_symbols(array, size);
    fprintf(stderr, "%s[%d] Backtrace (%ld/%d max frames)\n", logHeader, tid, size, 40);
    for (uint32_t i = 0; i < size; i++) {
        fprintf(stderr, "%s[%d]  %s\n", logHeader, tid, strings[i]);
    }
    fflush(stderr);
    
    return EHR_CONTINUE_SEARCH; //we never solve anything at all :P
}

/* ===================================================================== */

int main(int argc, char *argv[]) {
    PIN_InitSymbols();
    if(PIN_Init(argc,argv)) return Usage();

    //Register an internal exception handler (ASAP, to catch segfaults in init)
    PIN_AddInternalExceptionHandler(InternalExceptionHandler, NULL);

    procIdx = KnobProcIdx.Value();
    char header[64];
    sprintf(header, "[S %d] ", procIdx);
    std::stringstream logfile_ss;
    logfile_ss << KnobOutputDir.Value() << "/zsim.log." << procIdx;
    InitLog(header, KnobLogToFile.Value()? logfile_ss.str().c_str() : NULL);

    //If parent dies, kill us
    //This avoids leaving strays running in any circumstances, but may be too heavy-handed with arbitrary process hierarchies.
    //If you ever need this disabled, sim.pinOptions = "-injection child" does the trick
    if (prctl(PR_SET_PDEATHSIG, 9 /*SIGKILL*/) != 0) {
        panic("prctl() failed");
    }

    info("Started instance");

    //Decrease priority to avoid starving system processes (e.g. gluster)
    //setpriority(PRIO_PROCESS, getpid(), 10);
    //info("setpriority, new prio %d", getpriority(PRIO_PROCESS, getpid()));

    gm_attach(KnobShmid.Value());

    bool masterProcess = false;
    if (procIdx == 0 && !gm_isready()) { //process 0 can exec() without fork()ing first, so we must check gm_isready() to ensure we don't initialize twice
        masterProcess = true;
        SimInit(KnobConfigFile.Value().c_str(), KnobOutputDir.Value().c_str(), KnobShmid.Value());
    } else {
        while(!gm_isready()) sched_yield(); //wait till proc idx 0 initializes everything; sched_yield avoids livelock with lots of processes
        zinfo = (GlobSimInfo*) gm_get_glob_ptr();
    }

    //End of phase thread
    if (masterProcess) {
        PIN_SpawnInternalThread(EOPThread, NULL, 1024*1024, NULL);
    }

    //LibzsimAddrs sanity check: Ensure that they match across processes
    struct LibInfo libzsimAddrs;
    getLibzsimAddrs(&libzsimAddrs);
    if (memcmp(&libzsimAddrs, &zinfo->libzsimAddrs, sizeof(libzsimAddrs)) != 0) {
        panic("libzsim.so address mismatch! text: %p != %p. Perform loader injection to homogenize offsets!", libzsimAddrs.textAddr, zinfo->libzsimAddrs.textAddr);
    }

    //Attach to debugger if needed (mas ter process does so in SimInit, to be able to debug initialization)
    //NOTE: Pin fails to follow exec()'s when gdb is attached. The simplest way to avoid it is to kill the debugger manually before an exec(). If this is common, we could automate it
    if (!masterProcess && zinfo->attachDebugger) {
        notifyHarnessForDebugger(zinfo->harnessPid);
    }

    assert((uint32_t)procIdx < zinfo->numProcs);
    procTreeNode = zinfo->procArray[procIdx];
    if (!masterProcess) procTreeNode->notifyStart(); //masterProcess notifyStart is called in init() to avoid races
    assert(procTreeNode->getProcIdx() == (uint32_t)procIdx); //must be consistent

    trace(Process, "SHM'd global segment, starting");

    assert(zinfo->phaseLength > 0);
    assert(zinfo->maxPhases >= 0);
    assert(zinfo->statsPhaseInterval >= 0);

    perProcessEndFlag = 0;

    lineBits = 0;
    int tmp = zinfo->lineSize;
    while (tmp >>= 1) lineBits++; //log`2

    procMask = ((uint64_t)procIdx) << (64-lineBits);

    //Initialize process-local per-thread state, even if ThreadStart does so later
    for (uint32_t i = 0; i < MAX_THREADS; i++) {
        fPtrs[i] = joinPtrs;
        cids[i] = UNINITIALIZED_CID;
    }

    info("Started process, PID %d", getpid()); //NOTE: external scripts expect this line, please do not change without checking first

    //Unless things change substantially, keep this disabled; it causes higher imbalance and doesn't solve large system time with lots of processes.
    //Affinity testing code
    /*cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(procIdx % 8, &cpuset);
    int result = sched_setaffinity(getpid(), sizeof(cpu_set_t), &cpuset);
    info("Affinity result %d", result);*/

    info("procMask: 0x%lx", procMask);

    TimeVirt_CaptureClocks(false);
    FFIInit();

    //Register instrumentation
    TRACE_AddInstrumentFunction(Trace, 0);
    VdsoInit(); //initialized vDSO patching information (e.g., where all the possible vDSO entry points are)

    PIN_AddThreadStartFunction(ThreadStart, 0);
    PIN_AddThreadFiniFunction(ThreadFini, 0);

    //dsm: For single-threaded sims, make syscalls completely transparent (no join-leave, so clocks never lose sync)
    //PIN_AddSyscallEntryFunction(SyscallEnter, 0);
    //PIN_AddSyscallExitFunction(SyscallExit, 0);
    //PIN_AddContextChangeFunction(ContextChange, 0);

    PIN_AddFiniFunction(Fini, 0);

    //Follow exec and fork
    PIN_AddFollowChildProcessFunction(FollowChild, 0);
    PIN_AddForkFunction(FPOINT_BEFORE, BeforeFork, 0);
    PIN_AddForkFunction(FPOINT_AFTER_IN_PARENT, AfterForkInParent, 0);
    PIN_AddForkFunction(FPOINT_AFTER_IN_CHILD, AfterForkInChild, 0);

    //FFwd control
    //OK, screw it. Launch this on a separate thread, and forget about signals... the caller will set a shared memory var. PIN is hopeless with signal instrumentation on multithreaded processes!
    PIN_SpawnInternalThread(FFThread, NULL, 64*1024, NULL);

    //Never returns
    PIN_StartProgram();
    return 0;
}

