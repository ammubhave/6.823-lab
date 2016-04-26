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

/* Statistics facilities
 * Author: Daniel Sanchez
 * Date: Aug 2010
 *
 * There are four basic types of stats:
 * - Counter: A plain single counter.
 * - VectorCounter: A fixed-size vector of logically related counters. Each
 *   vector element may be unnamed or named (useful when enum-indexed vectors).
 * - Histogram: A GEMS-style histogram, intended to profile a distribution.
 *   It has a fixed amount of buckets, and buckets are resized as samples
 *   are added, making profiling increasingly coarser but keeping storage
 *   space constant. Unlike GEMS-style stats, though, at some configurable
 *   point part of the array starts growing logarithmically, to capture
 *   outliers without hurting accuracy of most samples.
 * - ProxyStat takes a function pointer uint64_t(*)(void) at initialization,
 *   and calls it to get its value. It is used for cases where a stat can't
 *   be stored as a counter (e.g. aggregates, RDTSC, performance counters,...)
 *   or where we have values we want to output just as stats, but would not
 *   like to treat as raw counters because e.g. they have a different type, 
 *   or for efficiency reasons (e.g. the per-thread phase cycles count is
 *   updated on every BBL, and may be an uint32_t)
 *
 * Groups of stats are contained in aggregates (AggregateStat), representing
 * a collection of stats. At initialization time, all stats are registered
 * with an aggregate, forming a tree of stats. After all stats are
 * initialized, the tree of stats is made immutable; no new stats can be
 * created and output at runtime.
 *
 * These facilities are created with three goals in mind:
 * 1) Allow stats to be independent of stats output: Simulator code is only
 *    concerned with creating, naming, describing and updating a hierarchy of
 *    stats. We can then use a variety of *stats backends* to traverse and
 *    output the stats, either periodically or at specific events.
 * 2) High-performance stats: Updating counters should be as fast as updating raw
 *    integers. Counters are objects though, so they entail some space overhead.
 * 3) Allow fixed-size stats output: The stat types supported are all fixed-size,
 *    and stats cannot be created after initialization. This allows fixed-size records,
 *    making periodic stats much easier to parse and **iterate over** (e.g. we can
 *    parse 1% of the samples for a high-level graph without bringing the whole stats
 *    file from disk, then zoom in on a specific portion, etc.).
 *
 * This design was definitely influenced by the M5 stats facilities, however,
 * it is significantly simpler, doesn't use templates or has formula support,
 * and has an emphasis on fixed-size records for periodic stats.
 */

#ifndef __STATS_H__
#define __STATS_H__

/* TODO: I want these to be POD types, but polymorphism (needed by dynamic_cast) probably disables it. Dang. */

#include "log.h"
#include <stdint.h>
#include <string.h>
#include "g_std/g_vector.h"


class Stat : public GlobAlloc {
    protected:
        const char* _name;
        const char* _desc;
    public:
        Stat() : _name(NULL), _desc(NULL) {}

        virtual ~Stat() {}

        const char* name() const {
            assert(_name);
            return _name;
        }

        const char* desc() const {
            assert(_desc);
            return _desc;
        }

    protected:
        virtual void initStat(const char* name, const char* desc) {
            assert(name);
            assert(desc);
            assert(!_name);
            assert(!_desc);
            _name = name;
            _desc = desc;
        }
};

class AggregateStat : public Stat {
    private:
        g_vector<Stat*> _children;
        bool _isMutable;

        bool _isRegular;
    public:
        /* An aggregate stat is regular if all its children are 1) aggregate and 2) of the same type (e.g. all the threads).
         * This lets us express all the subtypes of instances of a common datatype, and this collection as an array. It is
         * useful with HDF5, where we would otherwise be forced to have huge compund datatypes, which HDF5 can't do after some
         * point.
         */
        AggregateStat(bool isRegular = false) : Stat(), _isMutable(true), _isRegular(isRegular) {};

        void init(const char* name, const char* desc) {
            assert(_isMutable);
            initStat(name, desc);
        }

        //Returns true if it is a non-empty type, false otherwise. Empty types are culled by the parent.
        bool makeImmutable() {
            assert(_isMutable);
            assert(_name != NULL); //Should have been initialized
            _isMutable = false;
            g_vector<Stat*>::iterator it;
            g_vector<Stat*> newChildren;
            for (it = _children.begin(); it != _children.end(); it++) {
                Stat* s = *it;
                AggregateStat* as = dynamic_cast<AggregateStat*>(s);
                if (as) {
                    bool emptyChild = as->makeImmutable();
                    if (!emptyChild) newChildren.push_back(s);
                } else {
                    newChildren.push_back(s);
                }
            }
            _children = newChildren;
            return _children.size() == 0;
        }

        void append(Stat* child) {
            assert(_isMutable);
            _children.push_back(child);
        }

        uint32_t size() const {
            assert(!_isMutable);
            return _children.size();
        }

        bool isRegular() const {
            return _isRegular;
        }

        Stat* get(uint32_t idx) const {
            assert(!_isMutable);
            return _children[idx];
        }
};

class Counter : public Stat {
    private:
        uint64_t _count;
    public:
        Counter() : Stat(), _count(0) {};
    
        void init(const char* name, const char* desc) {
            initStat(name, desc);
            _count = 0;
        }

        inline void inc(uint64_t delta) {
            _count += delta;
        }

        inline void inc() {
            _count++;
        }

        inline void atomicInc(uint64_t delta) {
            __sync_fetch_and_add(&_count, delta);
        }

        inline void atomicInc() {
            __sync_fetch_and_add(&_count, 1);
        }

        inline uint64_t count() const {
            return _count;
        }
};

class VectorCounter : public Stat {
    private:
        g_vector<uint64_t> _counters;
        const char** _counterNames;
    public:
        VectorCounter() : Stat(), _counterNames(NULL) {}

        /* Without counter names */
        virtual void init(const char* name, const char* desc, uint32_t size) {
            initStat(name, desc);
            assert(size > 0);
            _counters.resize(size);
            for (uint32_t i = 0; i < size; i++) _counters[i] = 0;
            _counterNames = NULL;
        }

        /* With counter names */
        virtual void init(const char* name, const char* desc, uint32_t size, const char** counterNames) {
            init(name, desc, size);
            assert(counterNames);
            _counterNames = gm_dup<const char*>(counterNames, size);
        }

        inline void inc(uint32_t idx, uint64_t value) {
            _counters[idx] += value;
        }

        inline void inc(uint32_t idx) {
             _counters[idx]++;
        }

        inline virtual uint64_t count(uint32_t idx) const {
            return _counters[idx];
        }

        inline uint64_t size() const {
            return _counters.size();
        }

        inline bool hasCounterNames() {
            return (_counterNames != NULL);
        }

        inline const char* counterName(uint32_t idx) const {
            return (_counterNames == NULL)? NULL : _counterNames[idx];
        }
};


class Histogram : public Stat {
    //TBD
};

class ProxyStat : public Stat {
    private:
        uint64_t* _statPtr;
    public:
        ProxyStat() : Stat(), _statPtr(NULL) {};
    
        void init(const char* name, const char* desc, uint64_t* ptr) {
            initStat(name, desc);
            _statPtr = ptr;
        }

        inline uint64_t stat() const {
            assert(_statPtr); //TBD we may want to make this work only with volatiles...
            return *_statPtr;
        }
};


class ProxyFuncStat : public Stat {
    private:
        uint64_t (*_func)();
    public:
        ProxyFuncStat() : Stat(), _func(NULL) {};
    
        void init(const char* name, const char* desc, uint64_t (*func)()) {
            initStat(name, desc);
            _func = func;
        }

        //Hmmm, this is a const function but the function pointer we use is not necessarily const. Oh well, it works.
        inline uint64_t stat() const {
            assert(_func);
            return _func();
        }
};


class ProxyVectorStat : public Stat {
    private:
        uint32_t _size;
        uint64_t* _counters;
        const char** _counterNames;
    public:
        ProxyVectorStat() : Stat(), _size(0), _counters(NULL), _counterNames(NULL) {}

        /* Without counter names */
        void init(const char* name, const char* desc, uint64_t* countersPtr, uint32_t size) {
            initStat(name, desc);
            assert(size > 0);
            assert(countersPtr);
            _counters = countersPtr;
            _size = size;
        }

        /* With counter names */
        void init(const char* name, const char* desc, uint64_t* countersPtr, uint32_t size, const char** counterNames) {
            init(name, desc, countersPtr, size);
            assert(counterNames);
            _counterNames = counterNames;
        }

        inline uint64_t count(uint32_t idx) const {
            assert(idx < _size);
            return _counters[idx];
        }

        inline uint64_t size() const {
            return _size;
        }

        inline bool hasCounterNames() {
            return (_counterNames != NULL);
        }

        inline const char* counterName(uint32_t idx) const {
            return (_counterNames == NULL)? NULL : _counterNames[idx];
        }
};

/* 
 * General scalar class
 * FIXME: All other scalar stats should derive from this
 */
class ScalarStat : public Stat {
    public:
        ScalarStat() : Stat() {};
    
        virtual void init(const char* name, const char* desc) {
            initStat(name, desc);
        }

        virtual uint64_t get() const = 0;
};


/*
 * Generic lambda stat
 * If your stat depends on a formula, this lets you encode it compactly using C++11 lambdas
 *
 * Usage example:
 *  auto x = [this]() { return curCycle - haltedCycles; }; //declare the lambda function that computes the stat; note this is captured because these values are class members
 *  LambdaStat<decltype(x)>* cyclesStat = new LambdaStat<decltype(x)>(x); //instantiate the templated stat. Each lambda has a unique type, which you get with decltype
 *  cyclesStat->init("cycles", "Simulated cycles"); //etc. Use as an usual stat!
 */
template <typename F>
class LambdaStat : public ScalarStat {
    private:
        F f;
    public:
        LambdaStat(F _f) : f(_f) {} //copy the lambda
        uint64_t get() const {return f();}
};


//Stat Backends declarations.

class StatsBackend : public GlobAlloc {
    public:
        StatsBackend() {};
        virtual ~StatsBackend() {};

        virtual void dump(bool buffered)=0;
};


class TextBackendImpl;

class TextBackend : public StatsBackend {
    private:
        TextBackendImpl* backend;
    public:
        TextBackend(const char* filename, AggregateStat* rootStat);
        virtual void dump(bool buffered);
};


class HDF5BackendImpl;

class HDF5Backend : public StatsBackend {
    private:
        HDF5BackendImpl* backend;
    public:
        HDF5Backend(const char* filename, AggregateStat* rootStat, size_t bytesPerWrite, bool skipVectors, bool sumRegularAggregates); 
        virtual void dump(bool buffered);
};

#endif /*__STATS_H__*/
