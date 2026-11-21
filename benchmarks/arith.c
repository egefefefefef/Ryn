#include <inttypes.h>
#include <stdio.h>

int main(void) {
    uint64_t x = 1;
    for (int64_t i = 0; i < 100000000; i++) {
        x = x * UINT64_C(6364136223846793005) +
            UINT64_C(1442695040888963407);
    }
    printf("%" PRIu64 "\n", x);
    return 0;
}
