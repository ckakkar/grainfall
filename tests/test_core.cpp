// Tests for src/core (lead-owned).
#include <set>

#include "core/material.hpp"
#include "core/rng.hpp"
#include "core/world.hpp"
#include "testing.hpp"

using namespace gf;

TEST(core_material_table_is_complete) {
    const auto& table = material_table();
    CHECK_EQ(static_cast<int>(table.size()), kMaterialCount);
    std::set<std::string_view> names;
    for (const auto& mi : table) {
        CHECK(!mi.name.empty());
        names.insert(mi.name);
        CHECK(mi.life_min <= mi.life_max);
    }
    CHECK_EQ(static_cast<int>(names.size()), kMaterialCount);
    CHECK(phase_of(Material::Empty) == Phase::Empty);
    CHECK(is_movable(Material::Sand));
    CHECK(!is_movable(Material::Stone));
}

TEST(core_rng_is_deterministic_and_in_range) {
    Rng a(42), b(42), c(43);
    bool differs = false;
    for (int i = 0; i < 1000; ++i) {
        const auto va = a.next_u64();
        CHECK_EQ(va, b.next_u64());
        if (va != c.next_u64()) differs = true;
        const float u = a.uniform();
        b.uniform();
        c.uniform();
        CHECK(u >= 0.0f && u < 1.0f);
        const int r = a.range(-3, 5);
        b.range(-3, 5);
        c.range(-3, 5);
        CHECK(r >= -3 && r <= 5);
    }
    CHECK(differs);
}

TEST(core_world_set_and_bounds) {
    World w(8, 4, 1);
    CHECK_EQ(w.width(), 8);
    CHECK_EQ(w.height(), 4);
    CHECK(w.try_at(-1, 0) == nullptr);
    CHECK(w.try_at(8, 0) == nullptr);
    CHECK(w.try_at(0, 4) == nullptr);
    REQUIRE(w.try_at(7, 3) != nullptr);

    w.set(3, 2, Material::Lava);
    CHECK(w.at(3, 2).mat == Material::Lava);
    CHECK_EQ(w.at(3, 2).temp, info(Material::Lava).spawn_temp);
    w.set(100, 100, Material::Sand);  // ignored, must not crash
    CHECK_EQ(w.count(Material::Lava), 1);

    w.set(0, 0, Material::Fire);
    const auto life = w.at(0, 0).life;
    CHECK(life >= info(Material::Fire).life_min && life <= info(Material::Fire).life_max);

    w.clear();
    CHECK_EQ(w.count(Material::Empty), 32);
}

TEST(core_world_paint_circle) {
    World w(21, 21, 7);
    w.paint_circle(10, 10, 3, Material::Sand);
    CHECK(w.at(10, 10).mat == Material::Sand);
    CHECK(w.at(13, 10).mat == Material::Sand);
    CHECK(w.at(13, 13).mat == Material::Empty);  // outside the radius
    const int sand = w.count(Material::Sand);
    CHECK(sand > 20 && sand < 40);

    w.set(10, 10, Material::Stone);
    w.paint_circle(10, 10, 3, Material::Water, /*only_empty=*/true);
    CHECK(w.at(10, 10).mat == Material::Stone);
    CHECK(w.at(11, 10).mat == Material::Sand);

    w.paint_circle(0, 0, 5, Material::Oil);  // clipped at the corner, must not crash
    CHECK(w.at(0, 0).mat == Material::Oil);
}
