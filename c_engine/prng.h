// prng.h — xoshiro256** with SplitMix64 seeding
// Header-only, extern "C" compatible

#ifndef PRNG_H
#define PRNG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { uint64_t s[4]; } Xoshiro256;

static inline uint64_t rotl(const uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

static inline uint64_t xoshiro256_next(Xoshiro256 *rng) {
    const uint64_t result = rotl(rng->s[1] * 5, 7) * 9;
    const uint64_t t = rng->s[1] << 17;
    rng->s[2] ^= rng->s[0];
    rng->s[3] ^= rng->s[1];
    rng->s[1] ^= rng->s[2];
    rng->s[0] ^= rng->s[3];
    rng->s[2] ^= t;
    rng->s[3] = rotl(rng->s[3], 45);
    return result;
}

static inline uint64_t splitmix64(uint64_t *state) {
    uint64_t z = (*state += 0x9e3779b97f4a7c15);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
    z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
    return z ^ (z >> 31);
}

static inline void xoshiro256_seed(Xoshiro256 *rng, uint64_t seed) {
    rng->s[0] = splitmix64(&seed);
    rng->s[1] = splitmix64(&seed);
    rng->s[2] = splitmix64(&seed);
    rng->s[3] = splitmix64(&seed);
}

// Uniform integer in [0, n) — rejection sampling, no modulo bias
static inline uint32_t xoshiro256_uniform(Xoshiro256 *rng, uint32_t n) {
    uint64_t r = xoshiro256_next(rng);
    uint64_t m = (uint64_t)n * (uint32_t)r;
    uint32_t l = (uint32_t)m;
    if (l < n) {
        uint32_t t = -n % n;
        while (l < t) {
            r = xoshiro256_next(rng);
            m = (uint64_t)n * (uint32_t)r;
            l = (uint32_t)m;
        }
    }
    return m >> 32;
}

#ifdef __cplusplus
}
#endif

#endif // PRNG_H
