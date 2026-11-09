#include <algorithm>
#include <set>
#include <string_view>

#include "core/material.hpp"
#include "core/world.hpp"
#include "scenes/scenes.hpp"
#include "sim/simulation.hpp"
#include "testing.hpp"

using namespace gf;
using namespace gf::scenes;

TEST(scene_names_unique_and_non_empty) {
    const auto list = all();
    // Must have 6-8 scenes
    CHECK(list.size() >= 6 && list.size() <= 8);

    std::set<std::string_view> names;
    for (const auto& s : list) {
        CHECK(!s.name.empty());
        CHECK(!s.description.empty());
        REQUIRE(s.build != nullptr);

        // Names must be unique
        CHECK(names.find(s.name) == names.end());
        names.insert(s.name);
    }

    // Most impressive scene first
    CHECK_EQ(list.front().name, "Volcano");

    // Plain "Sandbox" must be the LAST entry
    CHECK_EQ(list.back().name, "Sandbox");
}

TEST(scene_builds_on_various_resolutions) {
    const auto list = all();
    const struct {
        int w;
        int h;
    } resolutions[] = {{64, 64}, {320, 180}, {640, 360}};

    for (const auto& res : resolutions) {
        for (const auto& s : list) {
            World world(res.w, res.h, 0x1234ull);
            s.build(world);

            CHECK_EQ(world.width(), res.w);
            CHECK_EQ(world.height(), res.h);

            // Must be non-empty (at least the border contains Stone cells)
            int non_empty_count = 0;
            for (const auto& c : world.cells()) {
                if (c.mat != Material::Empty) {
                    ++non_empty_count;
                }
            }
            CHECK(non_empty_count > 0);

            // For non-sandbox scenes, verify there are content cells inside the inner region
            if (s.name != "Sandbox") {
                int inner_non_empty = 0;
                for (int y = 0; y < res.h - 1; ++y) {
                    for (int x = 1; x < res.w - 1; ++x) {
                        if (world.at(x, y).mat != Material::Empty) {
                            ++inner_non_empty;
                        }
                    }
                }
                CHECK(inner_non_empty > 0);
            }
        }
    }
}

TEST(scene_determinism) {
    const auto list = all();

    for (const auto& s : list) {
        const std::uint64_t seed = 0xabcdef0123456789ull;
        World w1(120, 80, seed);
        World w2(120, 80, seed);

        s.build(w1);
        s.build(w2);

        REQUIRE(w1.cells().size() == w2.cells().size());
        for (int y = 0; y < 80; ++y) {
            for (int x = 0; x < 120; ++x) {
                CHECK(w1.at(x, y).mat == w2.at(x, y).mat);
            }
        }
    }
}

TEST(scene_border_rule_holds) {
    const auto list = all();
    const struct {
        int w;
        int h;
    } resolutions[] = {{64, 64}, {320, 180}, {640, 360}};

    for (const auto& res : resolutions) {
        for (const auto& s : list) {
            World world(res.w, res.h, 42);
            s.build(world);

            // Left edge: x = 0, all y
            for (int y = 0; y < res.h; ++y) {
                CHECK(world.at(0, y).mat == Material::Stone);
            }

            // Right edge: x = w - 1, all y
            for (int y = 0; y < res.h; ++y) {
                CHECK(world.at(res.w - 1, y).mat == Material::Stone);
            }

            // Bottom edge: y = h - 1, all x
            for (int x = 0; x < res.w; ++x) {
                CHECK(world.at(x, res.h - 1).mat == Material::Stone);
            }
        }
    }
}

TEST(scene_hourglass_holds_sand) {
    const auto list = all();
    const auto it = std::find_if(list.begin(), list.end(), [](const Scene& s) { return s.name == "Hourglass"; });
    REQUIRE(it != list.end());

    World world(320, 180, 42);
    it->build(world);

    const int initial_sand = world.count(Material::Sand);
    REQUIRE(initial_sand > 0);

    const int w = world.width();
    const int center_x = w / 2;
    const int max_radius = std::max(6, static_cast<int>(0.26f * w));
    const int top_y = static_cast<int>(0.14f * 180.0f);
    const int bot_y = static_cast<int>(0.86f * 180.0f);

    const int bbox_min_x = center_x - max_radius - 6;
    const int bbox_max_x = center_x + max_radius + 6;
    const int bbox_min_y = top_y - 4;
    const int bbox_max_y = bot_y + 4;

    gf::Simulation sim(world);
    for (int step = 0; step < 1500; ++step) {
        sim.step();
    }

    // Sand count must be completely conserved
    CHECK_EQ(world.count(Material::Sand), initial_sand);

    // No sand must end up outside the hourglass bounding box
    for (int y = 0; y < world.height(); ++y) {
        for (int x = 0; x < world.width(); ++x) {
            if (world.at(x, y).mat == Material::Sand) {
                CHECK(x >= bbox_min_x && x <= bbox_max_x);
                CHECK(y >= bbox_min_y && y <= bbox_max_y);
            }
        }
    }
}

TEST(scene_powder_keg_explodes) {
    const auto list = all();
    const auto it = std::find_if(list.begin(), list.end(), [](const Scene& s) { return s.name == "Powder keg"; });
    REQUIRE(it != list.end());

    World world(320, 180, 12345);
    it->build(world);

    const int initial_gunpowder = world.count(Material::Gunpowder);
    REQUIRE(initial_gunpowder > 0);

    gf::Simulation sim(world);
    for (int step = 0; step < 2500; ++step) {
        sim.step();
    }

    const int final_gunpowder = world.count(Material::Gunpowder);
    // Gunpowder count must drop below 10% of the start
    CHECK(final_gunpowder < (initial_gunpowder / 10));
}
