// -*- mode:c++; c-style:k&r; c-basic-offset:4; indent-tabs-mode: nil; -*-
// vi:set et cin sw=4 cino=>se0n0f0{0}0^0\:0=sl1g0hspst0+sc3C0/0(0u0U0w0m0:

#include "main_memory.h"

MainMemory::MainMemory(const char *_name, uint64_t _latency) : name(_name), latency(_latency) { }

uint64_t MainMemory::load(Address wordAddr) {
    return latency;
}

uint64_t MainMemory::store(Address wordAddr) {
    return latency;
}

