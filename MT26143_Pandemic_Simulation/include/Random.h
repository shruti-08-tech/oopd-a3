
#pragma once

#include <cstdint>

// Deterministic 64-bit hash.
inline std::uint64_t splitmix64(std::uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

// Produce one random value in [0,1).
// stream 0: infection
// stream 1: death
inline double rand01(
    std::uint64_t seed,
    std::uint64_t day,
    std::uint64_t cellId,
    std::uint64_t stream
) {
    std::uint64_t x = splitmix64(seed);
    x = splitmix64(x ^ day);
    x = splitmix64(x ^ cellId);
    x = splitmix64(x ^ stream);

    return static_cast<double>(x >> 11) *
           (1.0 / 9007199254740992.0);
}
