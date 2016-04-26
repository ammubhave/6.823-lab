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

#ifndef __MEMORY_HIERARCHY_H__
#define __MEMORY_HIERARCHY_H__

/* Type and interface definitions of memory hierarchy objects */

#include <stdint.h>
#include "galloc.h"
#include "g_std/g_vector.h"
#include "locks.h"

/** TYPES **/

/* Addresses are plain 64-bit uints. This should be kept compatible with PIN addrints */
typedef uint64_t Address; 

class Memory {
    public:
        // returns the number of cycles taken
        virtual uint64_t load(Address wordAddr) = 0;
        // returns the number of cycles taken
        virtual uint64_t store(Address wordAddr) = 0;

        // returns nothing, we assume writebacks are off the critical path
        virtual void writeback(Address lineAddr) = 0;

        // returns nothing, we assume invalidate are off the critical path
        virtual void invalidate(Address lineAddr) = 0;

};

#endif
