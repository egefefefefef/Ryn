#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    const int64_t n = 50000;
    int32_t *a = malloc((size_t)n * sizeof(int32_t));
    for (int64_t i = 0; i < n; i++) {
        a[i] = (int32_t)(n - i);
    }
    for (int64_t j = 1; j < n; j++) {
        int32_t key = a[j];
        int64_t k = j - 1;
        while (k >= 0 && a[k] > key) {
            a[k + 1] = a[k];
            k--;
        }
        a[k + 1] = key;
    }
    printf("%d\n%d\n", a[0], a[n - 1]);
    free(a);
    return 0;
}
