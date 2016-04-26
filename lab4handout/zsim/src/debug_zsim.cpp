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

#include "debug_zsim.h"
#include "log.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <fcntl.h>
#include <link.h>
#include <gelf.h>

#include <signal.h>

/* This file is pretty much self-contained, and has minimal external dependencies.
 * Please keep it this way, and ESPECIALLY don't include Pin headers since there
 * seem to be conflicts between those and some system headers.
 */

static int pp_callback(struct dl_phdr_info *info, size_t size, void *data) {
    if (strstr(info->dlpi_name, "libzsim.so")) {
        int fd;
        Elf* e;
        Elf_Scn* scn;
        if ((fd = open (info->dlpi_name, O_RDONLY , 0)) < 0)
            panic("Opening %s failed", info->dlpi_name);
        elf_version(EV_CURRENT);
        if ((e = elf_begin(fd, ELF_C_READ, NULL)) == NULL)
            panic("elf_begin() failed");
        size_t shstrndx; //we need this to get the section names
        if (elf_getshdrstrndx(e, &shstrndx) != 0)
            panic("elf_getshdrstrndx() failed");
        
        struct LibInfo* offsets = (struct LibInfo*) data;
        offsets->textAddr = NULL;
        offsets->dataAddr = NULL;
        offsets->bssAddr = NULL;

        scn = NULL;
        while ((scn = elf_nextscn(e, scn)) != NULL) {
            GElf_Shdr shdr;
            if (gelf_getshdr(scn, &shdr) != &shdr)
                panic("gelf_getshdr() failed");
            char* name = elf_strptr (e, shstrndx , shdr.sh_name);
            //info("Section %s %lx %lx", name, shdr.sh_addr, shdr.sh_offset);
            //info("Section %s %lx %lx\n", name, info->dlpi_addr + shdr.sh_addr, info->dlpi_addr + shdr.sh_offset);
            void* sectionAddr = (void*) (info->dlpi_addr + shdr.sh_addr);
            if (strcmp(".text", name) == 0) {
                offsets->textAddr = sectionAddr;
            } else if (strcmp(".data", name) == 0) {
                offsets->dataAddr = sectionAddr;
            } else if (strcmp(".bss", name) == 0) {
                offsets->bssAddr = sectionAddr;
            }
        }
        elf_end(e);
        close(fd);

        //Check that we got all the section addresses; it'd be extremely weird if we didn't
        assert(offsets->textAddr && offsets->dataAddr && offsets->bssAddr);

        return 1; //stops iterating
    }
    return 0; //continues iterating
}

void getLibzsimAddrs(struct LibInfo* libzsimAddrs) {
    int ret = dl_iterate_phdr(pp_callback, libzsimAddrs);
    if (ret != 1) panic("libzsim.so not found");
}


void notifyHarnessForDebugger(int harnessPid) {
    kill(harnessPid, SIGUSR1);
    sleep(1); //this is a bit of a hack, but ensures the debugger catches us
}
