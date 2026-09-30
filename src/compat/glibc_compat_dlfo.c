/* _dl_find_object (glibc 2.35) for glibc 2.34, see glibc_compat.c.
 *
 * The C++ exception unwinder in the static libgcc asks it which loaded object
 * holds a code address and where that object's exception tables are. This
 * version answers from dl_iterate_phdr, which every glibc has. */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <link.h>
#include <stddef.h>
#include <stdint.h>

struct lookup {
    uintptr_t address;
    struct dl_find_object* result;
    int found;
};

static int visit(struct dl_phdr_info* info, size_t size, void* data) {
    (void)size;
    struct lookup* l = data;
    uintptr_t start = UINTPTR_MAX, end = 0;
    void* ehFrame = NULL;
    int contains = 0;
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr)* p = &info->dlpi_phdr[i];
        const uintptr_t from = info->dlpi_addr + p->p_vaddr;
        if (p->p_type == PT_LOAD) {
            const uintptr_t to = from + p->p_memsz;
            if (from < start) start = from;
            if (to > end) end = to;
            if (l->address >= from && l->address < to) contains = 1;
        } else if (p->p_type == PT_GNU_EH_FRAME) {
            ehFrame = (void*)from;
        }
    }
    if (!contains) return 0;
    l->result->dlfo_flags = 0;
    l->result->dlfo_map_start = (void*)start;
    l->result->dlfo_map_end = (void*)end;
    l->result->dlfo_link_map = NULL;
    l->result->dlfo_eh_frame = ehFrame;
    l->result->dlfo_sframe = NULL;
    l->found = 1;
    return 1;
}

int __wrap__dl_find_object(void* address, struct dl_find_object* result) {
    struct lookup l = {(uintptr_t)address, result, 0};
    dl_iterate_phdr(visit, &l);
    return l.found ? 0 : -1;
}
