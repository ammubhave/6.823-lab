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

/* dsm: A blocked deque class, since std::list performance is so dismal.
 *
 * TODO: Clean up. This code is taken straight out of the ADM runtime code,
 * so a lot of the comments cover how to make things async-safe and so on.
 * This is not really needed here.
 */

#ifndef __BDEQUE_H__
#define __BDEQUE_H__

#include <stdint.h>
#include "log.h"

#define DQBLOCK_SIZE 6
//#define DQBLOCK_SIZE 256
//#define DQBLOCK_SIZE 64

/* Nomenclature: head grows UP, tail grows down. Tail grows block list in next, head does so in prev*/

//#define FUNC_ATTR __attribute__ ((noinline))
#define FUNC_ATTR 

//#define BQDEBUG(args...) printf(args)
#define BQDEBUG(args...)

template <class T>
struct DequeBlock {
   struct DequeBlock<T>* prev;
   struct DequeBlock<T>* next;
   T elems[DQBLOCK_SIZE];
};


/* Templated fully static class implementing a very specialized allocator. Learning enough C++ to hang myself...*/
//TODO: This can be made a generic slab allocator, not restricted to DequeBlock<T>
template <class T>
class BlockPool {
    private: 
        BlockPool<T>() {} //disallow instantiation
        ~BlockPool<T>() {} 
        BlockPool<T>(const BlockPool<T>&); //disallow copy
        BlockPool<T>& operator=(const BlockPool<T>&); //disallow assignment
        
        static bool initialized;

        static uint64_t slabElems;
        static uint64_t slabUsedElems;
        static DequeBlock<T>* slab;

        static uint64_t freelistElems;
        static DequeBlock<T>* freelistHead;

        static uint64_t extraElems;

    public:
        static inline DequeBlock<T>* allocBlock() {
            assert(initialized);
            if (freelistElems > 0) {
                DequeBlock<T>* res = freelistHead;
                freelistHead = freelistHead->next;
                freelistElems--;
                res->next = res->prev = 0;
                return res;
            } else if (slabUsedElems < slabElems) {
                return &slab[slabUsedElems++];
            } else {
                //Grow from normal allocations. It is expected that this will not grow boundless, be quite rare, and simplifies allocation substantially.
                extraElems++;
                return (DequeBlock<T>*) calloc(1, sizeof(DequeBlock<T>));
            }
        }

        static inline void freeBlock(DequeBlock<T>* block) {
            assert(initialized);
            block->next = freelistHead;
            freelistHead = block;
            freelistElems++;
        }

        static inline uint64_t unusedBlocks() {
            uint64_t slabUnused = slabElems - slabUsedElems;
            return slabUnused + ( (extraElems > freelistElems)? 0 : freelistElems - extraElems);
        }

        static void initialize(uint64_t elems) {
            assert(!initialized);
            freelistHead = NULL;
            slabElems = elems;
            slabUsedElems = 0;
            slab = (DequeBlock<T>*) calloc(elems, sizeof(DequeBlock<T>));
            if (!slab) {
                panic("Can't initialize pool, not enough memory");
            }

            freelistElems = 0;
            freelistHead = NULL;
            extraElems = 0;
            initialized = true;
        }

        /* Note the lack of teardown. We'd need to go through the freelist, detect extra
         * elems (by checking whether its address is in the slab's range) and free them
         * to avoid leaks. But this is unneeded for now.
         */
};


template <class T> bool BlockPool<T>::initialized = false;
template <class T> uint64_t BlockPool<T>::slabElems;
template <class T> uint64_t BlockPool<T>::slabUsedElems;
template <class T> DequeBlock<T>* BlockPool<T>::slab;
template <class T> uint64_t BlockPool<T>::freelistElems;
template <class T> DequeBlock<T>* BlockPool<T>::freelistHead;
template <class T> uint64_t BlockPool<T>::extraElems;

//I want this to be of POD type for performance, so it's a struct, and private/public interfaces are not separated
template <class T>
struct /*class*/ BlockedDeque {
    //private:
        /* NOTE: NONE of these are volatile, so we require public functions NOT to be inlined, or volatility issues screw this up */
        //Let's try volatilizing some. Ideally, only numElems needs to be volatile, b/c it's what we check on a dequeue (the only interruptible code that deals with a deque)
        /*volatile*/ int numElems;
        DequeBlock<T>* bhead;
        int phead; //last USED position
        DequeBlock<T>* btail;
        int ptail; //first FREE position

        inline DequeBlock<T>* expandBlock(DequeBlock<T>* block, bool expandPrev) {
            DequeBlock<T>* newBlock;
            newBlock = BlockPool<T>::allocBlock(); 
            //Link
            if (expandPrev) {
                //assert(block->prev == NULL);
                block->prev = newBlock;
                newBlock->next = block;
            } else {
                //assert(block->next == NULL);
                block->next = newBlock;
                newBlock->prev = block;
            }
            //NOTE: We don't care about garbage pointers, they are NEVER read
            return newBlock;
        }

        inline DequeBlock<T>* shrinkBlock(DequeBlock<T>* block, bool shrinkToNext) {
            DequeBlock<T>* newBlock = (shrinkToNext)? block->next : block->prev;
            assert(newBlock);
            BlockPool<T>::freeBlock(block);
            return newBlock;
        }

    //public:
        BlockedDeque() {
          bhead = BlockPool<T>::allocBlock();
          btail = bhead;
          numElems = 0;
          phead = ptail = 0;
       }

       ~BlockedDeque() {
           assert(numElems == 0);
           assert(phead == ptail);
           assert(bhead == btail);
           BlockPool<T>::freeBlock(bhead); 
       }


       inline int elems() FUNC_ATTR {
           return numElems;
       }

       inline int enqueueHead(T val) FUNC_ATTR {
           BQDEBUG("enqueueHead v0=%ld bhead=%p phead=%d numElems=%d\n", val, bhead, phead, numElems);
           if (phead == 0) {
               bhead = expandBlock(bhead, true);
               phead = DQBLOCK_SIZE;
           }
           phead--;
           bhead->elems[phead] = val;
           int elems = numElems + 1;
           numElems = elems;
           return elems;
       }

       inline int enqueueTail(T val) FUNC_ATTR {
           BQDEBUG("enqueueTail v0=%ld btail=%p ptail=%d numElems=%d\n", val, btail, ptail, numElems);
           if (ptail == DQBLOCK_SIZE) {
               btail = expandBlock(btail, false);
               ptail = 0;
           }
           btail->elems[ptail] = val;
           ptail++;
           int elems = numElems + 1;
           numElems = elems;
           BQDEBUG("enqueueTail AFTER v0=%ld btail=%p ptail=%d numElems=%d\n", val, btail, ptail, numElems); 
           return elems;
       }

       //NOTE: These functions ASSUME you're not dequeueing from an empty queue!! No bounds check for performance
       inline void dequeueHead(T& v) FUNC_ATTR {
           assert(numElems > 0);
           BQDEBUG("dequeueHead bhead=%p phead=%d numElems=%d\n", bhead, phead, numElems);
           if (phead == DQBLOCK_SIZE) {
               bhead = shrinkBlock(bhead, true);
               phead = 0;
           }
           v = bhead->elems[phead];
           phead++;
           numElems--;
           BQDEBUG("dequeueHead END v=%ld bhead=%p phead=%d numElems=%d\n", v, bhead, phead, numElems);
       }

       inline void dequeueTail(T& v) FUNC_ATTR {
           assert(numElems > 0);
           BQDEBUG("dequeueTail btail=%p ptail=%d numElems=%d\n", btail, ptail, numElems);
           if (ptail == 0) {
               btail = shrinkBlock(btail, false);
               ptail = DQBLOCK_SIZE;
           }
           ptail--;
           v = btail->elems[ptail];
           numElems--;
           BQDEBUG("dequeueTail END v=%ld btail=%p ptail=%d numElems=%d\n", v, btail, ptail, numElems);
       }

       inline T head() {
           BQDEBUG("head bhead=%p phead=%d numElems=%d\n", bhead, phead, numElems);
           if (phead == DQBLOCK_SIZE) { //TODO: Do this in enqueues/dequeues
               bhead = shrinkBlock(bhead, true);
               phead = 0;
           }
           T v =  bhead->elems[phead];
           BQDEBUG("head END v=%ld bhead=%p phead=%d numElems=%d\n", v, bhead, phead, numElems);
           return v;
       }
};

#endif /*__BDEQUE_H__*/

