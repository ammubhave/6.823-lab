#ifndef __PORT_VIRT_H__
#define __PORT_VIRT_H__

/* Simple class to keep tabs on virtualized ports */

#include "galloc.h"
#include "g_std/g_unordered_map.h"

class PortVirtualizer : public GlobAlloc {
    private:
        g_unordered_map<int, int> realToVirt;
        g_unordered_map<int, int> virtToReal;

        lock_t pvLock;

    public:
        PortVirtualizer() {
            futex_init(&pvLock);
        }

        //Must always lock before any operation, and unlock after!
        //lock() unlock() are external because bind() spans multiple methods
        void lock() { futex_lock(&pvLock); }
        void unlock() { futex_unlock(&pvLock); }

        //Note there's no error checking for a bind that binds on a previous one.
        //If someone previous bound to that port, the virtualization code should just go ahead with that mapping and 
        //either let bind() fail (if the previous bind is stil active) or succeed (if the previous bind ended)
        void registerBind(int virt, int real) {
            realToVirt[real] = virt;
            virtToReal[virt] = real;
        }

        //Returns -1 if not in map. For connect() and bind()
        int lookupReal(int virt) {
            g_unordered_map<int, int>::iterator it = virtToReal.find(virt);
            return (it == virtToReal.end())? -1 : it->second;
        }

        //Returns -1 if not in map. For getsockname(), where the OS returns real and we need virt
        int lookupVirt(int real) {
            g_unordered_map<int, int>::iterator it = realToVirt.find(real);
            return (it == realToVirt.end())? -1 : it->second;
        }
};

#endif /*__PORT_VIRT_H__*/
