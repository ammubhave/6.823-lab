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

#ifndef __SCHEDULER_H__
#define __SCHEDULER_H__

#include "constants.h"
#include "barrier.h"
#include "stats.h"
#include "zsim.h"
#include "core.h"

#include <g_std/g_vector.h>
#include <g_std/g_unordered_map.h>
#include <list>
#include <iomanip>
#include <sstream>


template <typename T>
class InList;

//Intrusive doubly-linked list -- simple enough to not include boost, but watch out for other places this could be used
template <typename T>
struct InListNode {
    T* next;
    T* prev;
    InList<T>* owner;

    InListNode() {
        next = NULL;
        prev = NULL;
        owner = NULL;
    }

    void unlink(InList<T>* lst) {
        if (next) next->prev = prev;
        if (prev) prev->next = next;
        next = NULL;
        prev = NULL;
        assert(lst == owner);
        owner = NULL;
    }

    void linkPrev(T* p, InList<T>* lst) {
        assert(p);
        assert(owner == NULL);
        assert(prev == NULL && next == NULL);
        if (p->next) {
            assert(p->next->prev == p);
            p->next->prev = (T*)this;
            next = p->next;
        }
        p->next = (T*)this;
        prev = p;
        owner = lst;
    }
};

template <typename T>
class InList {
    private:
        T* head;
        T* tail;
        size_t elems;
    public:
        InList() : head(NULL), tail(NULL), elems(0) {};
        bool empty() {return !head;}

        T* front() {return head;}
        T* back() {return tail;}

        void push_front(T* e) {
            assert(e && e->next == NULL && e->prev == NULL && e->owner == NULL);
            if (empty()) {
                head = e;
                tail = e;
            } else {
                assert(head && head->prev == NULL && head->owner == this);
                e->next = head;
                head->prev = e;
                head = e;
            }
            e->owner = this;
            elems++;
        }

        void push_back(T* e) {
            assert(e && e->next == NULL && e->prev == NULL && e->owner == NULL);
            if (empty()) {
                head = e;
                tail = e;
                e->owner = this;
            } else {
                assert(tail);
                e->linkPrev(tail, this);
                tail = e;
            }
            elems++;
        }

        void pop_front() {
            if (empty()) return;
            T* e = head;
            head = e->next;
            e->unlink(this);
            if (!head) tail = NULL;
            elems--;
        }

        void pop_back() {
            if (empty()) return;
            T* e = tail;
            tail = e->prev;
            e->unlink(this);
            if (!tail) head = NULL;
            elems--;
        }

        //Note how remove is O(1)
        void remove(T* e) {
            //info("Remove PRE h=%p t=%p e=%p", head, tail, e);
            if (e == head) head = e->next;
            if (e == tail) tail = e->prev;
            e->unlink(this);
            elems--;
            //info("Remove POST h=%p t=%p e=%p", head, tail);
        }

        void insertAfter(T* prev, T* e) {
            assert(e && e->owner == NULL);
            assert(prev && prev->owner == this);
            e->linkPrev(prev, this);
            if (prev == tail) tail = e;
            elems++;
        }

        size_t size() const {
            return elems;
        }
};


/* Performs (pid, tid) -> cid translation; round-robin scheduling with no notion of locality or heterogeneity... */

class Scheduler : public GlobAlloc, public Callee {
    private:
        enum ThreadState {
            STARTED, //transient state, thread will do a join immediately after
            RUNNING, //has cid assigned, managed by the phase barrier
            OUT, //in leave() this phase, can rejoin immediately
            BLOCKED, //inside a system call, no cid assigned, not in the barrier or the runqueue
            SLEEPING, //inside a patched sleep syscall; no cid assigned, in sleepQueue; it is our responsibility to wake this thread up when its deadline arrives
            QUEUED //in the runqueue
        };

        enum ContextState {
            IDLE,
            USED
        };

        void (*atSyncFunc)(void); //executed by syncing thread while others are waiting. Good for non-thread-safe stuff
        Barrier bar;
        uint32_t numCores;
        uint32_t schedQuantum; //in phases

        struct ThreadInfo : GlobAlloc, InListNode<ThreadInfo> {
            uint32_t gid;
            ThreadState state;
            uint32_t cid; //only current if RUNNING; otherwise, it's the last one used.            
            int64_t quota; //in cycles. Starts at 0, moves between min and max. A thread loses quota when it is scheduled, at a cycle rate; it gains quota when descheduled, but not blocked. THIS SHOULD BE A FUNCTION.

            volatile ThreadInfo* handoffThread; //if at the end of a sync() this is not NULL, we need to transfer our current context to the thread pointed here.
            volatile uint32_t futexWord;
            volatile bool needsJoin; //after waiting on the scheduler, should we join the barrier, or is our cid good to go already?

            bool markedForSleep; //if true, we will go to sleep on the next leave()
            uint64_t wakeupPhase; //if SLEEPING, when do we have to wake up?

            g_vector<bool> mask;

            ThreadInfo(uint32_t _gid, const g_vector<bool>& _mask) : InListNode<ThreadInfo>(), mask(_mask) {
                gid = _gid;
                state = STARTED;
                cid = 0;
                quota = 0;
                handoffThread = NULL;
                futexWord = 0;
                markedForSleep = false;
                wakeupPhase = 0;
                assert(mask.size() == zinfo->numCores);
                uint32_t count = 0;
                for (auto b : mask) if (b) count++;
                if (count == 0) panic("Empty mask on gid %d!", gid);
            }
        };

        struct ContextInfo : InListNode<ContextInfo> {
            uint32_t cid;
            ContextState state;
            ThreadInfo* curThread; //only current if used, otherwise NULL
        };

        g_unordered_map<uint32_t, ThreadInfo*> gidMap;
        g_vector<ContextInfo> contexts;

        InList<ContextInfo> freeList;

        InList<ThreadInfo> runQueue;
        InList<ThreadInfo> outQueue;
        InList<ThreadInfo> sleepQueue; //contains all the sleeping threads, it is ORDERED by wakeup time

        PAD();
        lock_t schedLock;
        PAD();
        
        uint64_t curPhase;
        //uint32_t nextVictim;
        MTRand rnd;

        volatile bool terminateWatchdogThread;

        //Stats
        Counter threadsCreated, threadsFinished;
        Counter scheduleEvents, waitEvents, handoffEvents, sleepEvents;
        Counter idlePhases, idlePeriods;
        VectorCounter occHist, runQueueHist;
        uint32_t scheduledThreads;

        // gid <-> (pid, tid) xlat functions
        inline uint32_t getGid(uint32_t pid, uint32_t tid) {return (pid<<16) | tid;}
        inline uint32_t getPid(uint32_t gid) {return gid>>16;}
        inline uint32_t getTid(uint32_t gid) {return gid & 0x0FFFF;}

    public:
        Scheduler(void (*_atSyncFunc)(void), uint32_t _parallelThreads, uint32_t _numCores, uint32_t _schedQuantum) :
            atSyncFunc(_atSyncFunc), bar(_parallelThreads, this), numCores(_numCores), schedQuantum(_schedQuantum), rnd(0x5C73D9134)
        {
            contexts.resize(numCores);
            for (uint32_t i = 0; i < numCores; i++) {
                contexts[i].cid = i;
                contexts[i].state = IDLE;
                contexts[i].curThread = NULL;
                freeList.push_back(&contexts[i]);
            }
            schedLock = 0;
            //nextVictim = 0; //only used when freeList is empty.
            curPhase = 0;
            scheduledThreads = 0;
            info("Started RR scheduler, quantum=%d phases", schedQuantum);
            terminateWatchdogThread = false;
            startWatchdogThread();
        }

        ~Scheduler() {}

        void initStats(AggregateStat* parentStat) {
            AggregateStat* schedStats = new AggregateStat();
            schedStats->init("sched", "Scheduler stats");
            threadsCreated.init("thCr", "Threads created"); schedStats->append(&threadsCreated);
            threadsFinished.init("thFn", "Threads finished"); schedStats->append(&threadsFinished);
            scheduleEvents.init("schedEvs", "Schedule events"); schedStats->append(&scheduleEvents);
            waitEvents.init("waitEvs", "Wait events"); schedStats->append(&waitEvents);
            handoffEvents.init("handoffEvs", "Handoff events"); schedStats->append(&handoffEvents);
            sleepEvents.init("sleepEvs", "Sleep events"); schedStats->append(&sleepEvents);
            idlePhases.init("idlePhases", "Phases with no thread active"); schedStats->append(&idlePhases);
            idlePeriods.init("idlePeriods", "Periods with no thread active"); schedStats->append(&idlePeriods);
            occHist.init("occHist", "Occupancy histogram", numCores+1); schedStats->append(&occHist);
            uint32_t runQueueHistSize = ((numCores > 16)? numCores : 16) + 1;
            runQueueHist.init("rqSzHist", "Run queue size histogram", runQueueHistSize); schedStats->append(&runQueueHist);
            parentStat->append(schedStats);
        }

        void start(uint32_t pid, uint32_t tid, const g_vector<bool>& mask) {
            futex_lock(&schedLock);
            uint32_t gid = getGid(pid, tid);
            //info("[G %d] Start", gid);
            assert(gidMap.find(gid) == gidMap.end());
            gidMap[gid] = new ThreadInfo(gid, mask);
            threadsCreated.inc();
            futex_unlock(&schedLock);
        }

        void finish(uint32_t pid, uint32_t tid) {
            futex_lock(&schedLock);
            uint32_t gid = getGid(pid, tid);
            //info("[G %d] Finish", gid);
            assert(gidMap.find(gid) != gidMap.end());
            ThreadInfo* th = gidMap[gid];
            gidMap.erase(gid);

            //dsm: Added this check; the normal sequence is leave, finish, but with fastFwd you never know
            if (th->state == RUNNING) {
                warn("RUNNING thread %d (cid %d) called finish(), trying leave() first", tid, th->cid);
                leave(pid, tid, th->cid);
            }
            
            assert_msg(th->state == STARTED /*might be started but in fastFwd*/ ||th->state == OUT || th->state == BLOCKED, "gid %d finish with state %d", gid, th->state);
            if (th->owner) {
                assert(th->owner == &outQueue);
                outQueue.remove(th);
                ContextInfo* ctx = &contexts[th->cid];
                deschedule(th, ctx, BLOCKED);
                freeList.push_back(ctx);
                //no need to try to schedule anything; this context was already being considered while in outQueue
                //assert(runQueue.empty()); need not be the case with masks
                //info("[G %d] Removed from outQueue and descheduled", gid);
            }
            //At this point noone holds pointer to th, it's out from all queues, and either on OUT or BLOCKED means it's not pending a handoff
            delete th;
            threadsFinished.inc();
            futex_unlock(&schedLock);
        }

        uint32_t join(uint32_t pid, uint32_t tid) {
            futex_lock(&schedLock);
            //If leave was in this phase, call bar.join()
            //Otherwise, try to grab a free context; if all are taken, queue up
            uint32_t gid = getGid(pid, tid);
            ThreadInfo* th = gidMap[gid];

            //dsm 25 Oct 2012: Failed this assertion right after a fork when trying to simulate gedit. Very weird, cannot replicate.
            assert_msg(th, "gid not found %d pid %d tid %d", gid, pid, tid);
            
            assert(!th->markedForSleep);

            if (th->state == SLEEPING) {
                /*panic(*/ warn("[%d] called join() while SLEEPING, early sleep termination, moving to BLOCKED", tid);
                sleepQueue.remove(th);
                th->state = BLOCKED;
            }

            if (th->state == OUT) {
                th->state = RUNNING;
                outQueue.remove(th);
                zinfo->cores[th->cid]->join();
                bar.join(th->cid, &schedLock); //releases lock
            } else {
                assert(th->state == BLOCKED || th->state == STARTED);

                ContextInfo* ctx = schedThread(th);
                if (ctx) {
                    schedule(th, ctx);
                    zinfo->cores[th->cid]->join();
                    bar.join(th->cid, &schedLock); //releases lock
                } else {
                    th->state = QUEUED;
                    runQueue.push_back(th);
                    waitForContext(th); //releases lock, might join
                }
            }

            return th->cid;
        }

        void leave(uint32_t pid, uint32_t tid, uint32_t cid) {
            futex_lock(&schedLock);
            //Just call bar.leave
            uint32_t gid = getGid(pid, tid);
            ThreadInfo* th = contexts[cid].curThread;
            assert(th->gid == gid);
            assert(th->state == RUNNING);
            zinfo->cores[cid]->leave();

            if (th->markedForSleep) { //transition to SLEEPING, eagerly deschedule
                trace(Sched, "Sched: %d going to SLEEP, wakeup on phase %ld", gid, th->wakeupPhase);
                th->markedForSleep = false;
                ContextInfo* ctx = &contexts[cid];
                deschedule(th, ctx, SLEEPING);
                
                //Ordered insert into sleepQueue
                if (sleepQueue.empty() || sleepQueue.front()->wakeupPhase > th->wakeupPhase) {
                    sleepQueue.push_front(th);
                } else {
                    ThreadInfo* cur = sleepQueue.front();
                    while (cur->next && cur->next->wakeupPhase <= th->wakeupPhase) {
                        cur = cur->next;
                    }
                    trace(Sched, "Put %d in sleepQueue (deadline %ld), after %d (deadline %ld)", gid, th->wakeupPhase, cur->gid, cur->wakeupPhase);
                    sleepQueue.insertAfter(cur, th);
                }
                sleepEvents.inc();

                ThreadInfo* inTh = schedContext(ctx);
                if (inTh) {
                    schedule(inTh, ctx);
                    wakeup(inTh, false /*no join, we did not leave*/);
                } else {
                    freeList.push_back(ctx);
                    bar.leave(cid); //may trigger end of phase
                }
            } else { //lazily transition to OUT, where we retain our context
                ContextInfo* ctx = &contexts[cid];
                ThreadInfo* inTh = schedContext(ctx);
                if (inTh) { //transition to BLOCKED, sched inTh
                    deschedule(th, ctx, BLOCKED);
                    schedule(inTh, ctx);
                    wakeup(inTh, false /*no join, we did not leave*/);
                } else { //lazily transition to OUT, where we retain our context
                    th->state = OUT;
                    outQueue.push_back(th);
                    bar.leave(cid); //may trigger end of phase
                }
            }

            futex_unlock(&schedLock);
        }

        uint32_t sync(uint32_t pid, uint32_t tid, uint32_t cid) {
            futex_lock(&schedLock);
            ThreadInfo* th = contexts[cid].curThread;
            assert(!th->markedForSleep);
            bar.sync(cid, &schedLock); //releases lock, may trigger end of phase, may block us

            //No locks at this point; we need to check whether we need to hand off our context
            if (th->handoffThread) {
                futex_lock(&schedLock); //this can be made lock-free, but it's not worth the effort
                ThreadInfo* dst = (ThreadInfo*)th->handoffThread; //de-volatilize
                th->handoffThread = NULL;
                ContextInfo* ctx = &contexts[th->cid];
                deschedule(th, ctx, QUEUED);
                schedule(dst, ctx);
                wakeup(dst, false /*no join needed*/);
                handoffEvents.inc();
                //info("%d starting handoff cid %d to gid %d", th->gid, ctx->cid, dst->gid);
                
                //We're descheduled and have completed the handoff. Now we need to see if we can be scheduled somewhere else.
                ctx = schedThread(th);
                if (ctx) { 
                    //TODO: This should only arise in very weird cases (e.g., partially overlapping process masks), and has not been tested
                    warn("Sched: untested code path, check with Daniel if you see this");
                    schedule(th, ctx);
                    //We need to do a join, because dst will not join
                    zinfo->cores[ctx->cid]->join();
                    bar.join(ctx->cid, &schedLock); //releases lock
                } else {
                    runQueue.push_back(th);
                    waitForContext(th); //releases lock, might join
                }
            }

            assert(th->state == RUNNING);
            return th->cid;
        }

        // This is called with schedLock held, and must not release it!
        virtual void callback() {
            //End of phase stats
            assert(scheduledThreads <= numCores);
            occHist.inc(scheduledThreads);
            uint32_t rqPos = (runQueue.size() < (runQueueHist.size()-1))? runQueue.size() : (runQueueHist.size()-1);
            runQueueHist.inc(rqPos);

            if (atSyncFunc) atSyncFunc(); //call the simulator-defined actions external to the scheduler

            /* End of phase accounting */
            zinfo->numPhases++;
            zinfo->globPhaseCycles += zinfo->phaseLength;
            curPhase++;

            assert(curPhase == zinfo->numPhases); //check they don't skew

            //Wake up all sleeping threads where deadline is met
            if (!sleepQueue.empty()) {
                ThreadInfo* th = sleepQueue.front();
                while(th && th->wakeupPhase <= curPhase) {
                    trace(Sched, "%d SLEEPING -> BLOCKED, waking up from fake futex syscall (curPhase %ld, wakeupPhase %ld)", th->gid, curPhase, th->wakeupPhase);
                    th->state = BLOCKED;
                    sleepQueue.pop_front();
                    
                    wakeup(th, false /*no join, this is sleeping out of the scheduler*/);
                    th = sleepQueue.front();
                }
            }

            //Handle rescheduling
            if (runQueue.empty()) return;

            if ((curPhase % schedQuantum) == 0) {
                schedTick();
            }
        }

        volatile uint32_t* markForSleep(uint32_t pid, uint32_t tid, uint64_t wakeupPhase) {
            futex_lock(&schedLock);
            uint32_t gid = getGid(pid, tid);
            trace(Sched, "%d marking for sleep", gid);
            ThreadInfo* th = gidMap[gid];
            assert(!th->markedForSleep);
            th->markedForSleep = true;
            th->wakeupPhase = wakeupPhase;
            th->futexWord = 1; //to avoid races, this must be set here. 
            futex_unlock(&schedLock);
            return &(th->futexWord);
        }

        bool isSleeping(uint32_t pid, uint32_t tid) {
            futex_lock(&schedLock);
            uint32_t gid = getGid(pid, tid);
            ThreadInfo* th = gidMap[gid];
            bool res = th->state == SLEEPING;
            futex_unlock(&schedLock);
            return res;
        }

        void notifySleepEnd(uint32_t pid, uint32_t tid) {
            futex_lock(&schedLock);
            uint32_t gid = getGid(pid, tid);
            ThreadInfo* th = gidMap[gid];
            assert(th->markedForSleep == false);
            //Move to BLOCKED; thread will join pretty much immediately
            assert(th->state == SLEEPING || th->state == BLOCKED);
            if (th->state == BLOCKED) {
                warn("Scheduler:notifySleepEnd: Benign race on SLEEPING->BLOCKED transition, thread is already blocked");
            } else {
                sleepQueue.remove(th);
                th->state = BLOCKED;
            }
            futex_unlock(&schedLock);
        }

        void printThreadState(uint32_t pid, uint32_t tid) {
            futex_lock(&schedLock);
            uint32_t gid = getGid(pid, tid);
            ThreadInfo* th = gidMap[gid];
            info("[%d] is in scheduling state %d", tid, th->state);
            futex_unlock(&schedLock);
        }

        void notifyTermination() {
            futex_lock(&schedLock);
            terminateWatchdogThread = true;
            futex_unlock(&schedLock);
        }

    private:
        void schedule(ThreadInfo* th, ContextInfo* ctx) {
            assert(th->state == STARTED || th->state == BLOCKED || th->state == QUEUED);
            assert(ctx->state == IDLE);
            assert(ctx->curThread == NULL);
            th->state = RUNNING;
            th->cid = ctx->cid;
            ctx->state = USED;
            ctx->curThread = th;
            scheduleEvents.inc();
            scheduledThreads++;
            //info("Scheduled %d <-> %d", th->gid, ctx->cid);
            //FIXME: Update comments!
            zinfo->cores[ctx->cid]->contextSwitch(th->gid);
        }

        void deschedule(ThreadInfo* th, ContextInfo* ctx, ThreadState targetState) {
            assert(th->state == RUNNING || th->state == OUT);
            assert(ctx->state == USED);
            assert(ctx->cid == th->cid);
            assert(ctx->curThread == th);
            assert(targetState == BLOCKED || targetState == QUEUED || targetState == SLEEPING);
            th->state = targetState;
            ctx->state = IDLE;
            ctx->curThread = NULL;
            scheduledThreads--;
            //Notify core of context-switch eagerly.
            //TODO: we may need more callbacks in the cores, e.g. in schedule(). Revise interface as needed...
            zinfo->cores[ctx->cid]->contextSwitch(-1);

            //info("Descheduled %d <-> %d", th->gid, ctx->cid);
        }

        void waitForContext(ThreadInfo* th) {
            th->futexWord = 1;
            waitEvents.inc();
            //info("%d waiting to be scheduled", th->gid);
            //printState();
            futex_unlock(&schedLock);
            while (true) {
                int futex_res = syscall(SYS_futex, &th->futexWord, FUTEX_WAIT, 1 /*a racing thread waking us up will change value to 0, and we won't block*/, NULL, NULL, 0);
                if (futex_res == 0 || th->futexWord != 1) break;
            }
            //info("%d out of sched wait, got cid = %d, needsJoin = %d", th->gid, th->cid, th->needsJoin);
            if (th->needsJoin) {
                futex_lock(&schedLock);
                assert(th->needsJoin); //re-check after the lock
                zinfo->cores[th->cid]->join();
                bar.join(th->cid, &schedLock);
                //info("%d join done", th->gid);
            }
        }

        void wakeup(ThreadInfo* th, bool needsJoin) {
            th->needsJoin = needsJoin;
            bool succ = __sync_bool_compare_and_swap(&th->futexWord, 1, 0);
            if (!succ) panic("Wakeup race in barrier?");
            syscall(SYS_futex, &th->futexWord, FUTEX_WAKE, 1, NULL, NULL, 0);
        }

        void printState() {
            std::stringstream ss;
            for (uint32_t c = 0; c < numCores; c++) {
                if (contexts[c].state == IDLE) {
                    ss << " " << "___";
                } else {
                    ss << " " << std::setw(2) << contexts[c].curThread->gid;
                    if (contexts[c].curThread->state == RUNNING) ss << "r";
                    else if (contexts[c].curThread->state == OUT) ss << "o";
                    else panic("Invalid state cid=%d, threadState=%d", c, contexts[c].curThread->state);
                }
            }
            info(" State: %s", ss.str().c_str());
        }


        //Core scheduling functions
        /* This is actually the interface that an abstract OS scheduler would have, and implements the scheduling policy:
         * - schedThread(): Here's a thread that just became available; return either a ContextInfo* where to schedule it, or NULL if none are available
         * - schedContext(): Here's a context that just became available; return either a ThreadInfo* to schedule on it, or NULL if none are available
         * - schedTick(): Current quantum is over, hand off contexts to other threads as you see fit
         * These functions can REMOVE from runQueue, outQueue, and freeList, but do not INSERT. These are filled in elsewhere. They also have minimal concerns
         * for thread and context states. Those state machines are implemented and handled elsewhere, except where strictly necessary.
         */
        ContextInfo* schedThread(ThreadInfo* th) {
            ContextInfo* ctx = NULL;

            //First, try to get scheduled in the last context we were running at
            assert(th->cid < numCores); //though old, it should be in a valid range
            if (contexts[th->cid].state == IDLE && th->mask[th->cid]) {
                ctx = &contexts[th->cid];
                freeList.remove(ctx);
            }
            
            //Second, check the freeList
            if (!ctx && !freeList.empty()) {
                ContextInfo* c = freeList.front();
                while (c) {
                    if (th->mask[c->cid]) {
                        ctx = c;
                        freeList.remove(ctx);
                        break;
                    } else {
                        c = c->next;
                    }
                }
            }

            //Third, try to steal from the outQueue (block a thread, take its cid)
            if (!ctx && !outQueue.empty()) {
                ThreadInfo* outTh = outQueue.front();
                while (outTh) {
                    if (th->mask[outTh->cid]) {
                        ctx = &contexts[outTh->cid];
                        outQueue.remove(outTh);
                        deschedule(outTh, ctx, BLOCKED);
                        break;
                    } else {
                        outTh = outTh->next;
                    }
                }
            }

            if (ctx) assert(th->mask[ctx->cid]);

            //info("schedThread done, gid %d, success %d", th->gid, ctx != NULL);
            //printState();
            return ctx;
        }

        ThreadInfo* schedContext(ContextInfo* ctx) {
            ThreadInfo* th = NULL;
            ThreadInfo* blockedTh = runQueue.front(); //NULL if empty
            while (blockedTh) {
                if (blockedTh->mask[ctx->cid]) {
                    th = blockedTh;
                    runQueue.remove(blockedTh);
                    break;
                } else {
                    blockedTh = blockedTh->next;
                }
            }

            //info("schedContext done, cid %d, success %d", ctx->cid, th != NULL);
            //printState();
            return th;
        }

        void schedTick() {
            std::vector<uint32_t> availVec;
            availVec.resize(zinfo->numCores);
            for (uint32_t i = 0; i < zinfo->numCores; i++) availVec[i] = i;

            //Random shuffle (Fisher-Yates)
            for (uint32_t i = zinfo->numCores - 1; i > 0; i--) {
                uint32_t j = rnd.randInt(i); //j is in 0,...,i
                std::swap(availVec[i], availVec[j]);
            }

            std::list<uint32_t> avail(availVec.begin(), availVec.end());

            /* NOTE: avail has all cores, including those in freeList, which may not be empty.
             * But we will never match anything in the freeList, because schedContext and
             * schedThread would have matched them out. So, no need to prioritize the freeList.
             */

            uint32_t contextSwitches = 0;

            ThreadInfo* th = runQueue.front();
            while (th && !avail.empty()) {
                bool scheduled = false;
                for (std::list<uint32_t>::iterator it = avail.begin(); it != avail.end(); it++) {
                    uint32_t cid = *it;
                    if (th->mask[cid]) {
                        ContextInfo* ctx = &contexts[cid];
                        ThreadInfo* victimTh = ctx->curThread;
                        assert(victimTh);
                        victimTh->handoffThread = th;
                        contextSwitches++;

                        scheduled = true;
                        avail.erase(it);
                        break;
                    }
                }

                ThreadInfo* pth = th;
                th = th->next;
                if (scheduled) runQueue.remove(pth);
            }

            info("Time slice ended, context-switched %d threads, runQueue size %ld, available %ld", contextSwitches, runQueue.size(), avail.size());
            printState();
        }

        //Watchdog thread functions
        /* With sleeping threads, we have to drive time forward if no thread is scheduled and some threads are sleeping; otherwise, we can deadlock.
         * This initially was the responsibility of the last leaving thread, but led to horribly long syscalls being simulated. For example, if you
         * have 2 threads, 1 is sleeping and the other one goes on a syscall, it had to drive time fwd to wake the first thread up, on the off-chance
         * that the impending syscall was blocking, to avoid deadlock.
         * Instead, we have an auxiliary thread check for this condition periodically, and if all threads are sleeping or blocked, we just drive time
         * forward.
         */
        void startWatchdogThread();
        void watchdogThreadFunc();

        static void threadTrampoline(void* arg);
};

#endif /*__SCHEDULER_H__*/
