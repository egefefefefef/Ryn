#include "ryn_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *ryn_mem_alloc(uint64_t size) {
    size_t requested = size == 0 ? 1 : (size_t)size;
#if UINT64_MAX > SIZE_MAX
    if (size > (uint64_t)SIZE_MAX) {
        ryn_panic("allocation is too large for this platform");
    }
#endif
    void *pointer = malloc(requested);
    if (!pointer) {
        char message[96];
        snprintf(message, sizeof(message), "out of memory allocating %llu bytes",
                 (unsigned long long)size);
        ryn_panic(message);
    }
    return pointer;
}

void ryn_mem_free(void *pointer) {
    free(pointer);
}

void *ryn_mem_realloc(void *pointer, uint64_t size) {
    size_t requested = size == 0 ? 1 : (size_t)size;
    void *grown = realloc(pointer, requested);
    if (!grown) {
        ryn_panic("out of memory while resizing an allocation");
    }
    return grown;
}

void ryn_mem_copy(void *destination, const void *source, uint64_t size) {
    if (size == 0) {
        return;
    }
    if (!destination || !source) {
        ryn_panic("mem.copy with a null pointer");
    }
    memmove(destination, source, (size_t)size);
}

void ryn_mem_zero(void *destination, uint64_t size) {
    if (size == 0) {
        return;
    }
    if (!destination) {
        ryn_panic("mem.zero with a null pointer");
    }
    memset(destination, 0, (size_t)size);
}
