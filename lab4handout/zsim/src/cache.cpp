#include <cstdlib>
#include "zsim.h"
#include "log.h"
#include "cache.h"

static bool is_valid(Address a) { return ((a >> 63) & 1); }
static bool is_dirty(Address a) { return ((a >> 62) & 1); }
static uint64_t entry_tag(Address a) { return (a & 0x3fffffffffffffff); }
static uint64_t entry_ctag(Address t) { return (t & 0xffff); }
static Address set_valid(Address a, bool valid) { return (a & 0x7fffffffffffffff) | (((Address) valid) << 63); }
static Address set_dirty(Address a, bool dirty) { return (a & 0xbfffffffffffffff) | (((Address) dirty) << 62); }

Cache::Cache(g_string _name,
             uint32_t _numLines, 
             uint32_t _numWays, 
             uint64_t _hitLatency, 
             bool _isL1Cache,
             bool _isL2Cache,
             bool _isL3Cache,
             Memory *_parent) 
            : name(_name), parent(_parent)
            , latency(_hitLatency)
            , numWays(_numWays)
            , isL1Cache(_isL1Cache)
            , isL2Cache(_isL2Cache)
            , isL3Cache(_isL3Cache)
            , tagArray(){
    tagArray.resize(_numLines);
    for (auto &tags : tagArray) { tags.resize(_numWays, 0); }
    usageQ.resize(_numLines);
    for (int j = 0; j < _numLines; j++) {
        usageQ[j].resize(_numWays);
        for (int i = 0; i < _numWays; i++) {
            usageQ[j][i] = i;
        }
    }
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
    uint32_t old_tag;

    if (isMiss) { // miss
        misses.inc();
        if (!isL3Cache || isWrite) { // evict if not L3
            way = isL1Cache ? random() % numWays : chooseEvictWay(line);
            old_tag = entry_tag(tags[way]);
            if (is_dirty(tags[way]) || (is_valid(tags[way]) && isL2Cache)) {
                Address victimAddr = (tags[way] * tagArray.size()) + line;
                parent->store(victimAddr);
                if (!isL3Cache) {
                    for (auto child : children) {
                        child->invalidate(victimAddr);
                    }
                }
            }
            tags[way] = set_valid(tag, true);
            evicts.inc();
        }
        cycles += parent->load(lineAddr);
    } else { // hit
        if (isL3Cache) { // invalidate if L3 hit
            assert (!isWrite);
            tags[way] = set_valid(tag, false);
            invalidations.inc();
        }
    }

    if (isWrite) {
        tags[way] = set_dirty(tags[way], true);
    }

    if (way != -1)
        updatePolicy(line, way, isMiss, old_tag);

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
    //return random() % numWays;
    /*for (int i = 0; i < (int) B1.size(); ++i) {
        if (B1[i] == ctag) {
            if (boundary == numWays-1)
                return usageQ[0]; // can't increase the size of T1 any further.
            else
                return usageQ[numWays-1];
        }
    }*/
    /*for (int i = 0; i < (int) B2.size(); ++i) {
        if (B2[i] == ctag) {
            return usageQ[0];
        }
    }*/
    //return usageQ[0];
    return usageQ[line][numWays-1];
}

void Cache::updatePolicy(uint32_t line, uint32_t way, bool isMiss, uint32_t old_tag) {
    assert(way < numWays);
    for (int i=numWays-1,j=numWays-1; i > 0; i--,j--) {
        if (usageQ[line][i] == way) {
            j--;
        }
        usageQ[line][i] = usageQ[line][j];
    }
    usageQ[line][0] = way;
    /*int L = numWays;
    if (isMiss) {
        for (int i = 0; i < (int) B1.size(); ++i) {
            if (B1[i] == ctag) {
                for (int j = i; j < (int)B1.size(); ++j)
                    B1[j] = B1[j+1];
                B1[B1.size()-1] = 0;
                if (boundary == numWays-1) {
                    for (int j = (int)B1.size()-1; j > 0; j--)
                        B1[j] = B1[j-1];
                    B1[0] = usageQ[0];
                    for (int j = 1; j < boundary; j++)
                        usageQ[j-1] = usageQ[j];
                    usageQ[boundary-1] = way;
                } else {
                    for (int j = 1; j < (int) B2.size(); j++)
                        B2[j] = B2[j-1];
                    B2[0] = usageQ[usageQ.size()-1];
                    for (int j = boundary+1; j < (int) usageQ.size(); j++)
                        usageQ[j] = usageQ[j-1];
                    usageQ[boundary] = way;
                    boundary++;
                }
                return;
            }
        }
        for (int i = 0; i < (int) B2.size(); ++i) {
            if (B2[i] == ctag) {

            }
        }
    } else {
        for (int i = 0; i <= m; ++i) {
            if (T[i] == way) {
                for (int j = L-m; j <= L-2; j++) 
                    B[j] = B[j-1];
                B[L-1] = entry_ctag(line_t[T[L-1]]);
                for (int j = L-1; j >= m+1; j++)
                    T[j] = T[j-1];
                T[m] = way;
                return;

            }
        }
        for (int i = boundary; i < (int)usageQ.size(); ++i) {
            if (usageQ[i] == way) {
                for (int j = i; j > boundary; j--)
                    usageQ[j] = usageQ[j-1];
                usageQ[boundary] = way;
                return;
            }
        }
        assert (false); // we shouldn't be here, ever
    }*/
}
