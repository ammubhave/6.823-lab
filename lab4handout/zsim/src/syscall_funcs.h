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

#ifndef __SYSCALL_FUNCS_H__
#define __SYSCALL_FUNCS_H__

/* This file has routines for system call handling. It is tied to PIN and system headers.
 * You better run this on Linux, and keep it tied to a recent kernel.
 *
 * For now, it simply determines on which syscalls we should leave. In the future, we will
 * probably want to virtualize a few of them.
 */

#include "pin.H"

enum PostPatchAction {
    PPA_NOTHING,
    PPA_USE_NOP_PTRS,
    PPA_USE_JOIN_PTRS,
};

//Returns true if we should block on the system call
bool blockOnSyscall(CONTEXT *ctxt, SYSCALL_STANDARD std);

void syscallPatchPre(THREADID tid, CONTEXT *ctxt, SYSCALL_STANDARD std, const char* patchRoot, bool isNopThread);
PostPatchAction syscallPatchPost(THREADID tid, CONTEXT *ctxt, SYSCALL_STANDARD std);

//vdso patching
void patchGettimeofday(uint32_t tid, ADDRINT arg0);
void patchTime(uint32_t tid, REG* retVal, ADDRINT arg0);
void patchClock_gettime(uint32_t tid, ADDRINT arg0, ADDRINT arg1);
void patchGetcpu(uint32_t tid, ADDRINT arg0, ADDRINT arg1);

//Signal interception is needed in some cases
BOOL sigSysIntercept(THREADID tid, INT32 sig, CONTEXT *ctxt, BOOL hasHandler, const EXCEPTION_INFO *pExceptInfo, VOID *v);

// Time virtualization direct functions
void TimeVirt_CaptureClocks(bool isDeffwd); //called on start and ffwd to get these clocks together
uint64_t TimeVirt_GetPhaseRDTSC();


#endif /*__SYSCALL_FUNCS_H__*/
