#ifndef __TRACING_DRIVER_H__
#define __TRACING_DRIVER_H__

#include "log.h"
#include "prio_queue.h"

class TracingDriver {
    private:
        struct CoreEvent {
            const uint32_t core;
            CoreEvent* next;

            CoreEvent(uint32_t c) : core(c), next(NULL) {}
        };
        PrioQueue<CoreEvent, 1024> pq;
    public:
        TracingDriver();
        void simPhase();
};

#endif /*__TRACING_DRIVER_H__*/
