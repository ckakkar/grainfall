#pragma once
// Turns a World into ARGB8888 pixels (one pixel per cell). Pure CPU code,
// no SDL, so it is shared by the window and the headless screenshot mode.

#include <cstdint>
#include <vector>

#include "core/world.hpp"

namespace gf {

class Simulation;

enum class ViewMode : std::uint8_t { Normal, Heat, Chunks, Count };

const char* view_name(ViewMode v);

// Fill `pixels` (resized to width*height) with the world's colors for `view`.
// `sim` may be null; the chunk view then shows no chunk tint.
void shade_world(const World& world, const Simulation* sim, ViewMode view, std::vector<std::uint32_t>& pixels);

// The flat swatch color of a material for UI (0xAARRGGBB, opaque).
std::uint32_t swatch_color(Material m);

// Heat-map ramp shared by the heat view and the HUD legend (0xAARRGGBB).
std::uint32_t heat_color(float temp_c);

}  // namespace gf
