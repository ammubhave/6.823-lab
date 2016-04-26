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

/* dsm: An attempt at having locks that don't suck */

#ifndef __LOCKS_H__
#define __LOCKS_H__

#include "log.h"
#include <stdint.h>


#include <unistd.h>
#include <linux/futex.h>
#include <syscall.h>

#ifdef WITH_MWAIT //careful with this define; most kernels don't allow mwait in userspace
#include <pmmintrin.h>
#else
#include <xmmintrin.h>
#endif

typedef volatile uint32_t lock_t;

/* SPINLOCK: A simple T&T&S spinlock. Lock can use monitor/mwait */

static inline void spin_init(volatile uint32_t* lock) {
    *lock = 0;
    __sync_synchronize();
}

static inline void spin_destroy(volatile uint32_t* lock) {}


static inline void spin_lock(volatile uint32_t* lock)
{
    while (1) {
        if ((*lock) == 0 /*test (read)*/ && __sync_bool_compare_and_swap(lock, 0, 1) /*test&set*/ ) {
            break;
        }

        // At this point, we have the line in S/E/O, or M if we have tried the test&set and failed.
#if WITH_MWAIT
        //Monitor / mwait
        _mm_monitor((const void*)lock, 0, 0);

        //Must test again, might have intervening write BEFORE monitor (so we would get stuck in mwait)
        if (*lock) {
            _mm_mwait(0, 0);
        }
#else
        //If we don't have mwait, we can at least pause
        _mm_pause();
#endif
    }
}

static inline int spin_trylock(volatile uint32_t* lock)
{
    return !((*lock) == 0 /*T*/ && __sync_bool_compare_and_swap(lock, 0, 1) /*T&S*/);
}


static inline void spin_unlock(volatile uint32_t* lock)
{
    assert(*lock == 1); //should own lock if we're unlocking...
    *lock = 0;
    __sync_synchronize();
}

/* TICKET LOCK: Provides FIFO ordering for fairness.
 * WARNING: Will not work with more than 64K threads
 */

#define TICKET_MASK ((1<<16) - 1)

static inline void ticket_init(volatile uint32_t* lock) {
    *lock = 0;
    __sync_synchronize();
}

static inline void ticket_destroy(volatile uint32_t* lock) {}


static inline void ticket_lock(volatile uint32_t* lock)
{
    /* Technically, we want to do this, but I'm guessing the 64-bit
     * datapath is not very well optimized for 16-bit xadd...
     * volatile uint16_t* low = ((volatile uint16_t*) lock) + 1;
     * uint32_t ticket = atomic_fetchadd_16(low, 1);
     */
    uint32_t val, hi, newLo;
    while(true) {
        val = *lock;
        hi = val & (TICKET_MASK << 16);
        newLo = (val + 1) & TICKET_MASK;
        if (__sync_bool_compare_and_swap(lock, val, (hi | newLo))) break;
    }

    uint32_t ticket = val & TICKET_MASK;

    while ((((*lock) >> 16) & TICKET_MASK) != ticket) {
#if WITH_MWAIT
        //Monitor / mwait
        _mm_monitor((const void*)lock, 0, 0);

        //Must test again, might have intervening write BEFORE monitor (so we would get stuck in mwait)
        if (*lock) {
            _mm_mwait(0, 0);
        }
#else
        //If we don't have mwait, we can at least pause
        _mm_pause();
#endif
    }
}

static inline int ticket_trylock(volatile uint32_t* lock)
{
    uint32_t val = *lock;
    uint32_t hi = (val >> 16) & TICKET_MASK;
    uint32_t lo = val & TICKET_MASK;
    uint32_t newLo = (lo + 1) & TICKET_MASK;
    return (hi == lo /*This is up for grabs*/ && __sync_bool_compare_and_swap(lock, val, ((hi << 16) | newLo)) /*T&S*/);
}


static inline void ticket_unlock(volatile uint32_t* lock)
{
    __sync_fetch_and_add(lock, 1<<16);
}


static inline void futex_init (volatile uint32_t* lock) {
    spin_init(lock);
}

/* NOTE: The current implementation of this lock is quite unfair. Not that we care for its current use. */
static inline void futex_lock (volatile uint32_t* lock) {
    uint32_t c;
    do {
        for (int i = 0; i < 1000; i++) { //this should be tuned to balance syscall/context-switch and user-level spinning costs 
            if (*lock == 0 && __sync_bool_compare_and_swap(lock, 0, 1)) {
                return;
            }
            _mm_pause();
        }

        //At this point, we will block
        c = __sync_lock_test_and_set(lock, 2); //this is not exactly T&S, but atomic exchange; see GCC docs
        if (c == 0) return;
        syscall(SYS_futex, lock, FUTEX_WAIT, 2, NULL, NULL, 0);
        c = __sync_lock_test_and_set(lock, 2); //atomic exchange
    } while (c != 0);
}

static inline void futex_lock_nospin (volatile uint32_t* lock) {
    uint32_t c;
    do {
        if (*lock == 0 && __sync_bool_compare_and_swap(lock, 0, 1)) {
            return;
        }

        //At this point, we will block
        c = __sync_lock_test_and_set(lock, 2); //this is not exactly T&S, but atomic exchange; see GCC docs
        if (c == 0) return;
        syscall(SYS_futex, lock, FUTEX_WAIT, 2, NULL, NULL, 0);
        c = __sync_lock_test_and_set(lock, 2); //atomic exchange
    } while (c != 0);
}



static inline void futex_unlock (volatile uint32_t* lock) {
    if (__sync_fetch_and_add(lock, -1) != 1) {
        *lock = 0;
        syscall(SYS_futex, lock, FUTEX_WAKE, 1 /*wake next*/, NULL, NULL, 0); //this may result in additional wakeups, but avoids completely starving processes that are sleeping on this. Still, if there is lots of contention in userland, this doesn't work that well. But I don't care that much, as this only happens between phase locks.
    }
}

#endif
