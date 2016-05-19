#ifndef __CACHE_H__
#define __CACHE_H__

#include "stats.h"
#include "g_std/g_string.h"
#include "g_std/g_vector.h"
#include "memory_hierarchy.h"

class Cache : public Memory {
    public:
        Cache(g_string name, uint32_t numLines, uint32_t numWays, uint64_t hitLatency, bool isL1Cache, bool isL2Cache, bool isL3Cache, Memory *parent);
        void initStats(AggregateStat* parentStat);

        uint64_t load(Address lineAddr);
        uint64_t store(Address lineAddr);
        void writeback(Address lineAddr) {}
        void invalidate(Address lineAddr);
        uint64_t access(Address lineAddr, bool isWrite);
        void addChild(Memory* child) { children.push_back(child); }
    private:
        // FIXME: Implement the following two functions
        void updatePolicy(uint32_t line, uint32_t way, bool isMiss, uint32_t old_tag);
        uint32_t chooseEvictWay(uint32_t line, uint32_t ctag);

    private:
        g_string name;
        Memory *parent;
        g_vector<Memory*> children;
        uint64_t latency;
        uint32_t numWays;
        bool isL1Cache;
        bool isL2Cache;
        bool isL3Cache;
        Counter hits;
        Counter misses;
        Counter invalidations;
        Counter evicts;

        typedef uint64_t tag_t;
        typedef g_vector<tag_t> line_t;
        g_vector<line_t> tagArray;

        // FIXME: Add meta data for you replacement policy
        // (e.g. timestamp for LRU)

        // ARC
        g_vector<uint32_t> T;/*[numWays]*/
        uint32_t m, n;
        g_vector<uint32_t> B;/*[numWays]*/
       
        // LRU
        //g_vector<g_vector<uint32_t>> usageQ;
};

#endif // __CACHE_H__

