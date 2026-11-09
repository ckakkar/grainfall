#pragma once
// The world grid. Owns cell storage, the tick counter and the RNG.
// Coordinates: x grows right, y grows DOWN (y = 0 is the top row).

#include <cstdint>
#include <vector>

#include "core/material.hpp"
#include "core/rng.hpp"

namespace gf {

struct Cell {
    Material mat{Material::Empty};
    std::uint8_t shade{0};   // fixed random 0..255 picked at creation; renderer uses it for texture
    std::uint8_t clock{0};   // simulation-owned: marks the tick a cell last moved (avoid double moves)
    std::uint8_t flags{0};   // simulation-owned bit flags (see CellFlags)
    std::int16_t life{0};    // remaining lifetime in ticks for transient materials (0 = forever)
    std::int16_t aux{0};     // simulation-owned scratch (e.g. growth / burn counters)
    float temp{20.0f};       // temperature in degrees C
};

// Bits for Cell::flags. The simulation may add more in the high bits.
enum CellFlags : std::uint8_t {
    kFlagBurning = 1u << 0,  // static flammable cell that is on fire (wood, plant)
    kFlagIgnited = 1u << 1,  // gunpowder that is about to detonate
};

inline constexpr float kAmbientTemp = 20.0f;

class World {
public:
    World(int width, int height, std::uint64_t seed = 0x5eedull);

    int width() const { return width_; }
    int height() const { return height_; }
    std::uint64_t seed() const { return seed_; }

    bool in_bounds(int x, int y) const { return x >= 0 && y >= 0 && x < width_ && y < height_; }

    // Unchecked access (asserts in debug builds).
    Cell& at(int x, int y);
    const Cell& at(int x, int y) const;

    // Bounds-checked access; nullptr when (x, y) is outside the world.
    Cell* try_at(int x, int y);
    const Cell* try_at(int x, int y) const;

    // Make a fresh cell of material m: spawn temperature, random shade,
    // random lifetime in [life_min, life_max], cleared flags/aux.
    Cell make_cell(Material m);

    // Replace the cell at (x, y) with make_cell(m). Ignores out-of-bounds.
    void set(int x, int y, Material m);

    // Paint a filled circle. When only_empty is true, existing non-empty
    // cells are left alone (lets you pour sand around things).
    void paint_circle(int cx, int cy, int radius, Material m, bool only_empty = false);

    void clear();

    Rng& rng() { return rng_; }
    std::uint64_t tick() const { return tick_; }
    void set_tick(std::uint64_t t) { tick_ = t; }
    void advance_tick() { ++tick_; }

    std::vector<Cell>& cells() { return cells_; }
    const std::vector<Cell>& cells() const { return cells_; }

    // Number of non-empty cells of a material (O(n); for tests, HUD and debugging).
    int count(Material m) const;

private:
    int width_;
    int height_;
    std::uint64_t seed_;
    std::uint64_t tick_{0};
    Rng rng_;
    std::vector<Cell> cells_;
};

}  // namespace gf
