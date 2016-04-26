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

#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdint.h>
#include "galloc.h"
#include <stdlib.h>
#include <stdio.h>
#include "log.h"
#include "locks.h"
#include "pad.h"
#include <string.h>

#include "g_heap/dlmalloc.h.c"

/* Base heap address. Has to be available cross-process. With 64-bit virtual
 * addresses, the address space is so sparse that it's quite easy to find
 * some random base that always works in practice. If for some weird reason
 * you want to compile this on a 32-bit address space, there are fancier,
 * more structured ways to get a common range (e.g. launch all the processes
 * before allocating the global heap segment, and find a common range either
 * by brute-force scanning and communicating through pipes, or by parsing
 * /proc/{pid}/maps).
 *
 * But, since I'm using a 64-bit address space, I don't really care to make
 * it fancy.
 */
#define GM_BASE_ADDR ((const void*)0x00ABBA000000)

struct gm_segment {
    volatile void* base_regp; //common data structure, accessible with glob_ptr; threads poll on gm_isready to determine when everything has been initialized
    volatile void* secondary_regp; //secondary data structure, used to exchange information between harness and initializing process
    mspace mspace_ptr;

    PAD();
    lock_t lock;
    PAD();
};

static gm_segment* GM = NULL;
static int gm_shmid = 0;

/* Heap segment size, in bytes. Can't grow for now, so choose something sensible, and within the machine's limits (see sysctl vars kernel.shmmax and kernel.shmall) */
int gm_init(size_t segmentSize) {
    /* Create a SysV IPC shared memory segment, attach to it, and mark the segment to
     * auto-destroy when the number of attached processes becomes 0.
     *
     * IMPORTANT: There is a small window of vulnerability between shmget and shmctl that
     * can lead to major issues: between these calls, we have a segment of persistent
     * memory that will survive the program if it dies (e.g. someone just happens to send us
     * a SIGKILL)
     */

    assert(GM == NULL);
    assert(gm_shmid == 0);
    gm_shmid = shmget(IPC_PRIVATE, segmentSize, 0644 | IPC_CREAT /*| SHM_HUGETLB*/);
    if (gm_shmid == -1) {
        perror("gm_create failed shmget");
        exit(1);
    }
    GM = (gm_segment*) shmat(gm_shmid, GM_BASE_ADDR, 0);
    if (GM != GM_BASE_ADDR) {
        perror("gm_create failed shmat");
        warn("shmat failed, shmid %d. Trying not to leave garbage behind before dying...", gm_shmid);
        int ret = shmctl(gm_shmid, IPC_RMID, NULL);
        if (ret) {
            perror("shmctl failed, we're leaving garbage behind!");
            panic("Check /proc/sysvipc/shm and manually delete segment with shmid %d", gm_shmid);
        } else {
            panic("shmctl succeeded, we're dying in peace");
        }
    }

    //Mark the segment to auto-destroy when the number of attached processes becomes 0.
    int ret = shmctl(gm_shmid, IPC_RMID, NULL);
    assert(!ret);

    char* alloc_start = ((char*)GM) + 1024;
    size_t alloc_size = segmentSize - 1 - 1024;
    GM->base_regp = NULL;

    GM->mspace_ptr = create_mspace_with_base(alloc_start, alloc_size, 1 /*locked*/);
    futex_init(&GM->lock);
    assert(GM->mspace_ptr);

    return gm_shmid;
}

void gm_attach(int shmid) {
    assert(GM == NULL);
    assert(gm_shmid == 0);
    gm_shmid = shmid;
    GM = (gm_segment*) shmat(gm_shmid, GM_BASE_ADDR, 0);
    if (GM != GM_BASE_ADDR) {
        warn("shmid %d \n", shmid);
        panic("gm_attach failed allocation");
    }
}


void* gm_calloc(size_t num, size_t size) {
    assert(GM);
    assert(GM->mspace_ptr);
    futex_lock(&GM->lock);
    void* ptr = mspace_calloc(GM->mspace_ptr, num, size);
    futex_unlock(&GM->lock);
    if (!ptr) panic("gm_calloc(): Out of global heap memory, use a larger GM segment");
    return ptr;
}

void* gm_malloc(size_t size) {
    assert(GM);
    assert(GM->mspace_ptr);
    futex_lock(&GM->lock);
    void* ptr = mspace_malloc(GM->mspace_ptr, size);
    futex_unlock(&GM->lock);
    if (!ptr) panic("gm_malloc(): Out of global heap memory, use a larger GM segment");
    return ptr;
}

void* gm_memalign(size_t blocksize, size_t bytes) {
    assert(GM);
    assert(GM->mspace_ptr);
    futex_lock(&GM->lock);
    void* ptr = mspace_memalign(GM->mspace_ptr, blocksize, bytes);
    futex_unlock(&GM->lock);
    if (!ptr) panic("gm_memalign(): Out of global heap memory, use a larger GM segment");
    return ptr;
}


void gm_free(void* ptr) {
    assert(GM);
    assert(GM->mspace_ptr);
    futex_lock(&GM->lock);
    mspace_free(GM->mspace_ptr, ptr);
    futex_unlock(&GM->lock);
}


char* gm_strdup(const char* str) {
    char* res = (char*)gm_malloc(strlen(str) + 1);
    strcpy(res, str);
    return res;
}


void gm_set_glob_ptr(void* ptr) {
    assert(GM);
    assert(GM->base_regp == NULL);
    GM->base_regp = ptr;
}

void* gm_get_glob_ptr() {
    assert(GM);
    assert(GM->base_regp);
    return (void*)GM->base_regp;
}

void gm_set_secondary_ptr(void* ptr) {
    assert(GM);
    assert(GM->secondary_regp == NULL);
    GM->secondary_regp = ptr;
}

void* gm_get_secondary_ptr() {
    assert(GM);
    assert(GM->secondary_regp != NULL);
    return (void*)GM->secondary_regp;
}

void gm_stats() {
    assert(GM);
    mspace_malloc_stats(GM->mspace_ptr);
}

bool gm_isready() {
    assert(GM);
    return (GM->base_regp != NULL);
}

void gm_detach() {
    assert(GM);
    shmdt(GM);
    GM = NULL;
    gm_shmid = 0;
}


