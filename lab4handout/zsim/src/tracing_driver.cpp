#include <cstdlib>
#include <ctime>
#include "tracing_core.h"
#include "tracing_driver.h"
#include "prio_queue.h"
#include "zsim.h"

TracingDriver::TracingDriver() {
    for (uint32_t c = 0; c < zinfo->numCores; c++) {
        pq.enqueue(new CoreEvent(c), 0);
    }
}

void TracingDriver::simPhase() {

    //info("Simulating phase %ld", zinfo->numPhases);

    TracingCore* cores[zinfo->numCores];
    Memory* l1ds[zinfo->numCores];
    Memory* l1is[zinfo->numCores];
    CircBuffer<TraceEntry>* tBufs[zinfo->numCores];
    for (uint32_t c = 0; c < zinfo->numCores; c++) {
        TracingCore* core = (TracingCore*)zinfo->cores[c];
        cores[c] = core;
        l1ds[c] = core->getL1d();
        l1is[c] = core->getL1i();
        tBufs[c] = &core->getTraceBuf();
        //info("%d", tBufs[c]->elems());
    }
    
    uint64_t startCycle = zinfo->numPhases*zinfo->phaseLength;
    uint64_t endCycle = startCycle + zinfo->phaseLength;
    
    //Hack: Skip simulating phase 0; processes will join at the first phase boundary
    //if (zinfo->numPhases < 100) return;

    while (pq.size() && pq.firstCycle() < endCycle) {
        uint64_t cycle = 0;
        CoreEvent* ev = pq.dequeue(cycle);
        uint32_t c = ev->core;
        TraceEntry e;
        if (tBufs[c]->pop(e)) {
            uint32_t lat = 0;
            switch (e.type) {
                case TR_LD: lat = l1ds[c]->load(e.addr); break;
                case TR_ST: lat = l1ds[c]->store(e.addr); break;
                case TR_BBL:
                    for (Address a = e.addr; a < e.addr + e.bytes; a += (1<<lineBits)) {
                        lat += l1is[c]->load(a); // once per line
                    }
                    lat += e.instrs;
                    cores[c]->addInstrs(e.instrs);
                    break;
                default: panic("invalid trace entry type");
            }

            cores[c]->addCycles(lat);
            pq.enqueue(ev, cycle + lat);
        } else {
            info("Done simulating core %d", c);
            //cores[c]->markDone();
        }
    }
}

