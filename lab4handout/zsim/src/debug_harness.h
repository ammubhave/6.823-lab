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

#ifndef __DEBUG_HARNESS_H__
#define __DEBUG_HARNESS_H__

#include "debug.h"

/* Launch gdb automatically in a separate xterm window to debug the current process.
 * I'm doing this because I'm sick to death of debugging manually (wait 20 secs, attach
 * to PID, copy the libzsim.so symbol file command, etc etc).
 * Returns PID of children. Must be called from harness, since we can't fork from a pintool.
 */
int launchXtermDebugger(int targetPid, struct LibInfo* libzsimAddrs);

#endif /*__DEBUG_HARNESS_H__*/
