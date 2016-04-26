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

#include <string.h>
#include <stdlib.h>
#include "log.h"
#include "locks.h"

const char* logHeader = "";

const char* logTypeNames[] = {"Harness", "Config", "Process", "Cache", "Mem", "Sched", "FSVirt", "TimeVirt"};

FILE* logFdOut = stdout;
FILE* logFdErr = stderr;

static lock_t log_printLock;


void InitLog(const char* header, const char* file) {
    char* logHdr = (char*) calloc(strlen(header)+1, sizeof(char));
    strcpy(logHdr, header);
    logHeader = logHdr;
    futex_init(&log_printLock);

    if (file) {
        FILE* fd = fopen(file, "a");
        if (fd == NULL) {
            perror("fopen() failed");
            panic("Could not open logfile %s", file); //we can panic in InitLog (will dump to stderr)
        }
        logFdOut = fd;
        logFdErr = fd;
        //NOTE: We technically never close this fd, but always flush it
    }
}

void __log_lock() {futex_lock(&log_printLock);}
void __log_unlock() {futex_unlock(&log_printLock);}

