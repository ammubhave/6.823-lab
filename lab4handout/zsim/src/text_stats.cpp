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

#include "zsim.h"
#include "stats.h"
#include "galloc.h"
#include "log.h"

#include <iostream>
#include <fstream>

using namespace std;

class TextBackendImpl : public GlobAlloc {
    private:
        const char* filename;
        AggregateStat* rootStat;

        void dumpStat(Stat* s, uint32_t level, std::ofstream* out) {
            for (uint32_t i = 0; i < level; i++) *out << " ";
            *out << s->name() << ": ";
            if (AggregateStat* as = dynamic_cast<AggregateStat*>(s)) {
                *out << "# " << as->desc() << endl;
                for (uint32_t i = 0; i < as->size(); i++) {
                    dumpStat(as->get(i), level+1, out);
                }
            } else if (Counter* cs = dynamic_cast<Counter*>(s)) {
                *out << cs->count() << " # " << cs->desc() << endl;
            } else if (ScalarStat* ss = dynamic_cast<ScalarStat*>(s)) {
                *out << ss->get() << " # " << ss->desc() << endl;
            } else if (VectorCounter* vs = dynamic_cast<VectorCounter*>(s)) {
                *out << "# " << vs->desc() << endl;
                for (uint32_t i = 0; i < vs->size(); i++) {
                    for (uint32_t j = 0; j < level+1; j++) *out << " ";
                    if (vs->hasCounterNames()) {
                        *out << vs->counterName(i) << ": " << vs->count(i) << endl;
                    } else {
                        *out << i << ": " << vs->count(i) << endl;
                    }
                }
            } else if (ProxyStat* ps = dynamic_cast<ProxyStat*>(s)) {
                *out << ps->stat() << " # " << ps->desc() << endl;
            } else if (ProxyFuncStat* pfs = dynamic_cast<ProxyFuncStat*>(s)) {
                *out << pfs->stat() << " # " << pfs->desc() << endl;
            } else if (ProxyVectorStat* pvs = dynamic_cast<ProxyVectorStat*>(s)) {
                *out << "# " << pvs->desc() << endl;
                for (uint32_t i = 0; i < pvs->size(); i++) {
                    for (uint32_t j = 0; j < level+1; j++) *out << " ";
                    if (pvs->hasCounterNames()) {
                        *out << pvs->counterName(i) << ": " << pvs->count(i) << endl;
                    } else {
                        *out << i << ": " << pvs->count(i) << endl;
                    }
                }
            } else {
                panic("Unrecognized stat type");
            }
        }
    public:
        TextBackendImpl(const char* _filename, AggregateStat* _rootStat) :
            filename(_filename), rootStat(_rootStat)
        {
            std::ofstream out(filename, ios_base::out);
            out << "# zsim stats" << endl;
            out << "===" << endl;
        }

        void dump(bool buffered) {
            std::ofstream out(filename, ios_base::app);
            dumpStat(rootStat, 0, &out);
            out << "===" << endl;
        }

};

TextBackend::TextBackend(const char* filename, AggregateStat* rootStat) {
    backend = new TextBackendImpl(filename, rootStat);
}

void TextBackend::dump(bool buffered) {
    backend->dump(buffered);
}

