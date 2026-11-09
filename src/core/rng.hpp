#pragma once
// Small, fast, deterministic RNG (xoshiro256**, seeded via splitmix64).
// Every random decision in the engine must go through World::rng() so that a
// given seed plus a given sequence of edits always produces the same world.

#include <cstdint>

namespace gf {

class Rng {
public:
    explicit Rng(std::uint64_t seed = 0x9e3779b97f4a7c15ull) { reseed(seed); }

    void reseed(std::uint64_t seed) {
        for (auto& word : s_) word = splitmix(seed);
    }

    std::uint64_t next_u64() {
        const std::uint64_t result = rotl(s_[1] * 5, 7) * 9;
        const std::uint64_t t = s_[1] << 17;
        s_[2] ^= s_[0];
        s_[3] ^= s_[1];
        s_[1] ^= s_[2];
        s_[0] ^= s_[3];
        s_[2] ^= t;
        s_[3] = rotl(s_[3], 45);
        return result;
    }

    std::uint32_t next_u32() { return static_cast<std::uint32_t>(next_u64() >> 32); }

    // Uniform float in [0, 1).
    float uniform() { return static_cast<float>(next_u64() >> 40) * (1.0f / 16777216.0f); }

    // Uniform integer in [lo, hi] (inclusive). Requires lo <= hi.
    int range(int lo, int hi) {
        const auto span = static_cast<std::uint64_t>(static_cast<std::int64_t>(hi) - lo + 1);
        return lo + static_cast<int>(next_u64() % span);
    }

    bool chance(float p) { return uniform() < p; }
    bool coin() { return (next_u64() >> 63) != 0; }

private:
    static std::uint64_t rotl(std::uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    static std::uint64_t splitmix(std::uint64_t& x) {
        std::uint64_t z = (x += 0x9e3779b97f4a7c15ull);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }

    std::uint64_t s_[4]{};
};

}  // namespace gf
