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

/* General logging/info/warn/panic routines */

#ifndef __LOG_H__
#define __LOG_H__

#include <stdlib.h>
#include <stdio.h>

void __log_lock();
void __log_unlock();

#ifdef MT_SAFE_LOG
#define log_lock() __log_lock()
#define log_unlock() __log_unlock()
#else
#define log_lock()
#define log_unlock()
#endif

#define PANIC_EXIT_CODE (112)

// assertions are often frequently executed but never inlined. Might as well tell the compiler about it
#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)

typedef enum {
    LOG_Harness,
    LOG_Config,
    LOG_Process,
    LOG_Cache,
    LOG_Mem,
    LOG_Sched,
    LOG_FSVirt,
    LOG_TimeVirt,
} LogType;

// defined in log.cpp
extern const char* logTypeNames[];
extern const char* logHeader;
extern FILE* logFdOut;
extern FILE* logFdErr;

/* Set per-process header for log/info/warn/panic messages
 * Calling this is not needed (the default header is ""),
 * but it helps in multi-process runs
 * If file is NULL or InitLog is not called, logs to stdout/stderr 
 */
void InitLog(const char* header, const char* file = NULL);

#define panic(args...) \
{ \
    fprintf(logFdErr, "%sPanic on %s:%d: ", logHeader, __FILE__, __LINE__); \
    fprintf(logFdErr, args); \
    fprintf(logFdErr, "\n"); \
    fflush(logFdErr); \
    exit(PANIC_EXIT_CODE); \
}

#define warn(args...) \
{ \
    log_lock(); \
    fprintf(logFdErr, "%sWARN: ", logHeader); \
    fprintf(logFdErr, args); \
    fprintf(logFdErr, "\n"); \
    fflush(logFdErr); \
    log_unlock(); \
}

#define info(args...) \
{ \
    log_lock(); \
    fprintf(logFdOut, "%s", logHeader); \
    fprintf(logFdOut, args); \
    fprintf(logFdOut, "\n"); \
    fflush(logFdOut); \
    log_unlock(); \
}

/* I would call these macros log, but there's this useless math function
 * that happens to conflict with this...
 */
#ifdef _LOG_TRACE_
#define trace(type, args...) \
{ \
    log_lock(); \
    fprintf(logFdErr, "%sLOG(%s): ", logHeader, logTypeNames[(int) LOG_##type]); \
    fprintf(logFdErr, args); \
    fprintf(logFdErr, "\n"); \
    fflush(logFdErr); \
    log_unlock(); \
}
#else
#define trace(type, args...) ;
#endif

#ifndef NASSERT
#define assert(cond) \
if (unlikely(!(cond))) { \
    fprintf(logFdErr, "%sFailed assertion on %s:%d\n", logHeader, __FILE__, __LINE__); \
    fflush(logFdErr); \
    *((int*)0L) = 42; /*SIGSEGVs*/ \
    exit(1); \
};

#define assert_msg(cond, args...) \
if (unlikely(!(cond))) { \
    fprintf(logFdErr, "%sFailed assertion on %s:%d: ", logHeader, __FILE__, __LINE__); \
    fprintf(logFdErr, args); \
    fprintf(logFdErr, "\n"); \
    fflush(logFdErr); \
    *((int*)0L) = 42; /*SIGSEGVs*/ \
    exit(1); \
};
#else
#define assert(cond) ;
#define assert_msg(cond, args...) ;
#endif

#endif /*__LOG_H__*/
