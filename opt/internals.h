#ifndef INCLUDED_OPT_INTERNALS_H
#define INCLUDED_OPT_INTERNALS_H

#include <malloc.h>

inline uint32_t mem_heap_total() {
    extern char __StackLimit, __bss_end__;
    return &__StackLimit  - &__bss_end__;
}

inline uint32_t mem_heap_free() {
    struct mallinfo m = mallinfo();
    return mem_heap_total() - m.uordblks;
}

#endif //INCLUDED_OPT_INTERNALS_H
