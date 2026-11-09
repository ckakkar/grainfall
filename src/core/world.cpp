#include "core/world.hpp"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace gf {

World::World(int width, int height, std::uint64_t seed)
    : width_(width), height_(height), seed_(seed), rng_(seed) {
    if (width <= 0 || height <= 0) throw std::invalid_argument("world dimensions must be positive");
    cells_.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
}

Cell& World::at(int x, int y) {
    assert(in_bounds(x, y));
    return cells_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)];
}

const Cell& World::at(int x, int y) const {
    assert(in_bounds(x, y));
    return cells_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)];
}

Cell* World::try_at(int x, int y) { return in_bounds(x, y) ? &at(x, y) : nullptr; }
const Cell* World::try_at(int x, int y) const { return in_bounds(x, y) ? &at(x, y) : nullptr; }

Cell World::make_cell(Material m) {
    const MaterialInfo& mi = info(m);
    Cell c;
    c.mat = m;
    c.shade = static_cast<std::uint8_t>(rng_.next_u32() & 0xffu);
    c.temp = mi.spawn_temp;
    if (mi.life_max > 0) c.life = static_cast<std::int16_t>(rng_.range(mi.life_min, mi.life_max));
    return c;
}

void World::set(int x, int y, Material m) {
    if (!in_bounds(x, y)) return;
    at(x, y) = make_cell(m);
}

void World::paint_circle(int cx, int cy, int radius, Material m, bool only_empty) {
    radius = std::max(radius, 0);
    const int r2 = radius * radius;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dy * dy > r2) continue;
            Cell* c = try_at(cx + dx, cy + dy);
            if (!c) continue;
            if (only_empty && c->mat != Material::Empty) continue;
            *c = make_cell(m);
        }
    }
}

void World::clear() { std::fill(cells_.begin(), cells_.end(), Cell{}); }

int World::count(Material m) const {
    return static_cast<int>(std::count_if(cells_.begin(), cells_.end(), [m](const Cell& c) { return c.mat == m; }));
}

}  // namespace gf
