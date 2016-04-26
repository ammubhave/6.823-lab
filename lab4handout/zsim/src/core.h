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

#ifndef __CORE_H__
#define __CORE_H__

#include <stdint.h>

#include "g_std/g_string.h"
#include "stats.h"
#include "decoder.h"

struct BblInfo {
    uint32_t instrs;
    uint32_t bytes;
};

/* Analysis function pointer struct
 * As an artifact of having a shared code cache, we need these to be the same for different core types.
 */
struct InstrFuncPtrs {
    void (*loadPtr)      (THREADID, ADDRINT);
    void (*storePtr)     (THREADID, ADDRINT);
    void (*bblPtr)       (THREADID, ADDRINT, BblInfo*);
    void (*branchPtr)    (THREADID, ADDRINT, BOOL, ADDRINT, ADDRINT);
    // Same as load/store functions, but last arg indicated whether op is executing
    void (*predLoadPtr)  (THREADID, ADDRINT, BOOL);
    void (*predStorePtr) (THREADID, ADDRINT, BOOL);
    uint64_t type;
    uint64_t pad[1];
    //NOTE: By having the struct be a power of 2 bytes, indirect calls are simpler (w/ gcc 4.4 -O3, 6->5 instructions, and those instructions are simpler)
};


//TODO: Switch type to an enum by using sizeof macros...
#define FPTR_ANALYSIS (0L)
#define FPTR_JOIN (1L)
#define FPTR_NOP (2L)

//Generic core class

class Core : public GlobAlloc {
    protected:
        g_string name;

    public:
        Core(g_string& _name) : name(_name) {}
        
        virtual uint64_t getInstrs() const = 0; // typically used to find out termination conditions or dumps
        virtual uint64_t getPhaseCycles() const = 0; // used by RDTSC faking --- we need to know how far along we are in the phase, but not the total number of phases

        virtual void initStats(AggregateStat* parentStat) = 0;
        virtual void contextSwitch(int32_t gid) = 0; //gid == -1 means descheduled, otherwise this is the new gid

        //Called by scheduler on every leave and join action, before barrier methods are called
        virtual void leave() {};
        virtual void join() {};

        virtual InstrFuncPtrs GetFuncPtrs() = 0;
};

#endif /*__CORE_H__*/

