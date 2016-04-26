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

#ifndef __PROFILE_STATS_H__
#define __PROFILE_STATS_H__

/* Stats used to profile the simulator */

#include <time.h>
#include "stats.h"

//Helper function
inline uint64_t getNs() {
    struct timespec ts;
    //guaranteed synchronized across processors, averages 20ns/call on Ubuntu 12.04... Linux hrtimers have gotten really good! In comparison, rdtsc is 9ns. 
    clock_gettime(CLOCK_REALTIME, &ts);
    return 1000000000L*ts.tv_sec + ts.tv_nsec;
}

/* Implements a single stopwatch-style cumulative clock. Useful to profile isolated events.
 * get() accounts for current interval if clock is running.
 */
class ClockStat : public ScalarStat {
    private:
        uint64_t startNs;
        uint64_t totalNs;

    public:
        ClockStat() : ScalarStat(), startNs(0), totalNs(0) {};

        void start() {
            assert(!startNs);
            startNs = getNs();
        }

        void end() {
            assert(startNs);
            uint64_t endNs = getNs();
            assert(endNs >= startNs)
            totalNs += (endNs - startNs);
            startNs = 0;
        }

        uint64_t get() const {
            return totalNs + (startNs? (getNs() - startNs) : 0);
        }
};

/* Implements multi-state time profiling. Always starts at state 0.
 * Using this with an enum will help retain your sanity. Does not stop,
 * so just transition to a dummy state if you want to stop profiling.
 * count() accounts for partial time in current state; count() is used
 * because we extend VectorCounter (TODO: we should have a VectorStat)
 */
class TimeBreakdownStat : public VectorCounter {
    private:
        uint32_t curState;
        uint64_t startNs;

    public:

        TimeBreakdownStat() : VectorCounter() {}

        virtual void init(const char* name, const char* desc, uint32_t size) {
            VectorCounter::init(name, desc, size);
            curState = 0;
            startNs = getNs();
        }
        
        //I need to define this even though it is completely unnecessary, but only if I override init. gcc bug or C++ oddity?
        virtual void init(const char* name, const char* desc, uint32_t size, const char** names) {
            VectorCounter::init(name, desc, size, names); //will call our init(name, desc, size)
        }

        void transition(uint32_t newState) {
            assert(curState < size());
            assert(newState < size());

            uint64_t curNs = getNs();
            assert(curNs >= startNs);

            inc(curState, curNs - startNs);
            //info("%d: %ld / %ld", curState, curNs - startNs, VectorCounter::count(curState));
            curState = newState;
            startNs = curNs;
        }

        inline virtual uint64_t count(uint32_t idx) const {
            uint64_t partial = VectorCounter::count(idx);
            return partial + ((idx == curState)? (getNs() - startNs) : 0);
        }
};

#endif /*__PROFILE_STATS_H__*/
