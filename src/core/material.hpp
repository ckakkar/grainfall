#pragma once
// Material catalogue. This file is the shared contract between the
// simulation, the save format, the scenes and the renderer: changing the
// enum order changes the save format, so only append new materials.

#include <array>
#include <cstdint>
#include <limits>
#include <string_view>

namespace gf {

enum class Material : std::uint8_t {
    Empty = 0,
    Stone,      // immovable solid, very heat resistant, melts to lava at ~1200 C
    Sand,       // powder; melts to lava at very high heat
    Water,      // liquid; boils to steam > 100 C, freezes to ice < 0 C
    Oil,        // light liquid, floats on water, very flammable
    Lava,       // dense, very hot liquid; cools into stone
    Ice,        // immovable solid; melts into water, chills neighbours
    Steam,      // gas; condenses back into water as it cools
    Smoke,      // gas; fades out over its lifetime
    Fire,       // short-lived hot gas; ignites flammable neighbours
    Wood,       // immovable solid; burns slowly
    Plant,      // immovable solid; grows into adjacent water; burns
    Acid,       // liquid; dissolves most things it touches (not stone/metal)
    Gunpowder,  // powder; explodes when ignited
    Metal,      // immovable solid; conducts heat extremely well, indestructible
    Count
};

inline constexpr int kMaterialCount = static_cast<int>(Material::Count);

enum class Phase : std::uint8_t {
    Empty,   // nothing there
    Static,  // never moves on its own (stone, wood, metal, ice, plant)
    Powder,  // falls, piles, slides diagonally (sand, gunpowder)
    Liquid,  // falls, spreads sideways, layers by density
    Gas,     // rises, drifts sideways, layers by density (lighter rises)
};

inline constexpr float kNever = std::numeric_limits<float>::infinity();

struct MaterialInfo {
    std::string_view name;
    Phase phase;
    float density;        // relative; denser things sink through lighter movable things
    float conductivity;   // 0..1, how fast heat flows through this material
    float spawn_temp;     // temperature (C) a freshly placed cell starts at
    std::uint32_t color;  // 0xRRGGBB base color
    std::uint8_t jitter;  // per-cell brightness variation applied by the renderer
    float flammability;   // 0..1 per-tick chance to ignite when next to fire / above ignite_temp
    float ignite_temp;    // C at which the cell may catch fire (kNever = never)
    std::int16_t life_min;  // lifetime range in ticks for transient materials;
    std::int16_t life_max;  // 0/0 = lives forever
    std::uint8_t dispersion;  // liquids/gases: max sideways cells moved per tick
};

// Lookup by material. Always valid for 0 <= m < Count.
const MaterialInfo& info(Material m);
const std::array<MaterialInfo, kMaterialCount>& material_table();

inline Phase phase_of(Material m) { return info(m).phase; }
inline bool is_movable(Material m) {
    const Phase p = phase_of(m);
    return p == Phase::Powder || p == Phase::Liquid || p == Phase::Gas;
}
inline std::string_view name_of(Material m) { return info(m).name; }

}  // namespace gf
