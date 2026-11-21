#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    const int64_t n = 20000000;
    uint8_t *flags = malloc((size_t)n);
    for (int64_t i = 0; i < n; i++) {
        flags[i] = 1;
    }
    flags[0] = 0;
    flags[1] = 0;
    for (int64_t p = 2; p * p < n; p++) {
        if (flags[p] == 1) {
            for (int64_t m = p * p; m < n; m += p) {
                flags[m] = 0;
            }
        }
    }
    int64_t count = 0;
    for (int64_t i = 0; i < n; i++) {
        if (flags[i] == 1) {
            count++;
        }
    }
    printf("%" PRId64 "\n", count);
    free(flags);
    return 0;
}
