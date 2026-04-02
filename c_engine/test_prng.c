#include <stdio.h>
#include <stdint.h>
#include "prng.h"

int main(void) {
    Xoshiro256 rng;
    xoshiro256_seed(&rng, 42);

    uint64_t xor_hash = 0;

    printf("First 10 values (seed=42):\n");
    for (int i = 0; i < 10000; i++) {
        uint64_t v = xoshiro256_next(&rng);
        if (i < 10) {
            printf("  [%d] 0x%016llx\n", i, (unsigned long long)v);
        }
        xor_hash ^= v;
    }

    printf("\nXOR hash of 10000 values: 0x%016llx\n", (unsigned long long)xor_hash);

    // Test xoshiro256_uniform: generate 1000 values in [0, 6) and check range
    printf("\nTesting xoshiro256_uniform([0, 6)):\n");
    xoshiro256_seed(&rng, 42);
    int counts[6] = {0};
    int all_in_range = 1;
    for (int i = 0; i < 10000; i++) {
        uint32_t v = xoshiro256_uniform(&rng, 6);
        if (v >= 6) {
            all_in_range = 0;
            printf("  ERROR: got %u, expected [0, 6)\n", v);
        }
        counts[v]++;
    }

    if (all_in_range) {
        printf("  All 10000 values in range [0, 6) — OK\n");
    }
    printf("  Distribution: ");
    for (int i = 0; i < 6; i++) {
        printf("%d=%d ", i, counts[i]);
    }
    printf("\n");

    // Test uniform with n=1 (should always return 0)
    xoshiro256_seed(&rng, 42);
    int n1_ok = 1;
    for (int i = 0; i < 100; i++) {
        if (xoshiro256_uniform(&rng, 1) != 0) {
            n1_ok = 0;
            break;
        }
    }
    printf("\nxoshiro256_uniform(rng, 1) always 0: %s\n", n1_ok ? "OK" : "FAIL");

    return 0;
}
