#pragma once
// Procedural starter scenes. OWNER: intern (agy), second task.
// Each builder clears the world and draws a scene using only World's public API
// (set / paint_circle / at) and world.rng() for randomness, so scenes are
// deterministic for a given world seed.

#include <span>
#include <string_view>

#include "core/world.hpp"

namespace gf::scenes {

struct Scene {
    std::string_view name;         // short, shown in the HUD, e.g. "Volcano"
    std::string_view description;  // one line
    void (*build)(World& world);
};

// All scenes, in the order the app cycles through them. Never empty.
std::span<const Scene> all();

}  // namespace gf::scenes
