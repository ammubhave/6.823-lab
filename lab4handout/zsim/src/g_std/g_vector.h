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

#ifndef __G_VECTOR_H__
#define __G_VECTOR_H__

#include <vector>
#include "g_std/stl_galloc.h"

// Supposedly, this will work in C++2011
//template <typename T> typedef std::vector<T, StlGlobAlloc<T> > g_vector;

// Until GCC is compliant with this, just inherit:
template <typename T> class g_vector : public std::vector<T, StlGlobAlloc<T> > {
    public:
        g_vector() = default;
        
        g_vector(const std::vector<T>& v) {
            this->resize(v.size());
            for (size_t i = 0; i < v.size(); i++) {
                (*this)[i] = v[i];
            }
        }
};

/* Some pointers on template typedefs:
 * http://www.gotw.ca/gotw/079.htm
 * http://drdobbs.com/cpp/184403850
 * http://gcc.gnu.org/ml/gcc-help/2007-04/msg00338.html
 */

#endif /*__G_VECTOR_H__*/
