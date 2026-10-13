#pragma once
// Deterministic PCG32 random generator: the same seed always gives the same sequence on every platform,
// which is what makes courses and drills shareable by code.

#include <cstdint>

namespace rlt {

class Rng {
public:
    explicit Rng(uint64_t seed = 0x853c49e6748fea9bULL, uint64_t stream = 0xda3e39cb94b95bdbULL) { reseed(seed, stream); }

    void reseed(uint64_t seed, uint64_t stream = 0xda3e39cb94b95bdbULL) {
        state_ = 0u;
        inc_ = (stream << 1u) | 1u;
        next();
        state_ += seed;
        next();
    }

    uint32_t next() {
        const uint64_t old = state_;
        state_ = old * 6364136223846793005ULL + inc_;
        const uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
        const uint32_t rot = static_cast<uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31u));
    }

    // Uniform float in [0, 1).
    float uniform() { return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); }
    // Uniform float in [lo, hi).
    float range(float lo, float hi) { return lo + (hi - lo) * uniform(); }
    // Uniform int in [lo, hi] (inclusive).
    int rangeInt(int lo, int hi) {
        if (hi <= lo) return lo;
        const uint32_t span = static_cast<uint32_t>(hi - lo) + 1u;
        return lo + static_cast<int>(next() % span);
    }
    bool chance(float p) { return uniform() < p; }

private:
    uint64_t state_ = 0;
    uint64_t inc_ = 0;
};

// Mixes a 64-bit value (SplitMix64 finaliser), handy to derive sub-seeds.
inline uint64_t mix64(uint64_t z) {
    z += 0x9e3779b97f4a7c15ULL;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

} // namespace rlt
