// -*- mode:c++; c-style:k&r; c-basic-offset:4; indent-tabs-mode: nil; -*-
// vi:set et cin sw=4 cino=>se0n0f0{0}0^0\:0=sl1g0hspst0+sc3C0/0(0u0U0w0m0:

#ifndef __MAIN_MEMORY_H__
#define __MAIN_MEMORY_H__

#include "g_std/g_string.h"
#include "memory_hierarchy.h"

class MainMemory : public Memory {
    public:
        MainMemory(const char *name, uint64_t latency);

        virtual uint64_t load(Address wordAddr);
        virtual uint64_t store(Address wordAddr);
        void writeback(Address lineAddr) {}
        void invalidate(Address lineAddr) {}
    private:
        g_string name;
        uint64_t latency;
};

#endif //  __MAIN_MEMORY_H__

