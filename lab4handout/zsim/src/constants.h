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

#ifndef __CONSTANTS_H__
#define __CONSTANTS_H__

/* Simulator constants/limits go here, defined by macros */

// PIN 2.9 (rev39599) can't do more than 2048 threads...
#define MAX_THREADS (2048)

// How many children caches can each cache track? Note each bank is a separate child. This impacts sharer bit-vector sizes.
#define MAX_CACHE_CHILDREN (256)
//#define MAX_CACHE_CHILDREN (1024)

// Complex multiprocess runs need multiple clocks, and multiple port domains
#define MAX_CLOCK_DOMAINS (64)
#define MAX_PORT_DOMAINS (64)

#define MAX_IPC (4)

#endif /*__CONSTANTS_H__*/

