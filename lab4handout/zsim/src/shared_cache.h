//#ifndef __SHAREDCACHE_H__
//#define __SHAREDCACHE_H__

#include "stats.h"
#include "g_std/g_string.h"
#include "g_std/g_vector.h"
#include "memory_hierarchy.h"
#include "cache.h"

class SharedCache : public Memory {
    public:
        SharedCache(g_string _name,
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
                g_string bankName(ss.str().c_str());
                Cache* bank = new Cache(bankName, bankLines, numWays, latency, false, parent);
                banks.push_back(bank);
            }

        }
 

        void initStats(AggregateStat* parentStat){
            AggregateStat* sharedCacheStat = new AggregateStat();
            sharedCacheStat->init(name.c_str(), "Shared Cache stats");
            accesses.init("accesses", "Shared Cache accesses");
            sharedCacheStat->append(&accesses);

            for (auto bank : banks) {
                bank->initStats(sharedCacheStat);
            }
            parentStat->append(sharedCacheStat);
        } 

        uint64_t load(Address lineAddr){ return access(lineAddr, false); }
        uint64_t store(Address lineAddr){ return access(lineAddr, true); }

        void writeback(Address lineAddr) {}
        void invalidate(Address lineAddr) {}
        
        void addChild(Memory* child) {
            for (auto b : banks) {
                b->addChild(child);
            }
        }

        uint64_t access(Address lineAddr, bool isWrite) {
            uint32_t bank = lineAddr % banks.size();
            accesses.inc();
            return banks[bank]->access(lineAddr, isWrite);
        }

    private:
        g_string name;
        uint32_t lineSize;
        uint32_t totalSize;
        uint32_t numWays;
        uint64_t latency;
        Memory *parent;

        Counter accesses;

        g_vector<Cache*> banks;

};

//#endif //

