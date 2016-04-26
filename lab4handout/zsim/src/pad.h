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

#ifndef _PAD_H_
#define _PAD_H_

/* Padding macros to remove false sharing */

//Line size, in chars (bytes). We could make it configurable through a define
#define CACHE_LINE_BYTES 64

#define _PAD_CONCAT(x,y) x ## y
#define PAD_CONCAT(x,y) _PAD_CONCAT(x,y)

#define PAD() unsigned char PAD_CONCAT(pad_line, __LINE__)[CACHE_LINE_BYTES] //assuming classes are defined over one file, this should generate unique names

//Pad remainder to line size, use as e.g. PAD(sizeof(uint32)) will produce 60B of padding
#define PAD_SZ(sz) unsigned char PAD_CONCAT(pad_sz_line, __LINE__)[CACHE_LINE_BYTES - ((sz) % CACHE_LINE_BYTES)]

#define ATTR_LINE_ALIGNED __attribute__(( aligned(CACHE_LINE_BYTES) )) 

#endif

