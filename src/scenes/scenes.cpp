#include "scenes/scenes.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace gf::scenes {

namespace {

// Enforce the 1-cell stone border on left, right, and bottom edges
void apply_border(World& world) {
    const int w = world.width();
    const int h = world.height();
    for (int y = 0; y < h; ++y) {
        world.set(0, y, Material::Stone);
        world.set(w - 1, y, Material::Stone);
    }
    for (int x = 0; x < w; ++x) {
        world.set(x, h - 1, Material::Stone);
    }
}

// Scale normalized coordinates [0.0, 1.0] to inner playable bounds
inline int sx(const World& w, float fx) {
    return std::clamp(static_cast<int>(fx * static_cast<float>(w.width())), 1, w.width() - 2);
}

inline int sy(const World& w, float fy) {
    return std::clamp(static_cast<int>(fy * static_cast<float>(w.height())), 0, w.height() - 2);
}

void fill_rect(World& w, int x0, int y0, int x1, int y1, Material m) {
    const int min_x = std::max(1, std::min(x0, x1));
    const int max_x = std::min(w.width() - 2, std::max(x0, x1));
    const int min_y = std::max(0, std::min(y0, y1));
    const int max_y = std::min(w.height() - 2, std::max(y0, y1));
    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            w.set(x, y, m);
        }
    }
}

// ---------------------------------------------------------------------------
// Scene 0: Volcano (Most impressive starter scene)
// ---------------------------------------------------------------------------
void build_volcano(World& world) {
    world.clear();
    const int w = world.width();
    const int h = world.height();

    // Mountain geometry
    const int peak_left_x = sx(world, 0.44f);
    const int peak_right_x = sx(world, 0.60f);
    const int summit_y = sy(world, 0.28f);
    const int crater_floor_y = sy(world, 0.38f);
    const int base_left_x = sx(world, 0.08f);
    const int base_right_x = sx(world, 0.94f);
    const int base_y = sy(world, 0.88f);

    // Bedrock floor
    fill_rect(world, 1, base_y, w - 2, h - 2, Material::Stone);

    // Mountain body: fill lines between left and right slopes
    for (int y = summit_y; y <= base_y; ++y) {
        const float t = static_cast<float>(y - summit_y) / static_cast<float>(std::max(1, base_y - summit_y));
        const int left_x = static_cast<int>(peak_left_x - t * (peak_left_x - base_left_x));
        const int right_x = static_cast<int>(peak_right_x + t * (base_right_x - peak_right_x));

        for (int x = left_x; x <= right_x; ++x) {
            // Cut out the crater depression at the top
            if (y < crater_floor_y && x >= peak_left_x && x <= peak_right_x) {
                continue;
            }
            world.set(x, y, Material::Stone);
        }
    }

    // Magma chamber in the mountain base
    const int chamber_cx = sx(world, 0.52f);
    const int chamber_cy = sy(world, 0.72f);
    const int chamber_radius = std::max(3, static_cast<int>(0.13f * h));
    world.paint_circle(chamber_cx, chamber_cy, chamber_radius, Material::Lava);

    // Volcanic conduit / vent from magma chamber up to the crater
    const int vent_w = std::max(2, static_cast<int>(0.035f * w));
    const int vent_left = chamber_cx - vent_w / 2;
    const int vent_right = vent_left + vent_w;
    fill_rect(world, vent_left, crater_floor_y, vent_right, chamber_cy, Material::Lava);

    // Crater lava pool
    fill_rect(world, peak_left_x + 1, summit_y + (crater_floor_y - summit_y) / 2, peak_right_x - 1, crater_floor_y, Material::Lava);

    // Alpine lake on the left flank
    const int lake_x0 = sx(world, 0.12f);
    const int lake_x1 = sx(world, 0.28f);
    const int lake_y0 = sy(world, 0.65f);
    const int lake_y1 = sy(world, 0.82f);
    fill_rect(world, lake_x0, lake_y0, lake_x1, lake_y1, Material::Water);
    // Lake retaining dam / slope
    for (int y = lake_y0; y <= lake_y1; ++y) {
        world.set(lake_x0 - 1, y, Material::Stone);
    }

    // Forest on the foothills (trees with wood trunks and plant leaves)
    const int tree_xs[] = {sx(world, 0.05f), sx(world, 0.09f), sx(world, 0.85f), sx(world, 0.90f)};
    for (int tx : tree_xs) {
        const int trunk_h = std::max(4, static_cast<int>(0.07f * h));
        for (int dy = 0; dy < trunk_h; ++dy) {
            world.set(tx, base_y - dy, Material::Wood);
        }
        world.paint_circle(tx, base_y - trunk_h, std::max(2, static_cast<int>(0.035f * w)), Material::Plant);
    }

    // A few ash and smoke plumes above crater
    for (int i = 0; i < 8; ++i) {
        const int rx = chamber_cx + static_cast<int>(world.rng().range(-vent_w, vent_w));
        const int ry = summit_y - static_cast<int>(world.rng().range(1, std::max(2, h / 10)));
        world.set(rx, ry, Material::Smoke);
    }

    apply_border(world);
}

// ---------------------------------------------------------------------------
// Scene 1: Powder keg
// ---------------------------------------------------------------------------
void build_powder_keg(World& world) {
    world.clear();
    const int w = world.width();
    const int h = world.height();

    // Rocky ground terraces
    fill_rect(world, 1, sy(world, 0.80f), w - 2, h - 2, Material::Stone);
    fill_rect(world, 1, sy(world, 0.77f), sx(world, 0.40f), sy(world, 0.80f), Material::Sand);

    // Subterranean oil pool on the left
    const int oil_x0 = sx(world, 0.10f);
    const int oil_x1 = sx(world, 0.32f);
    const int oil_y0 = sy(world, 0.82f);
    const int oil_y1 = sy(world, 0.94f);
    fill_rect(world, oil_x0, oil_y0, oil_x1, oil_y1, Material::Oil);

    // Wooden shack on the central terrace
    const int shack_x0 = sx(world, 0.45f);
    const int shack_x1 = sx(world, 0.78f);
    const int shack_floor_y = sy(world, 0.80f);
    const int shack_roof_y = sy(world, 0.52f);

    // Shack floor and walls
    fill_rect(world, shack_x0, shack_floor_y, shack_x1, shack_floor_y + 1, Material::Wood);
    fill_rect(world, shack_x0, shack_roof_y, shack_x0 + 1, shack_floor_y, Material::Wood);
    fill_rect(world, shack_x1 - 1, shack_roof_y, shack_x1, shack_floor_y, Material::Wood);
    fill_rect(world, shack_x0, shack_roof_y, shack_x1, shack_roof_y + 1, Material::Wood);

    // Metal reinforcement beams
    fill_rect(world, (shack_x0 + shack_x1) / 2, shack_roof_y, (shack_x0 + shack_x1) / 2 + 1, shack_floor_y, Material::Metal);

    // Gunpowder stash inside the shack
    const int gp_x0 = shack_x0 + 3;
    const int gp_x1 = shack_x1 - 3;
    const int gp_y0 = sy(world, 0.65f);
    const int gp_y1 = shack_floor_y - 1;
    fill_rect(world, gp_x0, gp_y0, gp_x1, gp_y1, Material::Gunpowder);

    // Heavy stone blocks on roof
    fill_rect(world, shack_x0 + 2, shack_roof_y - std::max(2, h / 25), shack_x1 - 2, shack_roof_y - 1, Material::Stone);

    // Trailing fuse leading from outside into the gunpowder
    const int fuse_start_x = sx(world, 0.38f);
    for (int x = fuse_start_x; x <= gp_x0; ++x) {
        world.set(x, shack_floor_y - 1, Material::Wood);
    }

    // Light the fuse reliably by heating the first few cells above their ignite_temp (400 C)
    for (int x = fuse_start_x; x < fuse_start_x + 3 && x <= gp_x0; ++x) {
        if (Cell* c = world.try_at(x, shack_floor_y - 1)) {
            c->temp = 400.0f;
            c->flags |= kFlagBurning;
            c->aux = 120;
        }
    }

    apply_border(world);
}

// ---------------------------------------------------------------------------
// Scene 2: Glacier
// ---------------------------------------------------------------------------
void build_glacier(World& world) {
    world.clear();
    const int w = world.width();
    const int h = world.height();

    // Massive ice shelf on upper left
    const int ice_w = sx(world, 0.52f);
    const int ice_top = sy(world, 0.18f);
    const int ice_bot = sy(world, 0.52f);
    fill_rect(world, 1, ice_top, ice_w, ice_bot, Material::Ice);

    // Snow dusting on top of glacier
    fill_rect(world, 1, ice_top - std::max(1, h / 50), ice_w, ice_top, Material::Sand);

    // Stepped ice tongue projecting into the sea
    fill_rect(world, ice_w, sy(world, 0.35f), sx(world, 0.62f), ice_bot, Material::Ice);

    // Deep water ocean on right and below ice shelf
    const int water_top = sy(world, 0.48f);
    const int water_bot = sy(world, 0.82f);
    fill_rect(world, ice_w - 4, water_top, w - 2, water_bot, Material::Water);

    // Floating iceberg chunk in the open water
    world.paint_circle(sx(world, 0.76f), sy(world, 0.52f), std::max(2, static_cast<int>(0.04f * w)), Material::Ice);

    // Subterranean stone cavern at bottom holding hot lava
    fill_rect(world, 1, water_bot, w - 2, h - 2, Material::Stone);

    const int lava_x0 = sx(world, 0.35f);
    const int lava_x1 = sx(world, 0.85f);
    const int lava_y0 = sy(world, 0.88f);
    const int lava_y1 = sy(world, 0.96f);
    fill_rect(world, lava_x0, lava_y0, lava_x1, lava_y1, Material::Lava);

    // Narrow hydrothermal vent connecting lava cavern to ocean
    const int vent_x = (lava_x0 + lava_x1) / 2;
    fill_rect(world, vent_x - 1, water_bot, vent_x + 1, lava_y0, Material::Water);

    apply_border(world);
}

// ---------------------------------------------------------------------------
// Scene 3: Acid rain
// ---------------------------------------------------------------------------
void build_acid_rain(World& world) {
    world.clear();
    const int w = world.width();
    const int h = world.height();

    // Suspended metal reservoir holding acid
    const int res_x0 = sx(world, 0.12f);
    const int res_x1 = sx(world, 0.88f);
    const int res_y0 = sy(world, 0.08f);
    const int res_y1 = sy(world, 0.22f);

    // Metal container walls and floor
    fill_rect(world, res_x0, res_y0, res_x0 + 1, res_y1, Material::Metal);
    fill_rect(world, res_x1 - 1, res_y0, res_x1, res_y1, Material::Metal);
    fill_rect(world, res_x0, res_y1, res_x1, res_y1 + 1, Material::Metal);

    // Fill vat with Acid
    fill_rect(world, res_x0 + 2, res_y0 + 1, res_x1 - 2, res_y1 - 1, Material::Acid);

    // Drip nozzles in the bottom metal floor
    const int nozzles[] = {sx(world, 0.25f), sx(world, 0.42f), sx(world, 0.58f), sx(world, 0.75f)};
    for (int nx : nozzles) {
        world.set(nx, res_y1, Material::Empty);
        world.set(nx, res_y1 + 1, Material::Empty);
        world.set(nx, res_y1 + 2, Material::Acid);
        world.set(nx, res_y1 + 4, Material::Acid);
    }

    // City foundation floor
    fill_rect(world, 1, sy(world, 0.86f), w - 2, h - 2, Material::Stone);

    // Tower 1: Wood building (vulnerable to acid dissolution)
    fill_rect(world, sx(world, 0.20f), sy(world, 0.50f), sx(world, 0.30f), sy(world, 0.86f), Material::Wood);

    // Tower 2: Stone monument (acid-resistant)
    fill_rect(world, sx(world, 0.37f), sy(world, 0.42f), sx(world, 0.47f), sy(world, 0.86f), Material::Stone);

    // Tower 3: Metal skyscraper (impervious to acid)
    fill_rect(world, sx(world, 0.53f), sy(world, 0.36f), sx(world, 0.63f), sy(world, 0.86f), Material::Metal);

    // Tower 4: Rooftop plant garden on stone plinth
    fill_rect(world, sx(world, 0.70f), sy(world, 0.60f), sx(world, 0.80f), sy(world, 0.86f), Material::Stone);
    fill_rect(world, sx(world, 0.70f), sy(world, 0.55f), sx(world, 0.80f), sy(world, 0.60f), Material::Plant);

    apply_border(world);
}

// ---------------------------------------------------------------------------
// Scene 4: Garden
// ---------------------------------------------------------------------------
void build_garden(World& world) {
    world.clear();
    const int w = world.width();
    const int h = world.height();

    // Terraced stepped hills
    fill_rect(world, 1, sy(world, 0.78f), w - 2, h - 2, Material::Stone);
    fill_rect(world, sx(world, 0.30f), sy(world, 0.66f), w - 2, sy(world, 0.78f), Material::Stone);
    fill_rect(world, sx(world, 0.62f), sy(world, 0.52f), w - 2, sy(world, 0.66f), Material::Stone);

    // Sand / soil beds on top of each terrace
    fill_rect(world, 1, sy(world, 0.76f), sx(world, 0.30f), sy(world, 0.78f), Material::Sand);
    fill_rect(world, sx(world, 0.30f), sy(world, 0.64f), sx(world, 0.62f), sy(world, 0.66f), Material::Sand);
    fill_rect(world, sx(world, 0.62f), sy(world, 0.50f), w - 2, sy(world, 0.52f), Material::Sand);

    // Irrigation canals cut into the terraces
    fill_rect(world, sx(world, 0.08f), sy(world, 0.78f), sx(world, 0.22f), sy(world, 0.84f), Material::Water);
    fill_rect(world, sx(world, 0.40f), sy(world, 0.66f), sx(world, 0.52f), sy(world, 0.72f), Material::Water);
    fill_rect(world, sx(world, 0.72f), sy(world, 0.52f), sx(world, 0.84f), sy(world, 0.58f), Material::Water);

    // Dense plant foliage thriving along the canal banks
    fill_rect(world, sx(world, 0.05f), sy(world, 0.72f), sx(world, 0.08f), sy(world, 0.78f), Material::Plant);
    fill_rect(world, sx(world, 0.22f), sy(world, 0.72f), sx(world, 0.27f), sy(world, 0.78f), Material::Plant);
    fill_rect(world, sx(world, 0.35f), sy(world, 0.58f), sx(world, 0.40f), sy(world, 0.66f), Material::Plant);
    fill_rect(world, sx(world, 0.52f), sy(world, 0.58f), sx(world, 0.58f), sy(world, 0.66f), Material::Plant);
    fill_rect(world, sx(world, 0.66f), sy(world, 0.44f), sx(world, 0.72f), sy(world, 0.52f), Material::Plant);
    fill_rect(world, sx(world, 0.84f), sy(world, 0.44f), sx(world, 0.90f), sy(world, 0.52f), Material::Plant);

    // Wooden footbridge crossing canal
    const int bridge_x0 = sx(world, 0.38f);
    const int bridge_x1 = sx(world, 0.54f);
    fill_rect(world, bridge_x0, sy(world, 0.62f), bridge_x1, sy(world, 0.63f), Material::Wood);

    apply_border(world);
}

// ---------------------------------------------------------------------------
// Scene 5: Hourglass (Leak-proof 4-connected walls)
// ---------------------------------------------------------------------------
void build_hourglass(World& world) {
    world.clear();
    const int w = world.width();

    const int top_y = sy(world, 0.14f);
    const int bot_y = sy(world, 0.86f);
    const int mid_y = (top_y + bot_y) / 2;
    const int center_x = w / 2;
    const int max_radius = std::max(6, static_cast<int>(0.26f * w));
    const int neck_radius = std::max(1, static_cast<int>(0.02f * w));

    // Top and bottom stone pedestals (3 cells thick)
    fill_rect(world, center_x - max_radius - 4, top_y - 2, center_x + max_radius + 4, top_y, Material::Stone);
    fill_rect(world, center_x - max_radius - 4, bot_y, center_x + max_radius + 4, bot_y + 2, Material::Stone);

    // Hourglass bulb contours with 4-connected walls (at least 2 cells thick)
    int prev_left = center_x - max_radius;
    int prev_right = center_x + max_radius;

    for (int y = top_y; y <= bot_y; ++y) {
        float t = 0.0f;
        if (y < mid_y) {
            t = static_cast<float>(y - top_y) / static_cast<float>(std::max(1, mid_y - top_y));
        } else {
            t = static_cast<float>(bot_y - y) / static_cast<float>(std::max(1, bot_y - mid_y));
        }

        const int r = static_cast<int>(max_radius - t * (max_radius - neck_radius));
        const int left_x = center_x - r;
        const int right_x = center_x + r;

        // Draw solid wall segments connecting previous row to current row (thickness >= 3)
        const int min_l = std::min(prev_left, left_x);
        const int max_l = std::max(prev_left, left_x);
        for (int x = min_l - 2; x <= max_l; ++x) {
            world.set(x, y, Material::Stone);
        }

        const int min_r = std::min(prev_right, right_x);
        const int max_r = std::max(prev_right, right_x);
        for (int x = min_r; x <= max_r + 2; ++x) {
            world.set(x, y, Material::Stone);
        }

        // Fill upper chamber with sand (strictly inside chamber boundaries)
        if (y > top_y + 2 && y < mid_y - 4) {
            for (int x = max_l + 1; x < min_r; ++x) {
                world.set(x, y, Material::Sand);
            }
        }

        prev_left = left_x;
        prev_right = right_x;
    }

    // Sand trickling through the neck
    for (int y = mid_y - 4; y <= mid_y + 4; ++y) {
        world.set(center_x, y, Material::Sand);
    }

    apply_border(world);
}

// ---------------------------------------------------------------------------
// Scene 6: Sandbox (Plain stone border and walls only, LAST entry)
// ---------------------------------------------------------------------------
void build_sandbox(World& world) {
    world.clear();
    apply_border(world);
}

constexpr std::array<Scene, 7> kScenes{{
    {"Volcano", "Active volcanic mountain with lava vent, crater lake, and forest", &build_volcano},
    {"Powder keg", "Wooden shacks and oil reservoirs surrounding an ignited gunpowder cache", &build_powder_keg},
    {"Glacier", "Massive ice shelf over deep water with a subterranean lava fissure", &build_glacier},
    {"Acid rain", "Suspended acid reservoir slowly dripping onto a multi-material city", &build_acid_rain},
    {"Garden", "Lush terraced vegetation alongside flowing irrigation channels", &build_garden},
    {"Hourglass", "Classic stone hourglass with sand trickling through a narrow constriction", &build_hourglass},
    {"Sandbox", "Empty canvas with a stone border ready for your own creations", &build_sandbox},
}};

}  // namespace

std::span<const Scene> all() {
    return kScenes;
}

}  // namespace gf::scenes