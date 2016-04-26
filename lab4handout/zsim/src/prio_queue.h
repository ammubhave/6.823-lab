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

#ifndef __PRIO_QUEUE_H__
#define __PRIO_QUEUE_H__

#include "g_std/g_multimap.h"

template <typename T, uint32_t B>
class PrioQueue {

    struct PQBlock {
        T* array[64];
        uint64_t occ; // bit i is 1 if array[i] is populated

        PQBlock() {
            for (uint32_t i = 0; i < 64; i++) array[i] = NULL;
            occ = 0;
        }

        inline T* dequeue(uint32_t& offset) {
            assert(occ);
            uint32_t pos = __builtin_ctzl(occ);
            T* res = array[pos];
            T* next = res->next;
            array[pos] = next;
            if (!next) occ ^= 1L<<pos;
            assert(res);
            offset = pos;
            res->next = NULL;
            return res;
        }

        inline void enqueue(T* obj, uint32_t pos) {
            occ |= 1L<<pos;
            assert(!obj->next);
            obj->next = array[pos];
            array[pos] = obj;
        }
    };

    PQBlock blocks[B];

    typedef g_multimap<uint64_t, T*> FEMap; //far element map
    typedef typename FEMap::iterator FEMapIterator;
    
    FEMap feMap;

    uint64_t curBlock;
    uint64_t elems;

    public:
        PrioQueue() {
            curBlock = 0;
            elems = 0;
        }

        void enqueue(T* obj, uint64_t cycle) {
            uint64_t absBlock = cycle/64;
            assert(absBlock >= curBlock);

            if (absBlock < curBlock + B) {
                uint32_t i = absBlock % B;
                uint32_t offset = cycle % 64;
                blocks[i].enqueue(obj, offset);
            } else {
                //info("XXX far enq() %ld", cycle);
                feMap.insert(std::pair<uint64_t, T*>(cycle, obj));
            }
            elems++;
        }

        T* dequeue(uint64_t& deqCycle) {
            assert(elems);
            while (!blocks[curBlock % B].occ) {
                curBlock++;
                if ((curBlock % (B/2)) == 0 && !feMap.empty()) {
                    uint64_t topCycle = (curBlock + B)*64;
                    //Move every element with cycle < topCycle to blocks[]
                    FEMapIterator it = feMap.begin();
                    while (it != feMap.end() && it->first < topCycle) {
                        uint64_t cycle = it->first;
                        T* obj = it->second;

                        uint64_t absBlock = cycle/64;
                        assert(absBlock >= curBlock);
                        assert(absBlock < curBlock + B);
                        uint32_t i = absBlock % B;
                        uint32_t offset = cycle % 64;
                        blocks[i].enqueue(obj, offset);
                        it++;
                    }
                    feMap.erase(feMap.begin(), it);
                }
            }

            //We're now at the first populated block
            uint32_t offset;
            T* obj = blocks[curBlock % B].dequeue(offset);
            elems--;

            deqCycle = curBlock*64 + offset;
            return obj;
        }

        inline uint64_t size() const {
            return elems;
        }

        inline uint64_t firstCycle() const {
            assert(elems);
            for (uint32_t i = 0; i < B/2; i++) {
                uint64_t occ = blocks[(curBlock + i) % B].occ;
                if (occ) {
                    uint64_t pos = __builtin_ctzl(occ);
                    return (curBlock + i)*64 + pos;
                }
            }
            for (uint32_t i = B/2; i < B; i++) { //beyond B/2 blocks, there may be a far element that comes earlier
                uint64_t occ = blocks[(curBlock + i) % B].occ;
                if (occ) {
                    uint64_t pos = __builtin_ctzl(occ);
                    uint64_t cycle = (curBlock + i)*64 + pos;
                    return feMap.empty()? cycle : MIN(cycle, feMap.begin()->first);
                }
            }

            return feMap.begin()->first;
        }
};

#endif /*__PRIO_QUEUE_H__*/

