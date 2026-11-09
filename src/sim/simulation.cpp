#include "sim/simulation.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <utility>

namespace gf {
namespace {

constexpr std::array<std::pair<int, int>, 4> kNeighbours{{{0, -1}, {1, 0}, {0, 1}, {-1, 0}}};
constexpr float kMinTemp = -273.15f;
constexpr float kMaxTemp = 20000.0f;
constexpr float kFireMinTemp = 800.0f;
constexpr float kFireMaxTemp = 1100.0f;

float safe_temp(float value) {
    return std::isfinite(value) ? std::clamp(value, kMinTemp, kMaxTemp) : kAmbientTemp;
}

bool acid_resistant(Material m) {
    return m == Material::Empty || m == Material::Stone || m == Material::Metal || m == Material::Acid;
}

}  // namespace

Simulation::Simulation(World& world) : world_(world) {
    epoch_ = world_.tick();
    chunks_x_ = (world_.width() + kChunkSize - 1) / kChunkSize;
    chunks_y_ = (world_.height() + kChunkSize - 1) / kChunkSize;
    const auto chunks = static_cast<std::size_t>(chunks_x_) * chunks_y_;
    awake_.assign(chunks, 1);
    awake_next_.assign(chunks, 0);
    dirty_.assign(chunks, 0);
    active_.reserve(chunks);
    heat_.resize(world_.cells().size(), kAmbientTemp);
    flowing_rows_.resize(chunks_y_, 0);
    moved_at_.resize(world_.cells().size(), 0);
    stats_.tick = world_.tick();
    stats_.total_chunks = static_cast<int>(chunks);
}

void Simulation::changed(int x, int y) {
    const auto chunk = static_cast<std::size_t>((y / kChunkSize) * chunks_x_ + x / kChunkSize);
    if (stepping_) {
        dirty_[chunk] = 1;
        // Nonlocal effects (chain blasts and flying debris) can reach beyond
        // the initial active list. Later stages must see those chunks now.
        awake_[chunk] = 1;
    } else {
        // Public explosions, like painting, must wake their whole boundary.
        wake_rect(x, y, x, y);
    }
}

void Simulation::replace(int x, int y, Material material, float temperature) {
    Cell replacement = world_.make_cell(material);
    replacement.temp = safe_temp(temperature);
    if (material == Material::Fire)
        replacement.temp = std::clamp(replacement.temp, kFireMinTemp, kFireMaxTemp);
    world_.at(x, y) = replacement;
    ++stats_.reactions;
    changed(x, y);
}

void Simulation::interact(int x, int y) {
    const Material material = world_.at(x, y).mat;
    if (material != Material::Lava && material != Material::Acid &&
        material != Material::Plant && material != Material::Ice) return;

    for (const auto [dx, dy] : kNeighbours) {
        const int nx = x + dx, ny = y + dy;
        Cell* neighbour = world_.try_at(nx, ny);
        if (!neighbour) continue;
        // A failed stochastic reaction is still pending work. In particular,
        // trapped acid and a plant beside still water must get another chance.
        if ((material == Material::Acid && !acid_resistant(neighbour->mat)) ||
            (material == Material::Plant && neighbour->mat == Material::Water)) changed(x, y);
        if (material == Material::Lava && neighbour->mat == Material::Water) {
            const float steam_temp = std::max(130.0f, safe_temp(neighbour->temp));
            replace(nx, ny, Material::Steam, steam_temp);
            replace(x, y, Material::Stone, 600.0f);
            return;
        }
        if (material == Material::Acid && !acid_resistant(neighbour->mat) && world_.rng().chance(0.22f)) {
            const bool smoke = world_.rng().chance(0.12f);
            replace(nx, ny, smoke ? Material::Smoke : Material::Empty,
                    smoke ? 120.0f : safe_temp(neighbour->temp));
            if (world_.rng().chance(0.08f)) {
                const bool fumes = world_.rng().chance(0.25f);
                replace(x, y, fumes ? Material::Smoke : Material::Empty, fumes ? 120.0f : kAmbientTemp);
            }
            return;
        }
        if (material == Material::Plant && !(world_.at(x, y).flags & kFlagBurning) &&
            neighbour->mat == Material::Water && world_.rng().chance(0.002f)) {
            replace(nx, ny, Material::Plant, safe_temp(neighbour->temp));
            return;
        }
        if (material == Material::Ice && world_.at(x, y).temp < 0.0f &&
            neighbour->mat == Material::Water && world_.rng().chance(0.012f)) {
            replace(nx, ny, Material::Ice, std::min(-1.0f, world_.at(x, y).temp * 0.5f));
            return;
        }
    }
}

void Simulation::diffuse_heat() {
    const int width = world_.width(), height = world_.height();
    auto& cells = world_.cells();
    const auto& materials = material_table();
    // Jacobi diffusion: all fluxes read the same temperature field. The sum
    // of four conductance weights is at most .8, so this is a bounded average.
    for (const int chunk : active_) {
        const int x0 = (chunk % chunks_x_) * kChunkSize;
        const int y0 = (chunk / chunks_x_) * kChunkSize;
        const int x1 = std::min(x0 + kChunkSize, width);
        const int y1 = std::min(y0 + kChunkSize, height);
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const auto index = static_cast<std::size_t>(y) * width + x;
                const Cell& cell = cells[index];
                const float temperature = safe_temp(cell.temp);
                const float conductivity = materials[static_cast<int>(cell.mat)].conductivity;
                float next = temperature;
                const auto exchange = [&](std::size_t other) {
                    const float b = materials[static_cast<int>(cells[other].mat)].conductivity;
                    const float conductance = conductivity + b > 0.0f
                        ? 0.4f * conductivity * b / (conductivity + b) : 0.0f;
                    next += (safe_temp(cells[other].temp) - temperature) * conductance;
                };
                if (x > 0) exchange(index - 1);
                if (x + 1 < width) exchange(index + 1);
                if (y > 0) exchange(index - width);
                if (y + 1 < height) exchange(index + width);
                if (cell.mat == Material::Empty) next += (kAmbientTemp - next) * 0.025f;
                if (std::abs(next - kAmbientTemp) < 0.01f) next = kAmbientTemp;
                heat_[index] = safe_temp(next);
            }
        }
    }
    for (const int chunk : active_) {
        const int x0 = (chunk % chunks_x_) * kChunkSize;
        const int y0 = (chunk / chunks_x_) * kChunkSize;
        const int x1 = std::min(x0 + kChunkSize, width);
        const int y1 = std::min(y0 + kChunkSize, height);
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const auto index = static_cast<std::size_t>(y) * width + x;
                if (cells[index].temp != heat_[index]) dirty_[chunk] = 1;
                cells[index].temp = heat_[index];
            }
        }
    }
}

void Simulation::evolve(int x, int y) {
    Cell& cell = world_.at(x, y);
    const Material material = cell.mat;
    if (material == Material::Empty) return;

    if (cell.life > 0) {
        --cell.life;
        changed(x, y);
        if (cell.life == 0) {
            if (material == Material::Steam) {
                const bool water = world_.rng().chance(0.30f);
                replace(x, y, water ? Material::Water : Material::Empty,
                        water ? 80.0f : cell.temp);
            } else if (material == Material::Fire) {
                replace(x, y, world_.rng().chance(0.40f) ? Material::Smoke : Material::Empty, 120.0f);
            } else {
                replace(x, y, Material::Empty, cell.temp);
            }
            return;
        }
    }

    if (material == Material::Fire) {
        cell.temp = std::clamp(cell.temp, kFireMinTemp, kFireMaxTemp);
        for (const auto [dx, dy] : kNeighbours) {
            if (Cell* neighbour = world_.try_at(x + dx, y + dy)) {
                const float temperature = safe_temp(neighbour->temp);
                // Preserve the initial heating rate, then approach the flame
                // ceiling. Adjacent flames cannot amplify one another forever;
                // genuinely hotter lava/explosion debris is left hot.
                const float warming = std::clamp((kFireMaxTemp - temperature) * 0.20f, 0.0f, 85.0f);
                const float heated = temperature + warming;
                if (neighbour->temp != heated) {
                    neighbour->temp = heated;
                    changed(x + dx, y + dy);
                }
            }
        }
        changed(x, y);
        return;
    }

    if (cell.flags & kFlagBurning) {
        cell.temp = std::max(cell.temp, 650.0f);
        if (cell.aux > 0) --cell.aux;
        changed(x, y);
        if (cell.aux <= 0) {
            const bool flame = world_.rng().coin();
            replace(x, y, flame ? Material::Fire : Material::Smoke, flame ? 900.0f : 160.0f);
            return;
        }
        for (const auto [dx, dy] : kNeighbours) {
            Cell* neighbour = world_.try_at(x + dx, y + dy);
            if (neighbour && neighbour->mat == Material::Empty && world_.rng().chance(0.18f)) {
                replace(x + dx, y + dy, Material::Fire, 900.0f);
            }
        }
        return;
    }

    const auto& mi = info(material);
    if (mi.flammability > 0.0f) {
        bool ignition = cell.temp >= mi.ignite_temp || (cell.flags & kFlagIgnited);
        if (!ignition) {
            for (const auto [dx, dy] : kNeighbours) {
                const Cell* neighbour = world_.try_at(x + dx, y + dy);
                if (neighbour && (neighbour->mat == Material::Fire || (neighbour->flags & kFlagBurning))) {
                    ignition = true;
                    break;
                }
            }
        }
        if (ignition && world_.rng().chance(mi.flammability)) {
            if (material == Material::Gunpowder) {
                explode(x, y, 6);
            } else if (material == Material::Wood || material == Material::Plant) {
                cell.flags |= kFlagBurning;
                cell.aux = static_cast<std::int16_t>(material == Material::Wood
                    ? world_.rng().range(70, 140) : world_.rng().range(30, 75));
                cell.temp = std::max(450.0f, cell.temp);
                changed(x, y);
            } else {
                replace(x, y, Material::Fire, std::max(900.0f, cell.temp));
            }
            return;
        }
    }

    // Hysteresis between boiling/condensation avoids a flickering phase front.
    Material phase = material;
    float temperature = cell.temp;
    switch (material) {
        case Material::Water:
            if (temperature > 100.0f) phase = Material::Steam;
            else if (temperature < 0.0f) phase = Material::Ice;
            break;
        case Material::Steam:
            if (temperature < 90.0f) phase = Material::Water;
            break;
        case Material::Ice:
            if (temperature > 0.0f) phase = Material::Water;
            break;
        case Material::Lava:
            if (temperature < 700.0f) phase = Material::Stone;
            break;
        case Material::Stone:
        case Material::Sand:
            if (temperature > 1200.0f) phase = Material::Lava;
            break;
        default: break;
    }
    if (phase != material) {
        changed(x, y);  // retry even if an ambient-temperature ice cell misses
        if (world_.rng().chance(0.35f)) {
            if (phase == Material::Steam) temperature = std::max(105.0f, temperature - 15.0f);
            if (phase == Material::Water && material == Material::Steam) temperature = std::min(85.0f, temperature);
            replace(x, y, phase, temperature);
        }
    }
}

bool Simulation::can_move(int x, int y, int tx, int ty, bool sideways) const {
    if (!world_.in_bounds(tx, ty)) return false;
    const int width = world_.width();
    const auto from = static_cast<std::size_t>(y) * width + x;
    const auto to = static_cast<std::size_t>(ty) * width + tx;
    const auto& cells = world_.cells();
    // A vacancy is not a particle: following cells may enter an empty source
    // immediately, while every displaced non-empty cell still moves once.
    if (cells[to].mat != Material::Empty && moved_at_[to] == epoch_) return false;
    const auto& source = info(cells[from].mat);
    const auto& destination = info(cells[to].mat);
    if (destination.phase != Phase::Empty) {
        if (sideways) return false;
        if (source.phase == Phase::Gas) {
            if (destination.phase != Phase::Gas || source.density >= destination.density) return false;
        } else {
            if ((destination.phase != Phase::Gas && destination.phase != Phase::Liquid) ||
                source.density <= destination.density) return false;
        }
    }
    return true;
}

bool Simulation::try_move(int x, int y, int tx, int ty, bool sideways) {
    if (!can_move(x, y, tx, ty, sideways)) return false;
    const auto from = static_cast<std::size_t>(y) * world_.width() + x;
    const auto to = static_cast<std::size_t>(ty) * world_.width() + tx;
    auto& cells = world_.cells();
    const bool liquid = phase_of(cells[from].mat) == Phase::Liquid || phase_of(cells[to].mat) == Phase::Liquid;
    stats_.moved_cells += cells[to].mat == Material::Empty ? 1 : 2;
    std::swap(cells[from], cells[to]);
    cells[to].clock = static_cast<std::uint8_t>(epoch_);
    cells[from].clock = cells[from].mat == Material::Empty ? 0 : static_cast<std::uint8_t>(epoch_);
    moved_at_[to] = epoch_;
    moved_at_[from] = cells[from].mat == Material::Empty ? 0 : epoch_;
    changed(x, y);
    changed(tx, ty);
    if (liquid) {
        wake_flow(y);
        wake_flow(ty);
    }
    return true;
}

void Simulation::wake_flow(int y) {
    // Hydraulic pressure spans a connected row, beyond the local dirty halo.
    // Wake horizontal strips once per tick so a remote change cannot strand a
    // free edge asleep. Also cover the row below, whose load just changed.
    const int first = y / kChunkSize;
    const int last = std::min(world_.height() - 1, y + 1) / kChunkSize;
    for (int cy = first; cy <= last; ++cy) {
        if (flowing_rows_[cy]) continue;
        flowing_rows_[cy] = 1;
        const auto begin = static_cast<std::size_t>(cy) * chunks_x_;
        std::fill_n(awake_.begin() + begin, chunks_x_, 1);
        std::fill_n(dirty_.begin() + begin, chunks_x_, 1);
    }
}

void Simulation::move(int x, int y) {
    const auto index = static_cast<std::size_t>(y) * world_.width() + x;
    if (moved_at_[index] == epoch_) return;
    const auto& mi = info(world_.cells()[index].mat);
    const int dy = mi.phase == Phase::Gas ? -1 : 1;
    if (try_move(x, y, x, y + dy)) return;
    const bool left = can_move(x, y, x - 1, y + dy);
    const bool right = can_move(x, y, x + 1, y + dy);
    if (left || right) {
        const int direction = left && right ? (world_.rng().coin() ? 1 : -1) : (left ? -1 : 1);
        try_move(x, y, x + direction, y + dy);
        return;
    }
    if (mi.phase == Phase::Powder) return;
    const bool liquid = mi.phase == Phase::Liquid;
    bool pressure = false;
    if (liquid && y > 0) {
        const auto loaded = [&](int column) {
            const Phase above = phase_of(world_.at(column, y - 1).mat);
            return above == Phase::Liquid || above == Phase::Powder;
        };
        pressure = loaded(x);
        const bool free_edge = (x > 0 && world_.at(x - 1, y).mat == Material::Empty) ||
            (x + 1 < world_.width() && world_.at(x + 1, y).mat == Material::Empty);
        if (!pressure && free_edge) {
            // A connected row transmits pressure to its free edge. Looking
            // only directly above that edge leaves a staircase whose treads
            // are as wide as the dispersion distance, rather than a flat pool.
            for (int sign : {-1, 1}) {
                for (int nx = x + sign; world_.in_bounds(nx, y); nx += sign) {
                    if (phase_of(world_.at(nx, y).mat) != Phase::Liquid) break;
                    if (loaded(nx)) {
                        pressure = true;
                        break;
                    }
                }
                if (pressure) break;
            }
        }
    }
    std::array<int, 2> targets{x, x};
    for (int side = 0; side < 2; ++side) {
        const int sign = side == 0 ? -1 : 1;
        int target = x;
        for (int distance = 1; distance <= mi.dispersion; ++distance) {
            const int nx = x + sign * distance;
            if (!world_.in_bounds(nx, y)) break;
            const auto ni = static_cast<std::size_t>(y) * world_.width() + nx;
            if (world_.cells()[ni].mat != Material::Empty) break;
            // A liquid spreads under the weight of a column, or toward a
            // downward outlet. Unpressurized water on a level surface rests
            // instead of performing an endless horizontal random walk.
            if (!liquid || pressure || can_move(x, y, nx, y + 1)) target = nx;
        }
        targets[side] = target;
    }
    // Settled cells consume no randomness. Thus waking an unchanged chunk
    // cannot perturb the random stream used by chemistry elsewhere.
    if (targets[0] != x || targets[1] != x) {
        const int side = targets[0] != x && targets[1] != x ? (world_.rng().coin() ? 1 : 0)
            : (targets[0] != x ? 0 : 1);
        try_move(x, y, targets[side], y, true);
    }
}

void Simulation::step() {
    const auto start = std::chrono::steady_clock::now();
    world_.advance_tick();
    ++epoch_;
    // This is effectively unreachable, but keeps the full stamp correct too.
    if (epoch_ == 0) {
        std::fill(moved_at_.begin(), moved_at_.end(), 0);
        ++epoch_;
    }
    stats_ = {};
    stats_.tick = world_.tick();
    stats_.total_chunks = chunks_x_ * chunks_y_;
    active_.clear();
    std::fill(dirty_.begin(), dirty_.end(), 0);
    std::fill(awake_next_.begin(), awake_next_.end(), 0);
    std::fill(flowing_rows_.begin(), flowing_rows_.end(), 0);
    for (int chunk = 0; chunk < stats_.total_chunks; ++chunk) {
        if (awake_[chunk]) active_.push_back(chunk);
    }
    stats_.active_chunks = static_cast<int>(active_.size());
    stepping_ = true;
    const int width = world_.width(), height = world_.height();

    // Contact reactions precede heat/phase changes: water touching lava must
    // quench it even when the water could also boil during this same tick.
    for (int chunk = 0; chunk < stats_.total_chunks; ++chunk) {
        if (!awake_[chunk]) continue;
        const int x0 = (chunk % chunks_x_) * kChunkSize;
        const int y0 = (chunk / chunks_x_) * kChunkSize;
        for (int y = y0; y < std::min(y0 + kChunkSize, height); ++y) {
            const bool reverse = ((world_.tick() + y) & 1u) != 0;
            const int end = std::min(x0 + kChunkSize, width);
            for (int i = x0; i < end; ++i) interact(reverse ? end - 1 - (i - x0) : i, y);
        }
    }
    diffuse_heat();
    for (int chunk = 0; chunk < stats_.total_chunks; ++chunk) {
        if (!awake_[chunk]) continue;
        const int x0 = (chunk % chunks_x_) * kChunkSize;
        const int y0 = (chunk / chunks_x_) * kChunkSize;
        for (int y = y0; y < std::min(y0 + kChunkSize, height); ++y) {
            const bool reverse = ((world_.tick() + y) & 1u) != 0;
            const int end = std::min(x0 + kChunkSize, width);
            for (int i = x0; i < end; ++i) {
                const int x = reverse ? end - 1 - (i - x0) : i;
                evolve(x, y);
                const Cell& cell = world_.at(x, y);
                if (cell.life > 0 || cell.flags != 0 || std::abs(cell.temp - kAmbientTemp) > 0.1f)
                    dirty_[chunk] = 1;
            }
        }
    }

    // Globally ordered rows, rather than chunk-by-chunk traversal, avoid seams
    // at chunk boundaries. Each swapped participant is stamped, including gas
    // displaced by falling sand/water. Vacancies remain reusable immediately.
    for (int pass = 0; pass < 2; ++pass) {
        for (int row = 0; row < height; ++row) {
            const int y = pass == 0 ? height - 1 - row : row;
            const bool reverse = ((world_.tick() + y) & 1u) != 0;
            for (int ci = 0; ci < chunks_x_; ++ci) {
                const int cx = reverse ? chunks_x_ - 1 - ci : ci;
                if (!awake_[(y / kChunkSize) * chunks_x_ + cx]) continue;
                const int x0 = cx * kChunkSize, end = std::min(x0 + kChunkSize, width);
                for (int i = x0; i < end; ++i) {
                    const int x = reverse ? end - 1 - (i - x0) : i;
                    const Phase phase = phase_of(world_.at(x, y).mat);
                    if ((pass == 0 && (phase == Phase::Powder || phase == Phase::Liquid)) ||
                        (pass == 1 && phase == Phase::Gas)) move(x, y);
                }
            }
        }
    }
    for (int chunk = 0; chunk < stats_.total_chunks; ++chunk) {
        if (!dirty_[chunk]) continue;
        const int cx = chunk % chunks_x_, cy = chunk / chunks_x_;
        for (int ny = std::max(0, cy - 1); ny <= std::min(chunks_y_ - 1, cy + 1); ++ny)
            for (int nx = std::max(0, cx - 1); nx <= std::min(chunks_x_ - 1, cx + 1); ++nx)
                awake_next_[ny * chunks_x_ + nx] = 1;
    }
    stats_.active_chunks = static_cast<int>(std::count(awake_.begin(), awake_.end(), 1));
    awake_.swap(awake_next_);
    stepping_ = false;
    stats_.step_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void Simulation::explode(int x, int y, int radius) {
    if (!world_.in_bounds(x, y) || radius < 0) return;
    // An iterative work queue bounds stack usage even for a world of powder.
    struct Blast { int x, y, radius; };
    const int maximum_radius = static_cast<int>(std::ceil(std::hypot(
        static_cast<double>(world_.width()), static_cast<double>(world_.height()))));
    std::vector<Blast> pending{{x, y, std::min(radius, maximum_radius)}};
    for (std::size_t blast = 0; blast < pending.size(); ++blast) {
        const auto [cx, cy, r] = pending[blast];
        ++stats_.explosions;
        const float radius_squared = static_cast<float>(r) * r;
        const int x0 = std::max(0, cx - r), x1 = std::min(world_.width() - 1, cx + r);
        const int y0 = std::max(0, cy - r), y1 = std::min(world_.height() - 1, cy + r);
        for (int py = y0; py <= y1; ++py) {
            for (int px = x0; px <= x1; ++px) {
                const float dx = static_cast<float>(px - cx), dy = static_cast<float>(py - cy);
                const float distance_squared = dx * dx + dy * dy;
                if (distance_squared > radius_squared) continue;
                Cell& cell = world_.at(px, py);
                const Material material = cell.mat;
                const float strength = 1.0f - std::sqrt(distance_squared) / static_cast<float>(r + 1);
                const float temperature = safe_temp(cell.temp + 400.0f + 1400.0f * strength);
                if (material == Material::Metal) {
                    cell.temp = temperature;
                    changed(px, py);
                    continue;
                }
                if (material == Material::Stone) {
                    cell.temp = std::max(safe_temp(cell.temp), std::min(1100.0f, temperature));
                    if (strength > 0.75f && world_.rng().chance(0.25f))
                        replace(px, py, Material::Sand, std::min(1000.0f, temperature));
                    changed(px, py);
                    continue;
                }
                if (material == Material::Gunpowder && (px != cx || py != cy)) {
                    pending.push_back({px, py, 6});
                    // Consume it immediately, so overlapping blasts queue it once.
                } else if (is_movable(material) && material != Material::Fire &&
                           distance_squared > 0.0f && world_.rng().chance(0.25f)) {
                    const float distance = std::sqrt(distance_squared);
                    const float reach = static_cast<float>(r + world_.rng().range(2, 5));
                    const int tx = cx + static_cast<int>(std::lround(dx * reach / distance));
                    const int ty = cy + static_cast<int>(std::lround(dy * reach / distance));
                    Cell* destination = world_.try_at(tx, ty);
                    if (destination && destination->mat == Material::Empty) {
                        std::swap(cell, *destination);
                        destination->temp = temperature;
                        destination->clock = static_cast<std::uint8_t>(epoch_);
                        const auto ti = static_cast<std::size_t>(ty) * world_.width() + tx;
                        moved_at_[ti] = epoch_;
                        ++stats_.moved_cells;
                        changed(tx, ty);
                    }
                }
                const float roll = world_.rng().uniform();
                const Material debris = roll < 0.40f ? Material::Fire :
                    (roll < 0.48f ? Material::Smoke : Material::Empty);
                replace(px, py, debris, temperature);
            }
        }
    }
}

void Simulation::wake_rect(int x0, int y0, int x1, int y1) {
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    if (x1 < 0 || y1 < 0 || x0 >= world_.width() || y0 >= world_.height()) return;
    const int cx0 = std::max(0, std::max(0, x0) / kChunkSize - 1);
    const int cy0 = std::max(0, std::max(0, y0) / kChunkSize - 1);
    const int cx1 = std::min(chunks_x_ - 1, std::min(world_.width() - 1, x1) / kChunkSize + 1);
    const int cy1 = std::min(chunks_y_ - 1, std::min(world_.height() - 1, y1) / kChunkSize + 1);
    for (int cy = cy0; cy <= cy1; ++cy) {
        for (int cx = cx0; cx <= cx1; ++cx) {
            const int chunk = cy * chunks_x_ + cx;
            awake_[chunk] = 1;
            if (stepping_) dirty_[chunk] = 1;
        }
    }
}

void Simulation::wake_all() {
    std::fill(awake_.begin(), awake_.end(), 1);
    if (stepping_) std::fill(dirty_.begin(), dirty_.end(), 1);
}

bool Simulation::chunk_awake(int x, int y) const {
    return world_.in_bounds(x, y) && awake_[(y / kChunkSize) * chunks_x_ + x / kChunkSize] != 0;
}

}  // namespace gf
