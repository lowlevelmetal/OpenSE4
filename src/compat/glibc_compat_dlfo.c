/* _dl_find_object (glibc 2.35) for glibc 2.34, see glibc_compat.c.
 *
 * The C++ exception unwinder in the static libgcc asks it which loaded object
 * holds a code address and where that object's exception tables are. This
 * version answers from dl_iterate_phdr, which every glibc has.
 *
 * The tables are the segment glibc names in DLFO_EH_SEGMENT_TYPE: the
 * .eh_frame_hdr (PT_GNU_EH_FRAME) on x86_64 and aarch64, the exception index
 * table (PT_ARM_EXIDX, 8-byte entries, counted in dlfo_eh_count) on 32-bit ARM. */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <link.h>
#include <stddef.h>
#include <stdint.h>

#if DLFO_STRUCT_HAS_EH_DBASE
#error "glibc_compat_dlfo.c: this architecture's dl_find_object needs dlfo_eh_dbase"
#endif

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
    size_t ehBytes = 0;
    int contains = 0;
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr)* p = &info->dlpi_phdr[i];
        const uintptr_t from = info->dlpi_addr + p->p_vaddr;
        if (p->p_type == PT_LOAD) {
            const uintptr_t to = from + p->p_memsz;
            if (from < start) start = from;
            if (to > end) end = to;
            if (l->address >= from && l->address < to) contains = 1;
        } else if (p->p_type == DLFO_EH_SEGMENT_TYPE) {
            ehFrame = (void*)from;
            ehBytes = p->p_memsz;
        }
    }
    if (!contains) return 0;
    l->result->dlfo_flags = 0;
    l->result->dlfo_map_start = (void*)start;
    l->result->dlfo_map_end = (void*)end;
    l->result->dlfo_link_map = NULL;
    l->result->dlfo_eh_frame = ehFrame;
#if DLFO_STRUCT_HAS_EH_COUNT
    l->result->dlfo_eh_count = (int)(ehBytes / 8);
#else
    (void)ehBytes;
#endif
    l->result->dlfo_sframe = NULL;
    l->found = 1;
    return 1;
}

int __wrap__dl_find_object(void* address, struct dl_find_object* result) {
    struct lookup l = {(uintptr_t)address, result, 0};
    dl_iterate_phdr(visit, &l);
    return l.found ? 0 : -1;
}
