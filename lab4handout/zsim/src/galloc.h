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

#ifndef _GALLOC_H_
#define _GALLOC_H_

#include <stdlib.h>
#include <string.h>

int gm_init(size_t segmentSize);

void gm_attach(int shmid); 

void* gm_calloc(size_t num, size_t size);
void* gm_malloc(size_t size);
void* gm_memalign(size_t blocksize, size_t bytes);
void gm_free(void* ptr);
char* gm_strdup(const char* str);

// Type-safe interface (I'm tired of typecasts)
template <typename T> T* gm_malloc() {return (T*) gm_malloc(sizeof(T));}
template <typename T> T* gm_malloc(size_t objs) {return (T*) gm_malloc(sizeof(T)*objs);}
template <typename T> T* gm_calloc() {return (T*) gm_calloc(1, sizeof(T));}
template <typename T> T* gm_calloc(size_t objs) {return (T*) gm_calloc(objs, sizeof(T));}
template <typename T> T* gm_memalign(size_t blocksize) {return (T*) gm_memalign(blocksize, sizeof(T));}
template <typename T> T* gm_memalign(size_t blocksize, size_t objs) {return (T*) gm_memalign(blocksize, sizeof(T)*objs);}
template <typename T> T* gm_dup(T* src, size_t objs) {
    T* dst = (T*) gm_malloc(sizeof(T)*objs);
    memcpy(dst, src, sizeof(T)*objs);
    return dst;
}

void gm_set_glob_ptr(void* ptr);
void* gm_get_glob_ptr();

void gm_set_secondary_ptr(void* ptr);
void* gm_get_secondary_ptr();

void gm_stats();

bool gm_isready();
void gm_detach();


class GlobAlloc {
    public:
        virtual ~GlobAlloc() {}

        inline void* operator new(size_t sz) {
            return gm_malloc(sz);
        }

        //Placement new
        inline void* operator new(size_t sz, void* ptr) {
            return ptr;
        }

        inline void operator delete(void *p, size_t sz) {
            gm_free(p);
        }
};

#endif

