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

#include "debug_harness.h"
#include "log.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

//For funky macro stuff
#define QUOTED_(x) #x
#define QUOTED(x) QUOTED_(x)

/* This file is pretty much self-contained, and has minimal external dependencies.
 * Please keep it this way, and ESPECIALLY don't include Pin headers since there
 * seem to be conflicts between those and some system headers.
 */

int launchXtermDebugger(int targetPid, struct LibInfo* libzsimAddrs) {
    int childPid = fork();
    if (childPid == 0) {
        char targetPidStr[10];
        sprintf(targetPidStr, "%d", targetPid);
        char symbolCmdStr[2048];
        sprintf(symbolCmdStr, "add-symbol-file %s %p -s .data %p -s .bss %p", QUOTED(ZSIM_PATH), libzsimAddrs->textAddr, libzsimAddrs->dataAddr, libzsimAddrs->bssAddr);

        const char* const args[] = {"xterm", "-e", "gdb","-p", targetPidStr, 
            "-ex", "set confirm off", //we know what we're doing in the following 2 commands
            "-ex", symbolCmdStr,
            "-ex", "handle SIGTRAP nostop noprint", // For some reason we receive a lot of spurious sigtraps
            "-ex", "set confirm on", //reenable confirmations
            "-ex", "c", //start running
            NULL};
        execvp(args[0], (char* const*)args);
        panic("shouldn't reach this...");
    } else {
        return childPid;
    }
}


