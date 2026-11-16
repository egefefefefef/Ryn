#include "ryn_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void ryn_str_free(const char *s) {
    free((void *)s);
}

uint64_t ryn_str_len(const char *s) {
    if (!s) {
        return 0;
    }
    return (uint64_t)strlen(s);
}

bool ryn_str_eq(const char *a, const char *b) {
    if (a == b) {
        return true;
    }
    if (!a || !b) {
        return false;
    }
    return strcmp(a, b) == 0;
}

const char *ryn_str_concat(const char *a, const char *b) {
    if (!a) {
        a = "";
    }
    if (!b) {
        b = "";
    }
    size_t length_a = strlen(a);
    size_t length_b = strlen(b);
    if (length_a > SIZE_MAX - length_b - 1) {
        ryn_panic("string concatenation is too large");
    }
    char *result = (char *)malloc(length_a + length_b + 1);
    if (!result) {
        ryn_panic("out of memory while concatenating strings");
    }
    memcpy(result, a, length_a);
    memcpy(result + length_a, b, length_b + 1);
    return result;
}

const char *ryn_str_from_int(int64_t value) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%lld", (long long)value);
    size_t length = strlen(buffer);
    char *result = (char *)malloc(length + 1);
    if (!result) {
        ryn_panic("out of memory while converting an integer");
    }
    memcpy(result, buffer, length + 1);
    return result;
}

uint8_t ryn_str_index(const char *s, uint64_t index) {
    if (!s) {
        ryn_panic("cannot index a null string");
    }
    uint64_t length = (uint64_t)strlen(s);
    if (index >= length) {
        char message[128];
        snprintf(message, sizeof(message), "string index %llu is out of bounds (length %llu)",
                 (unsigned long long)index, (unsigned long long)length);
        ryn_panic(message);
    }
    return (uint8_t)s[index];
}
