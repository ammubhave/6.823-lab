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

#ifndef _HASH_H_
#define _HASH_H_

#include <stdint.h>
#include "galloc.h"

class HashFamily : public GlobAlloc {
    public:
        HashFamily() {};
        virtual ~HashFamily() {};

        virtual uint64_t hash(uint32_t id, uint64_t val)=0;
};

class H3HashFamily : public HashFamily {
    private:
        uint32_t numFuncs;
        uint32_t resShift;
        uint64_t* hMatrix;
    public:
        H3HashFamily(uint32_t numFunctions, uint32_t outputBits, uint64_t randSeed = 123132127);
        uint64_t hash(uint32_t id, uint64_t val);
};

class SHA1HashFamily : public HashFamily {
    private:
        int numFuncs;
        int numPasses;
        
        //SHA1 is quite expensive and returns large blocks, so we use memoization and chunk the block to implement hash function families.
        uint64_t memoizedVal;
        uint32_t* memoizedHashes;
    public:
        SHA1HashFamily(int numFunctions);
        uint64_t hash(uint32_t id, uint64_t val);
};

/* Used when we don't want hashing, just return the value */
class IdHashFamily : public HashFamily {
    public:
        inline uint64_t hash(uint32_t id, uint64_t val) {return val;}
};

#endif
