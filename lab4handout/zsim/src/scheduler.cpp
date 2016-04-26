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

#include "pin.H"
#include "scheduler.h"

//The scheduler class started simple, but at some point having it all in the header is too ridiculous. Migrate non perf-intensive calls here! (all but sync, really)

#define WATCHDOG_INTERVAL_USEC (2*1000)
#define WATCHDOG_MAX_MULTIPLER (5) //2-10ms waits

//OK, glibc's sleep functions suck. I want guaranteed minimum sleep time, is that so fucking hard to implement?
void trueSleep(uint32_t usecs) {
    struct timespec req;
    struct timespec rem;

    req.tv_sec = usecs/1000000;
    req.tv_nsec = (usecs*1000) % 1000000000;

    while (req.tv_sec != 0 || req.tv_nsec != 0) {
        int res = syscall(SYS_nanosleep, &req, &rem); //we don't call glibc's nanosleep because errno is not thread-safe in pintools. 
        if (res == 0) break;
        req = rem;
        if (res != -EINTR && res != 0) panic("nanosleep() returned an unexpected error code %d", res);
        //info("nanosleep() interrupted!");
    }
}

void Scheduler::watchdogThreadFunc() {
    info("Started scheduler watchdog thread");
    uint64_t lastPhase = 0;
    int multiplier = 1;
    uint64_t lastMs = 0;
    while (true) {
        trueSleep(multiplier*WATCHDOG_INTERVAL_USEC);
        futex_lock(&schedLock);
        if (lastPhase == curPhase && scheduledThreads == outQueue.size() && !sleepQueue.empty()) {
            //info("Watchdog Thread: Sleep dep detected...")
            int64_t wakeupPhase = sleepQueue.front()->wakeupPhase;
            int64_t wakeupCycles = (wakeupPhase - curPhase)*zinfo->phaseLength;
            int64_t wakeupUsec = (wakeupCycles > 0)? wakeupCycles/zinfo->freqMHz : 0;

            //info("Additional usecs of sleep %ld", wakeupUsec);
            if (wakeupUsec > 10*1000*1000) warn("Watchdog sleeping for a long time due to long sleep, %ld secs", wakeupUsec/1000/1000);

            futex_unlock(&schedLock);
            trueSleep(WATCHDOG_INTERVAL_USEC + wakeupUsec);
            futex_lock(&schedLock);
            if (lastPhase == curPhase && scheduledThreads == outQueue.size() && !sleepQueue.empty()) {
                ThreadInfo* sth = sleepQueue.front();
                uint64_t curMs = curPhase*zinfo->phaseLength/zinfo->freqMHz/1000;
                uint64_t endMs = sth->wakeupPhase*zinfo->phaseLength/zinfo->freqMHz/1000;
                (void)curMs; (void)endMs; //make gcc happy
                if (curMs > lastMs + 1000) {
                    info("Watchdog Thread: Driving time forward to avoid deadlock on sleep (%ld -> %ld ms)", curMs, endMs);
                    lastMs += 1000;
                }
                while (sth->state == SLEEPING) {
                    idlePhases.inc();
                    callback(); //sth will eventually get woken up
                }
                idlePeriods.inc();
                multiplier = 0;
            }
        }

        if (multiplier < WATCHDOG_MAX_MULTIPLER) {
            multiplier++;
        }
        
        lastPhase = curPhase;
        
        if (terminateWatchdogThread) {
            futex_unlock(&schedLock);
            break;
        } else {
            futex_unlock(&schedLock);
        }
    }
    info("Finished scheduler watchdog thread");
}

void Scheduler::threadTrampoline(void* arg) {
    Scheduler* sched = (Scheduler*) arg;
    sched->watchdogThreadFunc();
}

void Scheduler::startWatchdogThread() {
    PIN_SpawnInternalThread(threadTrampoline, this, 64*1024, NULL);
}


