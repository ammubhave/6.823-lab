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

#include "syscall_table.h" //always first

#include "syscall_funcs.h"
#include "constants.h"
#include "log.h"

#include <syscall.h>
#include <linux/futex.h>
#include <string.h>

#include "zsim.h"
#include "process_tree.h"

#include "scheduler.h"
#include <sys/types.h>
#include <signal.h>

#include "rdtsc.h"

// For network virt
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "port_virt.h"

#ifndef SYS_getcpu //because it's a vsyscall, some header files don't even define it
#define SYS_getcpu 168 //it's always 168 though
#endif

//We need these two syscalls, glibc does not provide wrappers
void tkill(pid_t tid, int sig) {
    //NOTE: tkill is not always safe in case a tid gets reused, but we have ownership of the thread we're signalling, so I absolutely do not care to use tgkill instead
    int res = syscall(SYS_tkill, tid, sig);
    assert(res == 0);
}

pid_t gettid() {
    return syscall(SYS_gettid);
}

static bool prePatchNanosleep(uint32_t tid, CONTEXT* ctxt, SYSCALL_STANDARD std, bool isClock); //isClock should be true for clock_nanosleep, false for nanosleep
static void postPatchNanosleep(uint32_t tid, CONTEXT* ctxt, SYSCALL_STANDARD std, bool isClock);
static bool prePatchTimeoutSyscall(uint32_t tid, CONTEXT* ctxt, SYSCALL_STANDARD std);
static bool postPatchTimeoutSyscall(uint32_t tid, CONTEXT* ctxt, SYSCALL_STANDARD std);

static void prePatchSanityCheck(uint32_t tid);

//PIN_SafeCopy wrapper. We expect the default thing to be correct access
template<typename T>
static inline bool safeCopy(const T* src, T* dst, const char* file = __FILE__, int line = __LINE__) {
    size_t copiedBytes = PIN_SafeCopy(dst, src, sizeof(T));
    if (copiedBytes != sizeof(T)) {
        warn("[%d] %s:%d Failed app<->tool copy (%ld/%ld bytes copied)", PIN_ThreadId(), file, line, copiedBytes, sizeof(T));
        return false;
    }
    return true;
}

//Helper functions to translate between ns, timespec/timeval, and cycles

//ns per s :)
#define NSPS (1000*1000*1000)

uint64_t timevalToNs(struct timeval tv) {
    return tv.tv_sec*NSPS + tv.tv_usec*1000;
}

uint64_t timespecToNs(struct timespec ts) {
    return ts.tv_sec*NSPS + ts.tv_nsec;
}

struct timeval nsToTimeval(uint64_t ns) {
    struct timeval res;
    res.tv_sec = ns/NSPS;
    res.tv_usec = (ns % NSPS)/1000;
    return res;
}

struct timespec nsToTimespec(uint64_t ns) {
    struct timespec res;
    res.tv_sec = ns/NSPS;
    res.tv_nsec = (ns % NSPS);
    return res;
}

uint64_t cyclesToNs(uint64_t cycles) {
    return cycles*1000/zinfo->freqMHz;
}

uint64_t nsToCycles(uint64_t cycles) {
    return cycles*zinfo->freqMHz/1000;
}



//Helper function, see /usr/include/linux/futex.h
static bool isFutexWaitOp(int op) {
    switch (op) {
        case FUTEX_WAIT:
        case FUTEX_WAIT_BITSET:
        case FUTEX_WAIT_REQUEUE_PI:
        case FUTEX_WAIT_PRIVATE:
        case FUTEX_WAIT_BITSET_PRIVATE:
        case FUTEX_WAIT_REQUEUE_PI_PRIVATE:
            return true;
        default:
            return false;
    }
}

void warnIfTimingRelated(uint32_t tid, int syscall) {
    switch(syscall) {
        case SYS_poll:
        case SYS_select:
        case SYS_getitimer:
        case SYS_alarm:
        case SYS_setitimer:
        case SYS_semop:
        case SYS_gettimeofday:
        case SYS_times:
        case SYS_rt_sigtimedwait:
        case SYS_time:
        case SYS_futex:
        case SYS_mq_timedsend:
        case SYS_mq_timedreceive:
        case SYS_pselect6:
        case SYS_ppoll:
            warn("[%d] Executing unvirtualized potentially timing-sensitive syscall: %s (%d)", tid, syscallNames[syscall], syscall);
        default:
            ;
    }
}

/* For now, this implementation ties to the current syscall defs,
 * so if you change machines, this will need tuning!
 */

/* This should err on the side of caution: False positives are not so bad (may cause a few extra context-switches),
 * but false negatives can cause a deadlock. When in doubt, we block!
 */
bool blockOnSyscall(CONTEXT *ctxt, SYSCALL_STANDARD std) {
    return true; //TODO
}

//Patching functions!
static std::string pathPatches[MAX_THREADS];
static const char* pathRestores[MAX_THREADS];

const char* fakedPaths[] = {"/proc/cpuinfo", "/proc/stat", "/sys/"};

enum PatchType {PT_NONE=0, PT_BIND, PT_GETSOCKNAME, PT_CONNECT, PT_GETTIMEOFDAY, PT_TIME, PT_CLOCK_GETTIME, PT_GETCPU, PT_NANOSLEEP, PT_CLOCK_NANOSLEEP, PT_TIMEOUT_SYSCALL};

static PatchType sysPatch[MAX_THREADS];
static uint64_t sysEmuScratch[MAX_THREADS]; //scratch space for syscalls, valid only between prePatch and postPatch

//Helper for port virtualization
static struct sockaddr_in* GetSockAddr(ADDRINT guestAddr, size_t guestSize) {
    if (guestSize != sizeof(struct sockaddr_in)) return NULL;
    struct sockaddr_in* res = (struct sockaddr_in*) malloc(sizeof(struct sockaddr_in));
    if (!safeCopy((struct sockaddr_in*) guestAddr, res) || res->sin_family != AF_INET) {
        free(res);
        return NULL;
    }
    return res;
}


void syscallPatchPre(THREADID tid, CONTEXT *ctxt, SYSCALL_STANDARD std, const char* patchRoot, bool isNopThread) {
    prePatchSanityCheck(tid);

    uint32_t syscall = PIN_GetSyscallNumber(ctxt, std);
    if (syscall == SYS_open) { //these are ALWAYS patched
        if (!patchRoot) return; //process does not want patched system...
        ADDRINT pathArg = PIN_GetSyscallArgument(ctxt, std, 0);
        const char* fileName = (const char*) pathArg;
        if (fileName) {
            bool match = false;
            for (uint32_t i = 0; i < 3; i++) {
                uint32_t diff = strncmp(fileName, fakedPaths[i], strlen(fakedPaths[i]));
                if (!diff) {
                    match = true;
                    break;
                } 
            }

            if (match) {
                pathPatches[tid] = patchRoot;
                pathPatches[tid] += fileName;

                //Try to open the patched file to see if it exists
                FILE * patchedFd = fopen(pathPatches[tid].c_str(), "r");
                if (patchedFd) {
                    fclose(patchedFd);
                    pathRestores[tid] = fileName;
                    info("Patched SYS_open, original %s, patched %s", fileName, pathPatches[tid].c_str());
                    PIN_SetSyscallArgument(ctxt, std, 0, (ADDRINT) pathPatches[tid].c_str());
                } else {
                    info( "Patched SYS_open to match %s, left unpatched (no patch)", fileName);
                }
            }
        }
    } else if (syscall == SYS_bind) {
        ADDRINT sAddrPtr = PIN_GetSyscallArgument(ctxt, std, 1);
        ADDRINT sLen = PIN_GetSyscallArgument(ctxt, std, 2);
        struct sockaddr_in* servAddr = GetSockAddr(sAddrPtr, sLen);
        if (!servAddr) return; //invalid input or socketaddr family

        int port = ntohs(servAddr->sin_port);
        if (port != 0) { //if port is 0, we don't need to virtualize, OS will assign a free one
            uint32_t portDomain = zinfo->procArray[procIdx]->getPortDomain();
            info("Virtualizing bind() to port %d (domain %d)", port, portDomain);
            zinfo->portVirt[portDomain]->lock(); //unlocked either on write failure below, or after the syscall
            int prevPort = zinfo->portVirt[portDomain]->lookupReal(port);
            if (prevPort == -1) {
                //No previous bind(), request whatever
                servAddr->sin_port = htons(0);
            } else {
                //There was a previous bind() on this port, so we reuse the translation
                //This should work in MOST cases, but may fail if the port is reused by something else and we conflict. Should be quite rare, since Linux tries to space out anonymous reassigns to the same port
                warn("bind() to port %d, this port already has a translation %d, using it --- in rare cases this may fail when the unvirtualized case should succeed", port, prevPort);
                servAddr->sin_port = htons(prevPort);
            }
            PIN_SetSyscallArgument(ctxt, std, 1, (ADDRINT) servAddr);
            sysPatch[tid] = PT_BIND;
            sysEmuScratch[tid] = sAddrPtr;
        } else {
            free(servAddr);
        }
    } else if (syscall == SYS_getsockname) {
        sysPatch[tid] = PT_GETSOCKNAME;
    } else if (syscall == SYS_connect) {
        ADDRINT sAddrPtr = PIN_GetSyscallArgument(ctxt, std, 1);
        ADDRINT sLen = PIN_GetSyscallArgument(ctxt, std, 2);
        struct sockaddr_in* servAddr = GetSockAddr(sAddrPtr, sLen);
        if (!servAddr) return; //invalid input or socketaddr family

        int virtPort = ntohs(servAddr->sin_port);
        uint32_t portDomain = zinfo->procArray[procIdx]->getPortDomain();
        zinfo->portVirt[portDomain]->lock();
        int realPort = zinfo->portVirt[portDomain]->lookupReal(virtPort);
        zinfo->portVirt[portDomain]->unlock();
        if (realPort != -1) {
            info("Virtualizing connect(), v: %d r: %d (domain %d)", virtPort, realPort, portDomain);
            servAddr->sin_port = htons(realPort);
            PIN_SetSyscallArgument(ctxt, std, 1, (ADDRINT) servAddr);
            sysPatch[tid] = PT_CONNECT;
            sysEmuScratch[tid] = sAddrPtr;
        } else {
            free(servAddr);
        }
    } else if (!isNopThread && !zinfo->procArray[procIdx]->isInFastForward()) { //having both conditions ensures that we don't virtualize in the interim of toggling ff ON
        //Timing virtualization patching happens only on non fast-forwarded threads
        if (syscall == SYS_gettimeofday) {
            //Hmm, this is a vsyscall, but it still does sysenter. Hopefully we'll catch it!
            //Nope. This is doubly patched to handle the vsyscall case and direct syscalls, see zsim.cpp
            trace(TimeVirt, "[%d] Pre-patching SYS_gettimeofday", tid);
            sysPatch[tid] = PT_GETTIMEOFDAY;
        } else if (syscall == SYS_time) {
            trace(TimeVirt, "[%d] Pre-patching SYS_time", tid);
            sysPatch[tid] = PT_TIME;
        } else if (syscall == SYS_clock_gettime) {
            trace(TimeVirt, "[%d] Pre-patching SYS_clock_gettime", tid);
            sysPatch[tid] = PT_CLOCK_GETTIME;
        } else if (syscall == SYS_getcpu) {
            sysEmuScratch[tid] = getCid(tid);
            sysPatch[tid] = PT_GETCPU;
        } else if (syscall == SYS_nanosleep) {
            if (prePatchNanosleep(tid, ctxt, std, false)) {
                //syscall was patched
                sysPatch[tid] = PT_NANOSLEEP;
            }
        } else if (syscall == SYS_clock_nanosleep) {
            if (prePatchNanosleep(tid, ctxt, std, true /*clock variant*/)) {
                //syscall was patched
                sysPatch[tid] = PT_CLOCK_NANOSLEEP;
            }
        } else if (syscall == SYS_futex || syscall == SYS_epoll_wait || syscall == SYS_epoll_pwait || syscall == SYS_poll) {
            if (prePatchTimeoutSyscall(tid, ctxt, std)) {
                sysPatch[tid] = PT_TIMEOUT_SYSCALL;
            }
        } else {
            warnIfTimingRelated(tid, syscall);
        }
    }
}

//We need to patch the old value back, otherwise the simulated app will be using simulator memory!
//Returns an PostPatchAction
PostPatchAction syscallPatchPost(THREADID tid, CONTEXT *ctxt, SYSCALL_STANDARD std) {
    if (pathRestores[tid]) {
        PIN_SetSyscallArgument(ctxt, std, 0, (ADDRINT) pathRestores[tid]); //hopefully this works; it is doing something sensible.
        pathRestores[tid] = NULL;
        //trace(TimeVirt, "Restored syscall arg")
    }

    if (sysPatch[tid] != PT_NONE) {
        if (sysPatch[tid] == PT_BIND) {
            struct sockaddr_in* servAddr = (struct sockaddr_in*) PIN_GetSyscallArgument(ctxt, std, 1);
            ADDRINT sAddrPtr = sysEmuScratch[tid];
            int virtPort = ntohs(((struct sockaddr_in*)sAddrPtr)->sin_port);

            uint32_t portDomain = zinfo->procArray[procIdx]->getPortDomain();
            REG out = (REG) PIN_GetSyscallNumber(ctxt, std);
            if (out == 0) {
                int sockfd = PIN_GetSyscallArgument(ctxt, std, 0);
                struct sockaddr_in sockName; //NOTE: sockaddr_in to sockaddr casts are fine
                socklen_t sockLen = sizeof(sockName);
                if (getsockname(sockfd, (struct sockaddr*)&sockName, &sockLen) != 0) {
                    panic("bind() succeeded, but getsockname() failed...");
                }
                int realPort = ntohs(sockName.sin_port);

                info("Virtualized bind(), v: %d r: %d (domain %d)", virtPort, realPort, portDomain);
                zinfo->portVirt[portDomain]->registerBind(virtPort, realPort);
            } else {
                info("bind(): tried to virtualize port, but bind() failed, not registering (domain %d)", portDomain);
            }
            zinfo->portVirt[portDomain]->unlock(); //note lock was in prepatch

            //Restore original descriptor, free alloc
            PIN_SetSyscallArgument(ctxt, std, 1, sAddrPtr);
            free(servAddr);
        } else if (sysPatch[tid] == PT_GETSOCKNAME) {
            REG out = (REG) PIN_GetSyscallNumber(ctxt, std);
            if (out == 0) {
                ADDRINT sockAddrPtr = PIN_GetSyscallArgument(ctxt, std, 1);
                struct sockaddr_in sockAddr;
                //safecopy may fail here and that's OK, it's just not a sockaddr_in, so not IPv4
                if (safeCopy((struct sockaddr_in*) sockAddrPtr, &sockAddr) && sockAddr.sin_family == AF_INET) {
                    int realPort = ntohs(sockAddr.sin_port);
                    uint32_t portDomain = zinfo->procArray[procIdx]->getPortDomain();
                    zinfo->portVirt[portDomain]->lock();
                    int virtPort = zinfo->portVirt[portDomain]->lookupVirt(realPort);
                    zinfo->portVirt[portDomain]->unlock();
                    if (virtPort != -1) {
                        info("Virtualizing getsockname() on previously bound port, r: %d, v: %d (domain %d)", realPort, virtPort, portDomain);
                        sockAddr.sin_port = htons(virtPort);
                        if (!safeCopy(&sockAddr, (struct sockaddr_in*) sockAddrPtr)) {
                            panic("getsockname() virt fail");
                        }
                    }
                }
            } //else this failed, no need to virtualize
        } else if (sysPatch[tid] == PT_CONNECT) {
            //Restore original (virt) port (NOTE: regardless of whether connect() succeeded or not)
            struct sockaddr_in* servAddr = (struct sockaddr_in*) PIN_GetSyscallArgument(ctxt, std, 1);
            ADDRINT sAddrPtr = sysEmuScratch[tid];
            PIN_SetSyscallArgument(ctxt, std, 1, sAddrPtr);
            free(servAddr);
        } else if (sysPatch[tid] == PT_GETTIMEOFDAY) {
            trace(TimeVirt, "[%d] Post-patching SYS_gettimeofday", tid);
            ADDRINT arg0 = PIN_GetSyscallArgument(ctxt, std, 0);
            patchGettimeofday(tid, arg0);
        } else if (sysPatch[tid] == PT_TIME) {
            trace(TimeVirt, "[%d] Post-patching SYS_time", tid);
            ADDRINT arg0 = PIN_GetSyscallArgument(ctxt, std, 0);
            REG out = (REG)PIN_GetSyscallNumber (ctxt, std);
            patchTime(tid, &out, arg0);
            PIN_SetSyscallNumber(ctxt, std, (ADDRINT) out); //hack, we have no way of setting the result so here we go; this changes rax just as well
        } else if (sysPatch[tid] == PT_CLOCK_GETTIME) {
            trace(TimeVirt, "[%d] Post-patching SYS_clock_gettime", tid);
            ADDRINT arg0 = PIN_GetSyscallArgument(ctxt, std, 0);
            ADDRINT arg1 = PIN_GetSyscallArgument(ctxt, std, 1);
            patchClock_gettime(tid, arg0, arg1);
        } else if (sysPatch[tid] == PT_GETCPU) {
            trace(TimeVirt, "[%d] Post-patching SYS_getcpu", tid);
            ADDRINT arg0 = PIN_GetSyscallArgument(ctxt, std, 0);
            ADDRINT arg1 = PIN_GetSyscallArgument(ctxt, std, 1);
            patchGetcpu(tid, arg0, arg1);
        } else if (sysPatch[tid] == PT_NANOSLEEP) {
            trace(TimeVirt, "[%d] Post-patching SYS_nanosleep", tid);
            postPatchNanosleep(tid, ctxt, std, false);
        } else if (sysPatch[tid] == PT_CLOCK_NANOSLEEP) {
            trace(TimeVirt, "[%d] Post-patching SYS_clock_nanosleep", tid);
            postPatchNanosleep(tid, ctxt, std, true /*clock variant*/);
        } else if (sysPatch[tid] == PT_TIMEOUT_SYSCALL) {
            trace(TimeVirt, "[%d] Post-patching timeout syscall", tid);
            if (postPatchTimeoutSyscall(tid, ctxt, std)) {
                return PPA_USE_NOP_PTRS;
            } else {
                sysPatch[tid] = PT_NONE;
                return PPA_USE_JOIN_PTRS;
            }
        }
        sysPatch[tid] = PT_NONE;
    }

    return PPA_NOTHING; //no action by default
}

void patchGettimeofday(uint32_t tid, ADDRINT arg0) {
    trace(TimeVirt, "[%d] Post-patching gettimeofday", tid);
    if (arg0) {
        struct timeval tv;
        if (!safeCopy((struct timeval*) arg0, &tv)) {
            info("Failed read of gettimeofday() input");
            return;
        }
        trace(TimeVirt, "Orig %ld sec, %ld usec", tv.tv_sec, tv.tv_usec);
        uint64_t simNs = cyclesToNs(zinfo->globPhaseCycles);
        uint32_t domain = zinfo->procArray[procIdx]->getClockDomain();
        tv = nsToTimeval(zinfo->clockDomainInfo[domain].realtimeOffsetNs + simNs);

        trace(TimeVirt, " Patched %ld sec, %ld usec", tv.tv_sec, tv.tv_usec);
        if (!safeCopy(&tv, (struct timeval*) arg0)) {
            info("Failed write of gettimeofday() output");
        }
    }
}


void patchTime(uint32_t tid, REG* out, ADDRINT arg0) {
    time_t origRes = (time_t)out;
    if (origRes == ((time_t)-1) || origRes == ((time_t)-EFAULT)) { //glibc will return -1; raw syscall will return -EFAULT
        info("[%d] post-patch time(), returned error or EFAULT (%ld)", tid, origRes);
        return;
    }

    uint64_t simNs = cyclesToNs(zinfo->globPhaseCycles);
    uint32_t domain = zinfo->procArray[procIdx]->getClockDomain();
    time_t tm = (zinfo->clockDomainInfo[domain].realtimeOffsetNs + simNs)/NSPS;

    trace(TimeVirt, "[%d] Post-patching time(), orig %ld, new %ld", tid, (time_t)*out, tm);
    *out = (REG)tm;
    if (arg0) {
        if (!safeCopy(&tm, (time_t*) arg0)) {
            info("Failed write of time() output");
        }
    }
}


void patchClock_gettime(uint32_t tid, ADDRINT arg0, ADDRINT arg1) {
    uint32_t domain = zinfo->procArray[procIdx]->getClockDomain();
    ClockDomainInfo& dom =  zinfo->clockDomainInfo[domain];
    
    //arg0 indicates clock type
    uint64_t offset = 0;
    switch (arg0) {
        case CLOCK_MONOTONIC:
            offset = dom.monotonicOffsetNs;
            break;
        case CLOCK_REALTIME:
            offset = dom.realtimeOffsetNs;
            break;
        case CLOCK_PROCESS_CPUTIME_ID:
            offset = dom.processOffsetNs;
            break;
        case CLOCK_THREAD_CPUTIME_ID:
            offset = dom.processOffsetNs;
            warn("clock_gettime() called with CLOCK_THREAD_CPUTIME_ID, faking with CLOCK_PROCESS_CPUTIME_ID");
            break;
    } //with others, the result does not matter --- actual clock_gettime has returned -1 and EINVAL
    
    if (arg1) {
        struct timespec ts; 
        if (!safeCopy((struct timespec*) arg1, &ts)) {
            info("Failed read of clock_gettime() input");
            return;
        }

        trace(TimeVirt, "Patching clock_gettime()");
        trace(TimeVirt, "Orig %ld sec, %ld nsec", ts.tv_sec, ts.tv_nsec);

        clock_gettime(CLOCK_MONOTONIC, &ts);
        trace(TimeVirt, "MONOTONIC %ld sec, %ld nsec", ts.tv_sec, ts.tv_nsec);
        clock_gettime(CLOCK_REALTIME, &ts);
        trace(TimeVirt, "REALTIME %ld sec, %ld nsec", ts.tv_sec, ts.tv_nsec);
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
        trace(TimeVirt, "PROCESS_CPUTIME_ID %ld sec, %ld nsec", ts.tv_sec, ts.tv_nsec);
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
        trace(TimeVirt, "THREAD_CPUTIME_ID %ld sec, %ld nsec", ts.tv_sec, ts.tv_nsec);

        uint64_t simNs = cyclesToNs(zinfo->globPhaseCycles);
        ts = nsToTimespec(offset + simNs);
        trace(TimeVirt, "Patched %ld sec, %ld nsec", ts.tv_sec, ts.tv_nsec);

        if (!safeCopy(&ts, (struct timespec*) arg1)) {
            info("Failed write of gettimeofday() output");
        }
    }
}

void patchGetcpu(uint32_t tid, ADDRINT arg0, ADDRINT arg1) {
    uint32_t cpu = getCid(tid); //valid for the vdso version
    if (cpu == (uint32_t)-1) cpu = sysEmuScratch[tid]; //valid for the syscall version, since we are deschedule on a syscall
    unsigned resCpu;
    unsigned resNode = 0;
    if (!arg0) {
        info("getcpu() called with NULL cpu arg");
    }
    if (!safeCopy((unsigned*)arg0, &resCpu)) {
        info("getcpu() called with invalid cpu arg");
        return;
    }
    if (arg1 && !safeCopy((unsigned*)arg1, &resNode)) {
        info("getcpu() called with invalid node arg");
        return;
    }

    trace(TimeVirt, "Patching getcpu()");
    trace(TimeVirt, "Orig cpu %d, node %d, patching core %d / node 0", resCpu, resNode, cpu);
    resCpu = cpu;
    resNode = 0;

    safeCopy(&resCpu, (unsigned*)arg0);
    if (arg1) safeCopy(&resNode, (unsigned*)arg1);
}


//Sleep and hardcore syscall emulation. HERE BE DRAGONS, you are warned

static CONTEXT preSyscallCtxts[MAX_THREADS]; //these things overwrite random syscall register state, so we save the full context for later emulation...

static bool prePatchNanosleep(uint32_t tid, CONTEXT* ctxt, SYSCALL_STANDARD std, bool isClock) {
    struct timespec* ts;
    uint64_t offsetNsec = 0;
    if (isClock) {
        trace(TimeVirt, "[%d] Pre-patching SYS_clock_nanosleep", tid);
        int flags = (int) PIN_GetSyscallArgument(ctxt, std, 1);
        ts = (struct timespec*) PIN_GetSyscallArgument(ctxt, std, 2);
        if (flags == TIMER_ABSTIME) {
            trace(TimeVirt, "[%d] SYS_clock_nanosleep requests TIMER_ABSTIME, offsetting", tid);
            uint32_t domain = zinfo->procArray[procIdx]->getClockDomain();
            uint64_t simNs = cyclesToNs(zinfo->globPhaseCycles);
            offsetNsec = simNs + zinfo->clockDomainInfo[domain].realtimeOffsetNs;
        }
    } else {
        trace(TimeVirt, "[%d] Pre-patching SYS_nanosleep", tid);
        ts = (struct timespec*) PIN_GetSyscallArgument(ctxt, std, 0);
    }
    
    //Check preconditions
    //FIXME, shouldn't this use safeCopy??
    if (!ts) return false; //kernel will return EFAULT
    if (ts->tv_sec < 0 || ts->tv_nsec < 0 || ts->tv_nsec > 999999999) return false; //kernel will return EINVAL
    
    uint64_t waitNsec = timespecToNs(*ts); 
    if (waitNsec >= offsetNsec) waitNsec -= offsetNsec;
    else waitNsec = 0;

    PIN_SaveContext(ctxt, &preSyscallCtxts[tid]);
    
    uint64_t waitCycles = nsToCycles(waitNsec);
    uint64_t waitPhases = waitCycles/zinfo->phaseLength;
    uint64_t wakeupPhase = zinfo->numPhases + waitPhases;

    volatile uint32_t* futexWord = zinfo->sched->markForSleep(procIdx, tid, wakeupPhase);
    sysEmuScratch[tid] = wakeupPhase;

    //Turn this into a non-timed FUTEX_WAIT syscall
    PIN_SetSyscallNumber(ctxt, std, SYS_futex);
    PIN_SetSyscallArgument(ctxt, std, 0, (ADDRINT)futexWord);
    PIN_SetSyscallArgument(ctxt, std, 1, (ADDRINT)FUTEX_WAIT);
    PIN_SetSyscallArgument(ctxt, std, 2, (ADDRINT)1 /*by convention, see sched code*/);
    PIN_SetSyscallArgument(ctxt, std, 3, (ADDRINT)NULL);
    return true;
}

static void postPatchNanosleep(uint32_t tid, CONTEXT* ctxt, SYSCALL_STANDARD std, bool isClock) {
    if (isClock) {
        trace(TimeVirt, "[%d] Post-patching SYS_clock_nanosleep", tid);
    } else {
        trace(TimeVirt, "[%d] Post-patching SYS_nanosleep", tid);
    }

    int res = (int)(-PIN_GetSyscallNumber(ctxt, std));
    if (res == EWOULDBLOCK) {
        trace(TimeVirt, "Fixing EWOULDBLOCK --> 0");
        PIN_SetSyscallNumber(ctxt, std, 0); //this is fine, you just called a very very short sleep
    } else if (res == EINTR) {
        PIN_SetSyscallNumber(ctxt, std, -EINTR); //we got an interrupt
    } else {
        trace(TimeVirt, "%d", res);
        assert(res == 0);
    }

    //Restore pre-call args
    for (uint32_t i = 0; i <= 3; i++) {
        PIN_SetSyscallArgument(ctxt, std, i, PIN_GetSyscallArgument(&preSyscallCtxts[tid], std, i));
    }

    //Handle remaining time stuff
    struct timespec* rem = (struct timespec*) PIN_GetSyscallArgument(&preSyscallCtxts[tid], std, isClock? 3 : 1);

    if (rem) {
        if (res == EINTR) {
            uint64_t wakeupPhase = sysEmuScratch[tid];
            assert(wakeupPhase >= zinfo->numPhases); //o/w why is this EINTR...
            uint64_t remainingCycles = wakeupPhase - zinfo->numPhases;
            uint64_t remainingNsecs = remainingCycles*1000/zinfo->freqMHz;
            rem->tv_sec = remainingNsecs/1000000000;
            rem->tv_nsec = remainingNsecs % 1000000000;
        } else {
            rem->tv_sec = 0;
            rem->tv_nsec = 0;
        }
    }
}

/* Timeout-syscalls emulation follows. */

struct timespec fakeTimeouts[MAX_THREADS]; //for syscalls that use timespec to indicate a timeout
bool inFakeTimeoutMode[MAX_THREADS];

static int getTimeoutArg(int syscall) {
    if (syscall == SYS_poll) return 2;
    return 3; //futex, epoll_wait, epoll_pwait
}


static bool prePatchTimeoutSyscall(uint32_t tid, CONTEXT* ctxt, SYSCALL_STANDARD std) {
    assert(!inFakeTimeoutMode[tid]); //canary: this will probably fail...
    int64_t waitNsec = 0;
    PIN_SaveContext(ctxt, &preSyscallCtxts[tid]);

    //Per-syscall manipulation. This code either succeeds, fakes timeout value and sets waitNsec, or returns false
    int syscall = PIN_GetSyscallNumber(ctxt, std);
    int timeoutArg = getTimeoutArg(syscall);
    if (syscall == SYS_futex) {
        //Check preconditions
        assert(timeoutArg == 3);
        int* uaddr = (int*) PIN_GetSyscallArgument(ctxt, std, 0);
        int op = (int) PIN_GetSyscallArgument(ctxt, std, 1);
        //int val = (int) PIN_GetSyscallArgument(ctxt, std, 2);
        const struct timespec* timeout = (const struct timespec*) PIN_GetSyscallArgument(ctxt, std, 3);

        if (!(uaddr && isFutexWaitOp(op) && timeout)) return false; //not a timeout FUTEX_WAIT
        
        waitNsec = timeout->tv_sec*1000000000L + timeout->tv_nsec;
        if (waitNsec <= 0) return false; //while technically waiting, this does not block. I'm guessing this is done for trylocks? It's weird.

        fakeTimeouts[tid].tv_sec = 0;
        fakeTimeouts[tid].tv_nsec = 20*1000*1000; //timeout every 20ms of actual host time
        PIN_SetSyscallArgument(ctxt, std, 3, (ADDRINT)&fakeTimeouts[tid]);
    } else {
        assert(syscall == SYS_epoll_wait || syscall == SYS_epoll_pwait || syscall == SYS_poll);
        int timeout = (int) PIN_GetSyscallArgument(ctxt, std, timeoutArg);
        if (timeout <= 0) return false;
        //info("[%d] pre-patch epoll_wait/pwait", tid);

        PIN_SetSyscallArgument(ctxt, std, timeoutArg, 20); //20ms timeout
        waitNsec = ((uint64_t)timeout)*1000*1000; //timeout is in ms
    }

    //info("[%d] pre-patch %s (%d) waitNsec = %ld", tid, syscallNames[syscall], syscall, waitNsec);

    uint64_t waitCycles = waitNsec*zinfo->freqMHz/1000;
    uint64_t waitPhases = waitCycles/zinfo->phaseLength;
    if (waitPhases < 2) waitPhases = 2; //at least wait 2 phases; this should basically eliminate the chance that we get a SIGSYS before we start executing the syscal instruction
    uint64_t wakeupPhase = zinfo->numPhases + waitPhases;

    /*volatile uint32_t* futexWord =*/ zinfo->sched->markForSleep(procIdx, tid, wakeupPhase); //we still want to mark for sleep, bear with me...
    inFakeTimeoutMode[tid] = true;
    return true;
}

static bool postPatchTimeoutSyscall(uint32_t tid, CONTEXT* ctxt, SYSCALL_STANDARD std) {
    assert(inFakeTimeoutMode[tid]);
    //Allright, the syscall finished folks
    int syscall = PIN_GetSyscallNumber(&preSyscallCtxts[tid], std);
    int res = (int)PIN_GetSyscallNumber(ctxt, std);

    //1. Decide if it timed out
    bool timedOut;
    if (syscall == SYS_futex) {
        timedOut = (res == -ETIMEDOUT);
    } else {
        timedOut = (res == 0);
    }

    bool isSleeping = zinfo->sched->isSleeping(procIdx, tid);
    
    //Decide whether to retry
    bool retrySyscall;
    if (!timedOut) {
        if (isSleeping) zinfo->sched->notifySleepEnd(procIdx, tid);
        retrySyscall = false;
    } else {
        retrySyscall = isSleeping;
    }

    if (retrySyscall && zinfo->procArray[procIdx]->isInFastForward()) {
        warn("[%d] Fast-forwarding started, not retrying timeout syscall (%s)", tid, syscallNames[syscall]);
        retrySyscall = false;
        assert(isSleeping);
        zinfo->sched->notifySleepEnd(procIdx, tid);
    }

    if (retrySyscall) {
        //ADDRINT curIp = PIN_GetContextReg(ctxt, REG_INST_PTR);
        ADDRINT prevIp = PIN_GetContextReg(&preSyscallCtxts[tid], REG_INST_PTR);
        //info("[%d] post-patch, retrying, IP: 0x%lx -> 0x%lx", tid, curIp, prevIp);
        PIN_SetContextReg(ctxt, REG_INST_PTR, prevIp);
        PIN_SetSyscallNumber(ctxt, std, syscall);
    } else {
        //Restore timeout arg
        int timeoutArg = getTimeoutArg(syscall);
        PIN_SetSyscallArgument(ctxt, std, timeoutArg, PIN_GetSyscallArgument(&preSyscallCtxts[tid], std, timeoutArg));
        inFakeTimeoutMode[tid] = false;

        //Restore arg? I don't think we need this!
        /*if (syscall == SYS_futex) {
            PIN_SetSyscallNumber(ctxt, std, -ETIMEDOUT);
        } else {
            assert(syscall == SYS_epoll_wait || syscall == SYS_epoll_pwait || syscall == SYS_poll);
            PIN_SetSyscallNumber(ctxt, std, 0); //no events returned
        }*/
    }

    //info("[%d] post-patch %s (%d), timedOut %d, sleeping (orig) %d, retrying %d, orig res %d, patched res %d", tid, syscallNames[syscall], syscall, timedOut, isSleeping, retrySyscall, res, (int)PIN_GetSyscallNumber(ctxt, std));
    return retrySyscall;
}

static void prePatchSanityCheck(uint32_t tid) {}


void TimeVirt_CaptureClocks(bool isDeffwd) {
    uint32_t domain = zinfo->procArray[procIdx]->getClockDomain();
    ClockDomainInfo& dom = zinfo->clockDomainInfo[domain];
    futex_lock(&dom.lock);
    if (isDeffwd || dom.realtimeOffsetNs == 0) {
        info("[%d] Adjusting clocks, domain %d, de-ffwd %d", procIdx, domain, isDeffwd);

        struct timespec realtime;
        struct timespec monotonic;
        struct timespec process;
        clock_gettime(CLOCK_REALTIME, &realtime);
        clock_gettime(CLOCK_MONOTONIC, &monotonic);
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &process);
        uint64_t realRdtsc = rdtsc();

        uint64_t curCycles = zinfo->globPhaseCycles;
        uint64_t curNs = cyclesToNs(curCycles);

        uint64_t realtimeNs = timespecToNs(realtime);
        uint64_t monotonicNs = timespecToNs(monotonic);
        uint64_t processNs = timespecToNs(process);

        dom.realtimeOffsetNs = realtimeNs - curNs;
        dom.monotonicOffsetNs = monotonicNs - curNs;
        dom.processOffsetNs = processNs - curNs;
        dom.rdtscOffset = realRdtsc - curCycles;

        info("Offsets: %ld %ld %ld %ld", dom.realtimeOffsetNs, dom.monotonicOffsetNs, dom.processOffsetNs, dom.rdtscOffset)
    }
    futex_unlock(&dom.lock);
}

uint64_t TimeVirt_GetPhaseRDTSC() {
    uint32_t domain = zinfo->procArray[procIdx]->getClockDomain();
    return zinfo->clockDomainInfo[domain].rdtscOffset + zinfo->globPhaseCycles;
}


