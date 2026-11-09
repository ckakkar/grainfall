#pragma once
// The cellular-automaton engine. OWNER: partner (Codex).
// The public API below is the contract the app and tests use; keep it stable.
// Private members and everything in simulation.cpp are yours to design.

#include <cstdint>
#include <vector>

#include "core/world.hpp"

namespace gf {

struct SimStats {
    std::uint64_t tick = 0;      // world tick after the last step
    int active_chunks = 0;       // chunks that were simulated in the last step
    int total_chunks = 0;
    int moved_cells = 0;         // cells that moved in the last step
    int reactions = 0;           // material changes from reactions / phase changes in the last step
    int explosions = 0;          // explosions triggered in the last step
    double step_ms = 0.0;        // wall time of the last step
};

class Simulation {
public:
    // Chunk size for sleeping/dirty tracking (cells per side).
    static constexpr int kChunkSize = 16;

    explicit Simulation(World& world);

    // Advance the world by exactly one tick (calls world.advance_tick()).
    void step();

    // Instantly detonate at (x, y): destroys/flings non-metal cells within radius,
    // leaves fire and heat behind. Gunpowder uses this internally; the app uses it
    // for the "bomb" tool.
    void explode(int x, int y, int radius);

    // Tell the engine an external edit happened (painting, loading) so sleeping
    // chunks in that rectangle wake up. Coordinates are clamped to the world.
    void wake_rect(int x0, int y0, int x1, int y1);
    void wake_all();

    const SimStats& stats() const { return stats_; }
    const World& world() const { return world_; }

    // Debug view for the renderer: is the chunk containing (x, y) awake?
    bool chunk_awake(int x, int y) const;

private:
    World& world_;
    SimStats stats_{};
    int chunks_x_ = 0;
    int chunks_y_ = 0;
    std::vector<std::uint8_t> awake_;       // chunk awake this tick
    std::vector<std::uint8_t> awake_next_;  // chunk awake next tick
};

}  // namespace gf
