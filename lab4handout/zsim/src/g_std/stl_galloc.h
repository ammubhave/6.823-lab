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

#ifndef _STL_GALLOC_H
#define _STL_GALLOC_H

#include <iostream>
#include <stddef.h>
#include "galloc.h"

/* Follows interface of STL allocator, allocates and frees from the global heap */

template <class T>
class StlGlobAlloc {
    public:
        typedef size_t size_type;
        typedef ptrdiff_t difference_type;
        typedef T* pointer;
        typedef const T* const_pointer;
        typedef T& reference;
        typedef const T& const_reference;
        typedef T value_type;

        StlGlobAlloc() {}
        StlGlobAlloc(const StlGlobAlloc&) {}

        pointer allocate(size_type n, const void * = 0) {
            T* t = (T*) gm_calloc(n, sizeof(T));
            return t;
        }

        void deallocate(void* p, size_type) {
            if (p) {
                gm_free(p);
            }
        }

        pointer address(reference x) const { return &x; }
        const_pointer address(const_reference x) const { return &x; }
        StlGlobAlloc<T>& operator=(const StlGlobAlloc&) { return *this; }
        void construct(pointer p, const T& val) { new ((T*) p) T(val); }
        void construct(pointer p) { construct(p, value_type()); } //required by gcc 4.6
        void destroy(pointer p) { p->~T(); }

        size_type max_size() const { return size_t(-1); }

        template <class U> struct rebind { typedef StlGlobAlloc<U> other; };

        template <class U> StlGlobAlloc(const StlGlobAlloc<U>&) {}

        template <class U> StlGlobAlloc& operator=(const StlGlobAlloc<U>&) { return *this; }


        /* dsm: The == (and !=) operator in an allocator must be defined and,
         * per http://download.oracle.com/docs/cd/E19422-01/819-3703/15_3.htm :
         *
         *   Returns true if allocators b and a can be safely interchanged. Safely
         *   interchanged means that b could be used to deallocate storage obtained
         *   through a, and vice versa.
         *
         * We can ALWAYS do this, as deallocate just calls gm_free()
         */
        template <class U> bool operator==(const StlGlobAlloc<U>&) const {return true;}

        template <class U> bool operator!=(const StlGlobAlloc<U>&) const {return false;}
};

#endif

