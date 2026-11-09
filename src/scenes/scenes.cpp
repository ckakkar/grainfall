// STUB written by the lead so the project builds. The intern (agy) replaces this file.
#include "scenes/scenes.hpp"

#include <array>

namespace gf::scenes {

namespace {

void build_empty_box(World& world) {
    world.clear();
    for (int x = 0; x < world.width(); ++x) world.set(x, world.height() - 1, Material::Stone);
}

constexpr std::array<Scene, 1> kScenes{{
    {"Empty box", "A stone floor and nothing else", &build_empty_box},
}};

}  // namespace

std::span<const Scene> all() { return kScenes; }

}  // namespace gf::scenes
