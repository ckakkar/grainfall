#include "app/renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "sim/simulation.hpp"

namespace gf {

namespace {

struct Rgb {
    float r, g, b;
};

Rgb unpack(std::uint32_t rgb) {
    return {static_cast<float>((rgb >> 16) & 0xff), static_cast<float>((rgb >> 8) & 0xff),
            static_cast<float>(rgb & 0xff)};
}

std::uint32_t pack(Rgb c) {
    auto ch = [](float v) { return static_cast<std::uint32_t>(std::clamp(v, 0.0f, 255.0f) + 0.5f); };
    return 0xff000000u | (ch(c.r) << 16) | (ch(c.g) << 8) | ch(c.b);
}

Rgb lerp(Rgb a, Rgb b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

// Black-body-ish glow: dark red at 450 C, orange at ~900 C, near white at 1600 C.
Rgb incandescence(float temp) {
    static constexpr std::array<std::pair<float, Rgb>, 5> kStops{{
        {450.f, {90, 10, 0}},
        {700.f, {200, 40, 0}},
        {950.f, {255, 120, 20}},
        {1250.f, {255, 210, 90}},
        {1700.f, {255, 250, 230}},
    }};
    if (temp <= kStops.front().first) return kStops.front().second;
    for (std::size_t i = 1; i < kStops.size(); ++i) {
        if (temp <= kStops[i].first) {
            const float t = (temp - kStops[i - 1].first) / (kStops[i].first - kStops[i - 1].first);
            return lerp(kStops[i - 1].second, kStops[i].second, t);
        }
    }
    return kStops.back().second;
}

// Cheap per-cell hash so animated materials flicker independently.
std::uint32_t hash3(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    std::uint32_t h = a * 0x9e3779b1u ^ b * 0x85ebca77u ^ c * 0xc2b2ae3du;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    return h;
}

Rgb background(int y, int height) {
    const float t = static_cast<float>(y) / static_cast<float>(std::max(height - 1, 1));
    return lerp({14, 16, 24}, {8, 9, 13}, t);
}

Rgb cell_color(const Cell& c, int x, int y, int height, std::uint64_t tick) {
    if (c.mat == Material::Empty) return background(y, height);

    const MaterialInfo& mi = info(c.mat);
    Rgb base = unpack(mi.color);
    const float noise = (static_cast<float>(c.shade) - 127.5f) / 127.5f;  // -1..1
    const float jitter = static_cast<float>(mi.jitter);
    const auto t = static_cast<std::uint32_t>(tick);
    const auto ux = static_cast<std::uint32_t>(x);
    const auto uy = static_cast<std::uint32_t>(y);

    switch (c.mat) {
        case Material::Fire: {
            // Fade from white-yellow core to deep red as it burns out, with flicker.
            const float span = static_cast<float>(std::max<int>(mi.life_max, 1));
            const float fresh = std::clamp(static_cast<float>(c.life) / span, 0.0f, 1.0f);
            const float flick = static_cast<float>(hash3(ux, uy, t / 2) & 0xff) / 255.0f;
            base = lerp({170, 25, 5}, {255, 230, 120}, 0.35f * fresh + 0.65f * flick * fresh + 0.15f);
            return base;
        }
        case Material::Lava: {
            const float wave = 0.5f + 0.5f * std::sin(static_cast<float>(x) * 0.35f + static_cast<float>(tick) * 0.05f +
                                                       static_cast<float>(c.shade) * 0.05f);
            base = lerp(unpack(0xc22a08), unpack(0xffb03a), 0.25f + 0.6f * wave);
            break;
        }
        case Material::Water: {
            const float wave = 0.5f + 0.5f * std::sin(static_cast<float>(x) * 0.2f + static_cast<float>(y) * 0.5f +
                                                       static_cast<float>(tick) * 0.08f);
            base.r += 10.0f * wave;
            base.g += 18.0f * wave;
            base.b += 22.0f * wave;
            base.r += noise * jitter * 0.5f;
            base.g += noise * jitter * 0.5f;
            base.b += noise * jitter * 0.5f;
            break;
        }
        case Material::Acid: {
            const float bubble = (hash3(ux, uy, t / 6) & 0x3f) == 0 ? 40.0f : 0.0f;
            base.r += noise * jitter + bubble;
            base.g += noise * jitter + bubble;
            base.b += noise * jitter * 0.5f + bubble * 0.5f;
            break;
        }
        case Material::Steam:
        case Material::Smoke: {
            // Gases thin out as they age: blend toward the background.
            const float span = static_cast<float>(std::max<int>(mi.life_max, 1));
            const float alive = std::clamp(static_cast<float>(c.life) / span, 0.15f, 1.0f);
            base.r += noise * jitter;
            base.g += noise * jitter;
            base.b += noise * jitter;
            base = lerp(background(y, height), base, 0.35f + 0.65f * alive);
            break;
        }
        default:
            base.r += noise * jitter;
            base.g += noise * jitter;
            base.b += noise * jitter;
            break;
    }

    // Burning wood / plant: flickering orange embers.
    if (c.flags & kFlagBurning) {
        const float flick = static_cast<float>(hash3(ux, uy, t / 3) & 0xff) / 255.0f;
        base = lerp(base, {255, 110 + 90 * flick, 20}, 0.45f + 0.4f * flick);
    }

    // Anything solid or liquid that is very hot glows (red-hot metal, cooling lava rock).
    if (c.temp > 450.0f && phase_of(c.mat) != Phase::Gas) {
        const float glow = std::clamp((c.temp - 450.0f) / 900.0f, 0.0f, 0.85f);
        base = lerp(base, incandescence(c.temp), glow);
    }
    // Freezing things get a faint blue frost.
    if (c.temp < -5.0f && c.mat != Material::Ice) {
        base = lerp(base, {170, 220, 255}, std::clamp((-c.temp - 5.0f) / 60.0f, 0.0f, 0.5f));
    }
    return base;
}

}  // namespace

const char* view_name(ViewMode v) {
    switch (v) {
        case ViewMode::Normal: return "normal";
        case ViewMode::Heat: return "heat";
        case ViewMode::Chunks: return "chunks";
        default: return "?";
    }
}

std::uint32_t swatch_color(Material m) { return 0xff000000u | info(m).color; }

std::uint32_t heat_color(float temp) {
    static constexpr std::array<std::pair<float, Rgb>, 7> kStops{{
        {-60.f, {60, 120, 255}},
        {0.f, {20, 40, 120}},
        {20.f, {12, 14, 22}},
        {100.f, {150, 30, 90}},
        {400.f, {230, 60, 20}},
        {900.f, {255, 190, 30}},
        {1500.f, {255, 255, 240}},
    }};
    if (!(temp > kStops.front().first)) return pack(kStops.front().second);  // also catches NaN
    for (std::size_t i = 1; i < kStops.size(); ++i) {
        if (temp <= kStops[i].first) {
            const float t = (temp - kStops[i - 1].first) / (kStops[i].first - kStops[i - 1].first);
            return pack(lerp(kStops[i - 1].second, kStops[i].second, t));
        }
    }
    return pack(kStops.back().second);
}

void shade_world(const World& world, const Simulation* sim, ViewMode view, std::vector<std::uint32_t>& pixels) {
    const int w = world.width();
    const int h = world.height();
    pixels.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    const std::uint64_t tick = world.tick();
    const auto& cells = world.cells();

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
            const Cell& c = cells[i];
            std::uint32_t out;
            if (view == ViewMode::Heat) {
                out = heat_color(c.temp);
                if (c.mat != Material::Empty && c.temp > -1.0f && c.temp < 40.0f) {
                    // Show material outlines faintly at room temperature.
                    const Rgb mat = unpack(info(c.mat).color);
                    out = pack(lerp(unpack(out & 0xffffff), mat, 0.25f));
                }
            } else {
                Rgb col = cell_color(c, x, y, h, tick);
                if (view == ViewMode::Chunks && sim) {
                    const int cs = Simulation::kChunkSize;
                    const bool edge = (x % cs == 0) || (y % cs == 0);
                    if (sim->chunk_awake(x, y)) {
                        col = lerp(col, {40, 255, 120}, edge ? 0.55f : 0.12f);
                    } else if (edge) {
                        col = lerp(col, {90, 90, 110}, 0.35f);
                    }
                }
                out = pack(col);
            }
            pixels[i] = out;
        }
    }
}

}  // namespace gf
