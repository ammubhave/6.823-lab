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

#ifndef __TRACING_CORE_H__
#define __TRACING_CORE_H__

//A core model that runs at IPC=1, gathering bbl and ld/st traces. At the end of the phase, it takes in an updated cycle count to 

#include "core.h"
#include "memory_hierarchy.h"
#include "cache.h"
#include "pad.h"

enum TraceType {TR_LD, TR_ST, TR_BBL};

class TracingCore;

struct TraceEntry {
    //TracingCore *core;
    Address addr;
    TraceType type;
    //Only valid for BBLs 
    uint16_t bytes;
    uint16_t instrs;

    // for PrioQueue
    //struct TraceEntry *next;
};

template <typename T>
class CircIter {
    public:
        CircIter(uint64_t _bufMask, T* _buf, uint64_t _p) : bufMask(_bufMask), buf(_buf), p(_p) { }
        inline CircIter& operator++() { ++p; return *this; }
        inline bool operator==(const CircIter& rhs) { return p == rhs.p && buf == rhs.buf; }
        inline bool operator!=(const CircIter& rhs) { return p != rhs.p || buf != rhs.buf; }
        inline T& operator*() { return buf[p & bufMask]; }
    private:
        const uint64_t bufMask;
        T* buf;
        uint64_t p;
};

template <typename T>
class CircBuffer : public GlobAlloc {
    private:
        T* buf;
        uint64_t bufMask;
        uint32_t bufSz; //must be power of 2
        uint64_t curHead;
        uint64_t curTail;
    public:
        CircBuffer(uint32_t sz) {
            //Find next power of 2 (32-bit bithack)
            sz--;
            sz |= sz >> 1;
            sz |= sz >> 2;
            sz |= sz >> 4;
            sz |= sz >> 8;
            sz |= sz >> 16;
            sz++;

            bufSz = sz;
            bufMask = ((uint64_t)(bufSz-1));
            buf = gm_calloc<T>(bufSz);
            curHead = 0;
            curTail = 0;
            //info("%lx %d", bufMask, bufSz);
        }

        bool push(const T& elem) {
            //info("push %ld %ld", curHead, curTail);
            if (!full()) {
                buf[curTail & bufMask] = elem;
                curTail++;
                return true;
            } else {
                assert(false); //no push-on-full currently allowed
                return false;
            }
        }

        const T& peek() const { //warn: no empty checks
            return buf[curHead & bufMask];
        }

        CircIter<T> begin() {
            return CircIter<T>(bufMask, buf, curHead);
        }

        CircIter<T> end() {
            return CircIter<T>(bufMask, buf, curTail);
        }

        bool pop(T& res) {
            //info("pop %ld %ld", curHead, curTail);
            if (!empty()) {
                res = buf[curHead & bufMask];
                curHead++;
                return true;
            } else {
                return false;
            }
        }

        inline uint32_t elems() const {return curTail - curHead;}
        inline uint32_t size() const {return bufSz;}
        inline bool empty() const {return curTail == curHead;}
        inline bool full() const {return (curTail - curHead) == bufSz;}
};

class TracingCore : public Core {
    protected:
        uint64_t instrs;
        uint64_t cycles;
        //uint64_t phaseEndCycle; //next stopping point

        CircBuffer<TraceEntry> tBuf;

        //Caches
        Memory *l1i;
        Memory *l1d;

    public:
        TracingCore(g_string& _name, Memory *l1i, Memory *l1d);
        void initStats(AggregateStat* parentStat);
        
        uint64_t getInstrs() const {return instrs;}
        uint64_t getPhaseCycles() const;

        void contextSwitch(int32_t gid);

        InstrFuncPtrs GetFuncPtrs();

        //TracingDriver interface
        CircBuffer<TraceEntry>& getTraceBuf() {return tBuf;}
        void addCycles(uint64_t c) {cycles += c;}
        void addInstrs(uint64_t i) {instrs += i;}
 
        Memory *getL1i();
        Memory *getL1d();

    protected:
        inline void bbl(Address bblAddr, BblInfo* bblInstrs);
        inline void load(Address addr);
        inline void store(Address addr);

        static void LoadFunc(THREADID tid, ADDRINT addr);
        static void StoreFunc(THREADID tid, ADDRINT addr);
        static void BblFunc(THREADID tid, ADDRINT bblAddr, BblInfo* bblInfo);
        static void PredLoadFunc(THREADID tid, ADDRINT addr, BOOL pred);
        static void PredStoreFunc(THREADID tid, ADDRINT addr, BOOL pred);

        static void BranchFunc(THREADID, ADDRINT, BOOL, ADDRINT, ADDRINT) {};
}  __attribute__ ((aligned (CACHE_LINE_BYTES))); //This needs to take up a whole cache line, or false sharing will be extremely frequent

#endif /*__TRACING_CORE_H__*/

