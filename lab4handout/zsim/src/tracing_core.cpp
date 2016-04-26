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

#include "tracing_core.h"
#include "zsim.h"


TracingCore::TracingCore(g_string& _name, Memory *_l1i, Memory *_l1d) : Core(_name), instrs(0), cycles(0), tBuf(zinfo->phaseLength*8 /*at least 2 full phases*/), l1i(_l1i), l1d(_l1d) {
}

void TracingCore::initStats(AggregateStat* parentStat) {
    AggregateStat* coreStat = new AggregateStat();
    coreStat->init(name.c_str(), "Core stats");
    ProxyStat* cyclesStat = new ProxyStat();
    cyclesStat->init("cycles", "Simulated cycles", &cycles);
    ProxyStat* instrsStat = new ProxyStat();
    instrsStat->init("instrs", "Simulated instructions", &instrs);
    coreStat->append(cyclesStat);
    coreStat->append(instrsStat);
    parentStat->append(coreStat);
}

uint64_t TracingCore::getPhaseCycles() const {
    return cycles % zinfo->phaseLength;
}


void TracingCore::load(Address addr) {
    TraceEntry tr = {(addr >> lineBits) | procMask, TR_LD, 0, 0};
    tBuf.push(tr);
}

void TracingCore::store(Address addr) {
    TraceEntry tr = {(addr >> lineBits) | procMask, TR_ST, 0, 0};
    tBuf.push(tr);
}

void TracingCore::bbl(Address bblAddr, BblInfo* bblInfo) {
    //info("BBL %s %p", name.c_str(), bblInfo);
    //info("%d %d", bblInfo->instrs, bblInfo->bytes);
    //instrs += bblInfo->instrs;
    //cycles += bblInfo->instrs;

    TraceEntry tr = {(bblAddr >> lineBits) | procMask, TR_BBL, (uint16_t)bblInfo->bytes, (uint16_t)bblInfo->instrs};
    tBuf.push(tr);
}

void TracingCore::contextSwitch(int32_t gid) {}

//Static class functions: Function pointers and trampolines

InstrFuncPtrs TracingCore::GetFuncPtrs() {
    return {LoadFunc, StoreFunc, BblFunc, BranchFunc, PredLoadFunc, PredStoreFunc, FPTR_ANALYSIS, {0}};
}

void TracingCore::LoadFunc(THREADID tid, ADDRINT addr) {
    ((TracingCore*)cores[tid])->load(addr);
}

void TracingCore::StoreFunc(THREADID tid, ADDRINT addr) {
    ((TracingCore*)cores[tid])->store(addr);
}

void TracingCore::PredLoadFunc(THREADID tid, ADDRINT addr, BOOL pred) {
    if (pred) ((TracingCore*)cores[tid])->load(addr);
}

void TracingCore::PredStoreFunc(THREADID tid, ADDRINT addr, BOOL pred) {
    if (pred) ((TracingCore*)cores[tid])->store(addr);
}

void TracingCore::BblFunc(THREADID tid, ADDRINT bblAddr, BblInfo* bblInfo) {
    TracingCore* core = ((TracingCore*)cores[tid]);
    core->bbl(bblAddr, bblInfo);
    CircBuffer<TraceEntry>& tBuf = core->getTraceBuf();
    while (tBuf.elems() > tBuf.size()/2) {
        uint32_t cid = getCid(tid);
        //NOTE: TakeBarrier may take ownership of the core, and so it will be used by some other thread. If TakeBarrier context-switches us,
        //the *only* safe option is to return inmmediately after we detect this, or we can race and corrupt core state. If newCid == cid,
        //we're not at risk of racing, even if we were switched out and then switched in.
        uint32_t newCid = TakeBarrier(tid, cid);
        if (newCid != cid) break; /*context-switch*/
    }
}

Memory *TracingCore::getL1i() { return l1i; }

Memory *TracingCore::getL1d() { return l1d; }


