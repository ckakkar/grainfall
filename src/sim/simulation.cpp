// STUB written by the lead so the project builds. The partner (Codex) replaces this file.
#include "sim/simulation.hpp"

#include <algorithm>

namespace gf {

Simulation::Simulation(World& world) : world_(world) {
    chunks_x_ = (world_.width() + kChunkSize - 1) / kChunkSize;
    chunks_y_ = (world_.height() + kChunkSize - 1) / kChunkSize;
    awake_.assign(static_cast<std::size_t>(chunks_x_ * chunks_y_), 1);
    awake_next_ = awake_;
    stats_.total_chunks = chunks_x_ * chunks_y_;
}

void Simulation::step() {
    world_.advance_tick();
    stats_.tick = world_.tick();
}

void Simulation::explode(int, int, int) {}
void Simulation::wake_rect(int, int, int, int) {}
void Simulation::wake_all() { std::fill(awake_.begin(), awake_.end(), 1); }
bool Simulation::chunk_awake(int, int) const { return true; }

}  // namespace gf
