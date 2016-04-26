#include <cstdlib>
#include "zsim.h"
#include "log.h"
#include "cache.h"

static bool is_valid(Address a) { return ((a >> 63) & 1); }
static bool is_dirty(Address a) { return ((a >> 62) & 1); }
static uint64_t entry_tag(Address a) { return (a & 0x3fffffffffffffff); }
static Address set_valid(Address a, bool valid) { return (a & 0x7fffffffffffffff) | (((Address) valid) << 63); }
static Address set_dirty(Address a, bool dirty) { return (a & 0xbfffffffffffffff) | (((Address) dirty) << 62); }

Cache::Cache(g_string _name,
             uint32_t _numLines, 
             uint32_t _numWays, 
             uint64_t _hitLatency, 
             bool _isL1Cache,
             Memory *_parent) 
            : name(_name), parent(_parent)
            , latency(_hitLatency)
            , numWays(_numWays)
            , isL1Cache(_isL1Cache)
            , tagArray(){
    tagArray.resize(_numLines);
    for (auto &tags : tagArray) { tags.resize(_numWays, 0); }
}

void Cache::initStats(AggregateStat* parentStat) {
    AggregateStat* cacheStat = new AggregateStat();
    cacheStat->init(name.c_str(), "Cache stats");
    hits.init("hits", "Cache hits");
    misses.init("misses", "Cache misses");
    invalidations.init("invalidations", "Cache invalidations");
    evicts.init("evictions", "Cache evictions");
    cacheStat->append(&hits);
    cacheStat->append(&misses);
    cacheStat->append(&invalidations);
    cacheStat->append(&evicts);
    parentStat->append(cacheStat);
}

uint64_t Cache::load(Address lineAddr) {
    return access(lineAddr, false);
}

uint64_t Cache::store(Address lineAddr) {
    return access(lineAddr, true);
}

// FIXME: Modify Cache::access() function to make it a non-inclusive/exclusive cache 
uint64_t Cache::access(Address lineAddr, bool isWrite) {
    Address line = lineAddr % tagArray.size();
    Address tag = lineAddr / tagArray.size();
    uint64_t cycles = latency;
    auto &tags = tagArray[line];
    int way = -1;

    //info("[%s] access %lx %d", name.c_str(), lineAddr, isWrite);

    for (int i = 0; i < (int) tags.size(); ++i) {
        if (is_valid(tags[i]) && entry_tag(tags[i]) == tag) { // hit
            way = i;
            hits.inc();
            break;
        }
    }
    
    bool isMiss = way == -1;

    if (isMiss) { // miss
        misses.inc();
        way = isL1Cache ? random() % numWays : chooseEvictWay(line);
        if (is_dirty(tags[way])) {
            Address victimAddr = (tags[way] * tagArray.size()) + line;
            parent->store(victimAddr);
            for (auto child : children) {
                child->invalidate(victimAddr);
            }
        }
        cycles += parent->load(lineAddr);
        tags[way] = set_valid(tag, true);
        evicts.inc();
    }

    if (isWrite) {
        tags[way] = set_dirty(tags[way], true);
    }
    updatePolicy(line, way, isMiss);

    return cycles;
}

void Cache::invalidate(Address lineAddr) {
    Address line = lineAddr % tagArray.size();
    Address tag = lineAddr / tagArray.size();
    auto &tags = tagArray[line];
    int way = -1;

    for (int i = 0; i < (int) tags.size(); ++i) {
        if (is_valid(tags[i]) && entry_tag(tags[i]) == tag) { // hit
            way = i;
            break;
        }
    }

    if (way != -1) {
        tags[way] = set_valid(tag, false);
        invalidations.inc();
    }

    for (auto child: children) {
        child->invalidate(lineAddr);
    }

}

// FIXME: Implement your own replacement policy here

uint32_t Cache::chooseEvictWay(uint32_t line) {
    return random() % numWays;
}

void Cache::updatePolicy(uint32_t line, uint32_t way, bool isMiss) {
    assert(way < numWays);
}

