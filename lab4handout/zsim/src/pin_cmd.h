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

/* Interface to get pin command line */

#include <stdint.h>

#include "galloc.h"
#include "g_std/g_vector.h"
#include "g_std/g_string.h"

class Config;

class PinCmd : public GlobAlloc {
    private:
        g_vector<g_string> args;

        struct ProcCmdInfo {
            g_string cmd;
            g_string input;
            g_string loader;
            g_string env;
        };

        g_vector<ProcCmdInfo> procInfo; //one entry for each process that the harness launches (not for child procs)

    public:
        PinCmd(Config* conf, const char* configFile, const char* outputDir, uint64_t shmid);
        g_vector<g_string> getPinCmdArgs(uint32_t procIdx);
        g_vector<g_string> getFullCmdArgs(uint32_t procIdx, const char** inputFile);
        void setEnvVars(uint32_t procIdx);

        uint32_t getNumCmdProcs() {return procInfo.size();}
};

