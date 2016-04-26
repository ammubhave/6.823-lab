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

/* Small utility to control ff toggling. */

#include <stdlib.h>
#include <string.h>
#include <sched.h>

#include "galloc.h"
#include "locks.h"
#include "log.h"
#include "zsim.h"

int main(int argc, char *argv[]) {
    InitLog("[T] ");
    if (argc < 3 || argc > 4) {
        info("Usage: %s <ff|pause|globpause|term> <shmid> [<procIdx>]", argv[0]);
        exit(1);
    }

    const char* cmd = argv[1];
    int shmid = atoi(argv[2]);
    int procIdx = (argc == 4)? atoi(argv[3]) : -1;

    gm_attach(shmid);
    while(!gm_isready()) sched_yield(); //wait till proc idx 0 initializes everything; sched_yield to avoid livelock with lots of processes
    GlobSimInfo* zinfo = (GlobSimInfo*) gm_get_glob_ptr();

    if (strcmp(cmd, "ff") == 0) {
        if (procIdx < 0) panic("ff needs procIdx");
        futex_unlock(&zinfo->ffToggleLocks[procIdx]);
        info("Toggled fast-forward on process %d", procIdx);
    } else if (strcmp(argv[1], "pause") == 0) {
        if (procIdx < 0) panic("pause needs procIdx");
        futex_unlock(&zinfo->pauseLocks[procIdx]);
        info("Unpaused process %d", procIdx);
    } else if (strcmp(argv[1], "globpause") == 0) {
        if (procIdx >= 0) warn("globpause pauses the whole simulation, you specified a procIdx");
        zinfo->globalPauseFlag = !zinfo->globalPauseFlag; //you will not be stupid enough to run multiple fftoggles at the same time.
        __sync_synchronize();
    } else if (strcmp(argv[1], "term") == 0) {
        if (procIdx >= 0) warn("term terminates the whole simulation, you specified a procIdx");
        zinfo->externalTermPending = true;
        __sync_synchronize();
        info("Marked simulation for termination");
    } else {
        panic("Invalid command: %s", cmd);
    }
    exit(0);
}

