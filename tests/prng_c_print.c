#include <stdio.h>
#include <inttypes.h>
#include "prng.h"

int main(void) {
    Xoshiro256 rng;
    xoshiro256_seed(&rng, 42);
    for (int i = 0; i < 1000; i++) {
        printf("%016" PRIx64 "\n", xoshiro256_next(&rng));
    }
    return 0;
}
