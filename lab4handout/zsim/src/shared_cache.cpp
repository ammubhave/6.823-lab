#include <cstdlib>
#include <sstream>
#include "zsim.h"
#include "log.h"
#include "cache.h"
#include "shared_cache.h"
using namespace std;

SharedCache::SharedCache(g_string _name,
             uint32_t _lineSize,
             uint32_t _totalSize,
             uint32_t _numWays, 
             uint64_t _hitLatency,
             Memory *_parent) 
            : name(_name)
            , lineSize(_lineSize)
            , totalSize(_totalSize)
            , numWays(_numWays)
            , latency(_hitLatency)
            , parent(_parent){   
    uint32_t numBanks = totalSize / (1024 * 1024); // 1MB banks
    uint32_t bankSize = totalSize / numBanks;
    for (uint32_t b = 0; b < numBanks; b++){
        uint32_t bankLines = bankSize / (lineSize * numWays);
        stringstream ss;
        ss << "llc-b-" << b;
        g_string bankName(ss.str().c_str())
        Cache* bank = new Cache(bankName, bankLines, numWays, latency, parent);
        banks.push_back(bank);
    }

}

void SharedCache::initStats(AggregateStat* parentStat) {
    AggregateStat* sharedCacheStat = new AggregateStat();
    sharedCacheStat->init(name.c_str(), "Shared Cache stats");
    accesses.init("accesses", "Shared Cache accesses");
    sharedCacheState->append(&accesses);

    for (auto bank : banks) {
        bank->initStats(sharedCacheStat);
    }

    parentStat->append(sharedCacheStat);
}

uint64_t SharedCache::load(Address lineAddr) {
    return access(lineAddr, false);
}

uint64_t SharedCache::store(Address lineAddr) {
    return access(lineAddr, true);
}

uint64_t SharedCache::access(Address lineAddr, bool isWrite) {
}
