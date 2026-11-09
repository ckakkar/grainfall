#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

#include "sim/simulation.hpp"
#include "testing.hpp"

using namespace gf;

namespace {

void run(Simulation& simulation, int ticks) {
    for (int i = 0; i < ticks; ++i) simulation.step();
}

void fill(World& world, Material material) {
    for (int y = 0; y < world.height(); ++y)
        for (int x = 0; x < world.width(); ++x) world.set(x, y, material);
}

std::array<int, kMaterialCount> counts(const World& world) {
    std::array<int, kMaterialCount> result{};
    for (const auto& cell : world.cells()) ++result[static_cast<int>(cell.mat)];
    return result;
}

bool same_cells(const World& a, const World& b) {
    if (a.width() != b.width() || a.height() != b.height() || a.tick() != b.tick()) return false;
    for (std::size_t i = 0; i < a.cells().size(); ++i) {
        const Cell& x = a.cells()[i];
        const Cell& y = b.cells()[i];
        if (x.mat != y.mat || x.shade != y.shade || x.clock != y.clock || x.flags != y.flags ||
            x.life != y.life || x.aux != y.aux || x.temp != y.temp) return false;
    }
    return true;
}

void mixed_world(World& world) {
    // Dense, mobile, and reactive, with persistent metal barriers and heat.
    constexpr std::array materials{Material::Empty, Material::Sand, Material::Water,
        Material::Oil, Material::Wood, Material::Metal, Material::Stone, Material::Lava,
        Material::Smoke, Material::Acid, Material::Plant, Material::Ice,
        Material::Steam, Material::Gunpowder, Material::Fire};
    for (int y = 0; y < world.height(); ++y) {
        for (int x = 0; x < world.width(); ++x) {
            const Material material = materials[world.rng().range(0, static_cast<int>(materials.size()) - 1)];
            world.set(x, y, material);
        }
    }
}

void pool_walls(World& world) {
    for (int x = 0; x < world.width(); ++x) world.set(x, world.height() - 1, Material::Stone);
    for (int y = 0; y < world.height(); ++y) {
        world.set(0, y, Material::Stone);
        world.set(world.width() - 1, y, Material::Stone);
    }
}

void check_settled_pool(World& world, Simulation& simulation, int water, int oil) {
    int minimum_surface = world.height(), maximum_surface = 0;
    int holes = 0, wrong_layers = 0;
    for (int x = 1; x < world.width() - 1; ++x) {
        int surface = world.height() - 1;
        bool seen_water = false;
        for (int y = 0; y < world.height() - 1; ++y) {
            const Material material = world.at(x, y).mat;
            if (material == Material::Water || material == Material::Oil) {
                surface = std::min(surface, y);
                if (material == Material::Oil && seen_water) ++wrong_layers;
                seen_water |= material == Material::Water;
            } else if (surface < y) {
                ++holes;
            }
        }
        minimum_surface = std::min(minimum_surface, surface);
        maximum_surface = std::max(maximum_surface, surface);
    }
    CHECK_EQ(holes, 0);
    CHECK_EQ(wrong_layers, 0);
    CHECK(maximum_surface - minimum_surface <= 1);
    CHECK_EQ(world.count(Material::Water), water);
    CHECK_EQ(world.count(Material::Oil), oil);
    int moves = 0;
    for (int tick = 0; tick < 50; ++tick) {
        simulation.step();
        moves += simulation.stats().moved_cells;
    }
    CHECK_EQ(moves, 0);
    CHECK(simulation.stats().active_chunks < simulation.stats().total_chunks / 2);
    // Stability must follow from the movement rules, not merely sleeping.
    simulation.wake_all();
    simulation.step();
    CHECK_EQ(simulation.stats().moved_cells, 0);
    std::cout << "    settled pool: " << holes << " holes, surface spread "
              << maximum_surface - minimum_surface << ", last 50 ticks " << moves << " moves\n";
}

}  // namespace

TEST(sim_sand_falls_once_per_tick_and_piles) {
    World world(41, 32, 7);
    world.set(20, 0, Material::Sand);
    Simulation simulation(world);
    simulation.step();
    CHECK(world.at(20, 1).mat == Material::Sand);
    CHECK_EQ(simulation.stats().moved_cells, 1);
    for (int y = 0; y < 12; ++y)
        for (int x = 17; x <= 23; ++x) world.set(x, y, Material::Sand);
    const int grains = world.count(Material::Sand);
    simulation.wake_all();
    run(simulation, 180);
    CHECK_EQ(world.count(Material::Sand), grains);
    CHECK(world.at(20, 31).mat == Material::Sand);
    int highest = 32;
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 41; ++x) {
            if (world.at(x, y).mat != Material::Sand) continue;
            highest = std::min(highest, y);
            if (y + 1 < 32) {
                CHECK(world.at(x, y + 1).mat != Material::Empty);
                if (x > 0) CHECK(world.at(x - 1, y + 1).mat != Material::Empty);
                if (x + 1 < 41) CHECK(world.at(x + 1, y + 1).mat != Material::Empty);
            }
        }
    }
    CHECK(highest < 29);
    CHECK_EQ(simulation.stats().moved_cells, 0);
}

TEST(sim_oil_floats_above_water) {
    World world(24, 24, 41);
    for (int y = 8; y < 16; ++y)
        for (int x = 0; x < 24; ++x) world.set(x, y, Material::Water);
    for (int y = 16; y < 24; ++y)
        for (int x = 0; x < 24; ++x) world.set(x, y, Material::Oil);
    const auto before = counts(world);
    Simulation simulation(world);
    run(simulation, 240);
    CHECK(counts(world) == before);
    for (int y = 8; y < 24; ++y)
        for (int x = 0; x < 24; ++x)
            CHECK(world.at(x, y).mat == (y < 16 ? Material::Oil : Material::Water));
}

TEST(sim_water_settles_flat) {
    World world(120, 60, 5);
    pool_walls(world);
    for (int x = 80; x < 118; ++x)
        for (int y = 15; y < 59; ++y) world.set(x, y, Material::Water);
    Simulation simulation(world);
    run(simulation, 1450);
    check_settled_pool(world, simulation, 38 * 44, 0);
}

TEST(sim_oil_water_settle_layered) {
    World world(120, 60, 54);
    pool_walls(world);
    // Start with dense water above oil and ample room to spread sideways.
    for (int x = 80; x < 118; ++x) {
        for (int y = 15; y < 37; ++y) world.set(x, y, Material::Water);
        for (int y = 37; y < 59; ++y) world.set(x, y, Material::Oil);
    }
    Simulation simulation(world);
    run(simulation, 1450);
    check_settled_pool(world, simulation, 38 * 22, 38 * 22);
}

TEST(sim_powder_sinks_through_liquid) {
    World world(1, 8, 2);
    fill(world, Material::Water);
    world.set(0, 0, Material::Sand);
    Simulation simulation(world);
    run(simulation, 7);
    CHECK(world.at(0, 7).mat == Material::Sand);
    CHECK_EQ(world.count(Material::Water), 7);
}

TEST(sim_gas_rises_layers_and_never_displaces_solids_or_liquids) {
    World world(1, 4, 4);
    world.set(0, 0, Material::Smoke);
    world.set(0, 1, Material::Steam);
    world.set(0, 2, Material::Water);
    world.set(0, 3, Material::Smoke);
    for (auto& cell : world.cells()) {
        cell.temp = 95.0f;
        cell.life = 30000;
    }
    Simulation simulation(world);
    simulation.step();
    CHECK(world.at(0, 0).mat == Material::Steam);
    CHECK(world.at(0, 1).mat == Material::Smoke);
    // Falling water may displace gas; gas cannot initiate the reverse swap.
    CHECK(world.at(0, 2).mat == Material::Smoke);
    CHECK(world.at(0, 3).mat == Material::Water);

    World blocked(3, 3, 8);
    fill(blocked, Material::Metal);
    blocked.set(1, 1, Material::Smoke);
    Simulation blocked_sim(blocked);
    run(blocked_sim, 30);
    CHECK(blocked.at(1, 1).mat == Material::Smoke);
}

TEST(sim_liquid_dispersion_stops_at_wall) {
    World world(12, 3, 22);
    for (int x = 0; x < 12; ++x) world.set(x, 2, Material::Metal);
    for (int y = 0; y < 3; ++y) {
        world.set(0, y, Material::Metal);
        world.set(4, y, Material::Metal);
    }
    world.set(2, 1, Material::Water);
    Simulation simulation(world);
    run(simulation, 60);
    CHECK_EQ(world.count(Material::Water), 1);
    for (int x = 5; x < 12; ++x) CHECK(world.at(x, 1).mat == Material::Empty);
}

TEST(sim_lava_water_contact_makes_stone_and_steam) {
    World world(8, 8, 4);
    world.set(3, 4, Material::Lava);
    world.set(4, 4, Material::Water);
    Simulation simulation(world);
    simulation.step();
    CHECK_EQ(world.count(Material::Lava), 0);
    CHECK_EQ(world.count(Material::Stone), 1);
    CHECK_EQ(world.count(Material::Steam), 1);
    CHECK_EQ(simulation.stats().reactions, 2);
}

TEST(sim_water_near_lava_boils_through_metal) {
    World world(3, 3, 18);
    fill(world, Material::Metal);
    world.set(0, 1, Material::Lava);
    world.set(2, 1, Material::Water);
    Simulation simulation(world);
    bool boiled = false;
    for (int i = 0; i < 120; ++i) {
        simulation.step();
        boiled |= world.count(Material::Steam) > 0;
    }
    CHECK(boiled);
    CHECK_EQ(world.count(Material::Metal), 7);
}

TEST(sim_phase_transitions_and_transient_expiry) {
    const auto check_phase = [](Material start, Material expected, float temperature, std::uint64_t seed) {
        World world(1, 1, seed);
        world.set(0, 0, start);
        world.at(0, 0).temp = temperature;
        Simulation simulation(world);
        run(simulation, 30);
        CHECK(world.at(0, 0).mat == expected);
        CHECK(std::isfinite(world.at(0, 0).temp));
    };
    check_phase(Material::Water, Material::Steam, 140.0f, 1);
    check_phase(Material::Water, Material::Ice, -20.0f, 2);
    check_phase(Material::Ice, Material::Water, 20.0f, 3);
    check_phase(Material::Steam, Material::Water, 70.0f, 4);
    check_phase(Material::Lava, Material::Stone, 500.0f, 5);
    check_phase(Material::Stone, Material::Lava, 1400.0f, 6);
    check_phase(Material::Sand, Material::Lava, 1400.0f, 7);
    World smoke(1, 1, 8);
    smoke.set(0, 0, Material::Smoke);
    smoke.at(0, 0).life = 1;
    Simulation smoke_sim(smoke);
    smoke_sim.step();
    CHECK(smoke.at(0, 0).mat == Material::Empty);
    int condensed = 0, vanished = 0, fire_smoke = 0;
    for (std::uint64_t seed = 1; seed <= 100; ++seed) {
        World steam(1, 1, seed);
        steam.set(0, 0, Material::Steam);
        steam.at(0, 0).life = 1;
        Simulation steam_sim(steam);
        steam_sim.step();
        condensed += steam.at(0, 0).mat == Material::Water;
        vanished += steam.at(0, 0).mat == Material::Empty;
        World fire(1, 1, seed);
        fire.set(0, 0, Material::Fire);
        fire.at(0, 0).life = 1;
        Simulation fire_sim(fire);
        fire_sim.step();
        fire_smoke += fire.at(0, 0).mat == Material::Smoke;
    }
    CHECK(condensed >= 15 && condensed <= 45);
    CHECK_EQ(condensed + vanished, 100);
    CHECK(fire_smoke >= 20 && fire_smoke <= 60);
}

TEST(sim_fire_burns_wood_and_leaves_smoke) {
    World world(16, 16, 27);
    // Confine the ignition source beside the wood for its full lifetime.
    for (int y = 4; y <= 11; ++y) {
        world.set(6, y, Material::Metal);
        world.set(9, y, Material::Metal);
    }
    for (int x = 7; x <= 8; ++x) {
        world.set(x, 4, Material::Metal);
        world.set(x, 11, Material::Metal);
    }
    for (int y = 5; y < 11; ++y) {
        world.set(7, y, Material::Wood);
        world.set(8, y, Material::Fire);
    }
    Simulation simulation(world);
    bool burning = false, smoke = false;
    for (int tick = 0; tick < 350; ++tick) {
        simulation.step();
        smoke |= world.count(Material::Smoke) > 0;
        for (const Cell& cell : world.cells()) burning |= (cell.flags & kFlagBurning) != 0;
    }
    CHECK(burning);
    CHECK(smoke);
    CHECK_EQ(world.count(Material::Wood), 0);
}

TEST(sim_oil_ignites_and_plant_burns) {
    World oil(1, 1, 36);
    oil.set(0, 0, Material::Oil);
    oil.at(0, 0).temp = 350.0f;
    Simulation oil_sim(oil);
    run(oil_sim, 10);
    CHECK_EQ(oil.count(Material::Oil), 0);
    CHECK_EQ(oil.count(Material::Fire), 1);
    World plant(1, 1, 37);
    plant.set(0, 0, Material::Plant);
    plant.at(0, 0).temp = 350.0f;
    Simulation plant_sim(plant);
    run(plant_sim, 120);
    CHECK_EQ(plant.count(Material::Plant), 0);
}

TEST(sim_large_fire_temperature_is_bounded) {
    World world(40, 24, 1110);
    for (int x = 0; x < 40; ++x) {
        world.set(x, 23, Material::Stone);
        world.set(x, 22, Material::Sand);
    }
    for (int y = 5; y <= 21; ++y) {
        for (int x = 6; x <= 33; ++x) {
            if (y <= 7 || x <= 8 || x >= 31 || y == 15)
                world.set(x, y, Material::Wood);
        }
    }
    for (int y = 16; y <= 21; ++y)
        for (int x = 7; x <= 8; ++x) world.at(x, y).temp = 500.0f;
    const int wood = world.count(Material::Wood);
    Simulation simulation(world);
    float maximum = 0.0f;
    bool burned = false, smoke = false;
    for (int tick = 0; tick < 600; ++tick) {
        simulation.step();
        burned |= world.count(Material::Wood) < wood;
        smoke |= world.count(Material::Smoke) > 0;
        for (const Cell& cell : world.cells()) {
            REQUIRE(std::isfinite(cell.temp));
            maximum = std::max(maximum, cell.temp);
            if (cell.mat == Material::Fire) CHECK(cell.temp <= 1100.0f);
        }
        REQUIRE(maximum <= 1500.0f);
        REQUIRE(world.count(Material::Lava) == 0);
    }
    CHECK(burned);
    CHECK(smoke);
    CHECK_EQ(world.count(Material::Wood), 0);
    CHECK_EQ(world.count(Material::Stone), 40);
    CHECK_EQ(world.count(Material::Sand), 40);
    std::cout << "    large fire peak: " << maximum << " C\n";
}

TEST(sim_gunpowder_chains_destroy_sand_and_preserve_metal) {
    World world(56, 24, 44);
    for (int x = 4; x <= 44; x += 4) world.set(x, 12, Material::Gunpowder);
    for (int x = 2; x < 50; ++x) {
        world.set(x, 13, Material::Sand);
        world.set(x, 14, Material::Metal);
    }
    world.at(4, 12).flags |= kFlagIgnited;
    Simulation simulation(world);
    simulation.step();
    CHECK_EQ(world.count(Material::Gunpowder), 0);
    CHECK_EQ(simulation.stats().explosions, 11);
    CHECK(world.count(Material::Sand) < 48);
    CHECK_EQ(world.count(Material::Metal), 48);
    CHECK(world.count(Material::Fire) > 0);
    CHECK(world.at(42, 14).temp > kAmbientTemp);
    for (const Cell& cell : world.cells()) CHECK(std::isfinite(cell.temp));
}

TEST(sim_external_explosion_wakes_flings_and_clips_to_world) {
    World world(80, 80, 82);
    world.set(34, 40, Material::Stone);
    world.set(35, 40, Material::Metal);
    Simulation simulation(world);
    run(simulation, 2);
    CHECK(!simulation.chunk_awake(40, 40));
    for (int y = 35; y <= 45; ++y)
        for (int x = 36; x <= 45; ++x) world.set(x, y, Material::Sand);
    simulation.explode(40, 40, 7);
    CHECK(simulation.chunk_awake(40, 40));
    CHECK(world.at(34, 40).mat == Material::Stone);
    CHECK(world.at(35, 40).mat == Material::Metal);
    int flung = 0;
    for (int y = 0; y < 80; ++y)
        for (int x = 0; x < 80; ++x)
            if ((x - 40) * (x - 40) + (y - 40) * (y - 40) > 49 &&
                world.at(x, y).mat == Material::Sand) ++flung;
    CHECK(flung > 0);
    simulation.explode(0, 0, 8);
    simulation.explode(79, 79, 0);
    simulation.explode(-1, 0, 8);
    simulation.explode(1, 1, -1);
    run(simulation, 5);
    CHECK_EQ(world.count(Material::Metal), 1);

    World small(10, 10, 83);
    fill(small, Material::Wood);
    Simulation small_sim(small);
    small_sim.explode(0, 0, std::numeric_limits<int>::max());
    CHECK_EQ(small.count(Material::Wood), 0);
}

TEST(sim_acid_dissolves_wood_but_not_metal_or_stone) {
    World world(3, 3, 95);
    fill(world, Material::Metal);
    world.set(1, 0, Material::Stone);
    world.set(1, 1, Material::Acid);
    world.set(1, 2, Material::Wood);
    Simulation simulation(world);
    run(simulation, 120);
    CHECK_EQ(world.count(Material::Wood), 0);
    CHECK_EQ(world.count(Material::Metal), 6);
    CHECK_EQ(world.count(Material::Stone), 1);
}

TEST(sim_plant_growth_and_ice_freezing) {
    World plant(16, 16, 123);
    fill(plant, Material::Plant);
    plant.set(8, 8, Material::Water);
    Simulation plant_sim(plant);
    run(plant_sim, 1600);
    CHECK_EQ(plant.count(Material::Water), 0);
    CHECK_EQ(plant.count(Material::Plant), 256);

    World ice(16, 16, 124);
    fill(ice, Material::Ice);
    ice.set(8, 8, Material::Water);
    Simulation ice_sim(ice);
    run(ice_sim, 100);
    CHECK_EQ(ice.count(Material::Water), 0);
    CHECK_EQ(ice.count(Material::Ice), 256);
}

TEST(sim_movement_conserves_materials_and_particle_identity) {
    World world(65, 49, 151);
    constexpr std::array materials{Material::Empty, Material::Sand, Material::Water,
        Material::Oil, Material::Gunpowder, Material::Metal, Material::Stone};
    std::array<std::vector<int>, kMaterialCount> identities;
    int id = 0;
    for (int y = 0; y < world.height(); ++y) {
        for (int x = 0; x < world.width(); ++x) {
            const Material mat = materials[world.rng().range(0, 6)];
            world.set(x, y, mat);
            if (mat != Material::Empty) {
                world.at(x, y).aux = static_cast<std::int16_t>(++id);
                identities[static_cast<int>(mat)].push_back(id);
            }
        }
    }
    const auto before = counts(world);
    Simulation simulation(world);
    for (int tick = 0; tick < 650; ++tick) {
        simulation.step();
        if (tick % 50 == 0) CHECK(counts(world) == before);
    }
    std::array<std::vector<int>, kMaterialCount> after;
    for (const Cell& cell : world.cells())
        if (cell.mat != Material::Empty) after[static_cast<int>(cell.mat)].push_back(cell.aux);
    for (int mat = 0; mat < kMaterialCount; ++mat) {
        std::sort(after[mat].begin(), after[mat].end());
        CHECK(after[mat] == identities[mat]);
    }
    CHECK(counts(world) == before);
}

TEST(sim_same_seed_identical_after_500_ticks) {
    World a(65, 49, 1984), b(65, 49, 1984);
    mixed_world(a);
    mixed_world(b);
    Simulation sa(a), sb(b);
    for (int i = 0; i < 500; ++i) {
        sa.step();
        sb.step();
        CHECK_EQ(sa.stats().moved_cells, sb.stats().moved_cells);
        CHECK_EQ(sa.stats().reactions, sb.stats().reactions);
        CHECK_EQ(sa.stats().explosions, sb.stats().explosions);
    }
    CHECK(same_cells(a, b));
    CHECK_EQ(a.rng().next_u64(), b.rng().next_u64());
    World restored = a;
    Simulation resumed(restored);
    sa.step();
    resumed.step();
    CHECK(same_cells(a, restored));
    CHECK_EQ(a.rng().next_u64(), restored.rng().next_u64());
}

TEST(sim_chunks_sleep_wake_and_clock_wrap_is_safe) {
    World world(81, 67, 73);
    for (int x = 0; x < 81; ++x) world.set(x, 66, Material::Sand);
    Simulation simulation(world);
    run(simulation, 300);
    CHECK_EQ(simulation.stats().active_chunks, 0);
    CHECK_EQ(simulation.stats().total_chunks, 30);
    CHECK(!simulation.chunk_awake(80, 66));
    CHECK(!simulation.chunk_awake(-1, 0));
    world.set(80, 0, Material::Sand);
    world.at(80, 0).clock = static_cast<std::uint8_t>(301);
    simulation.wake_rect(80, 0, 1000, 2);
    CHECK(simulation.chunk_awake(80, 0));
    CHECK(!simulation.chunk_awake(0, 66));
    simulation.step();
    CHECK(world.at(80, 1).mat == Material::Sand);
    run(simulation, 140);
    CHECK_EQ(simulation.stats().active_chunks, 0);
    simulation.wake_rect(1000, 1000, 2000, 2000);
    CHECK(!simulation.chunk_awake(80, 66));
    simulation.wake_rect(80, 66, -100, -100);
    CHECK(simulation.chunk_awake(0, 0));
    CHECK(simulation.chunk_awake(80, 66));
    simulation.step();
    simulation.wake_all();
    simulation.step();
    CHECK_EQ(simulation.stats().active_chunks, simulation.stats().total_chunks);
    CHECK_EQ(simulation.stats().tick, world.tick());
    CHECK(simulation.stats().step_ms >= 0.0);
}

TEST(sim_sleeping_and_forced_awake_conserve_same_counts) {
    World a(65, 49, 521), b(65, 49, 521);
    for (int y = 30; y < 49; ++y) {
        for (int x = 0; x < 65; ++x) {
            const Material material = ((x + y) % 3 == 0) ? Material::Oil : Material::Water;
            a.set(x, y, material);
            b.set(x, y, material);
        }
    }
    Simulation sa(a), sb(b);
    const auto before = counts(a);
    for (int i = 0; i < 500; ++i) {
        sa.step();
        sb.wake_all();
        sb.step();
    }
    CHECK(counts(a) == before);
    CHECK(counts(b) == before);
    CHECK(same_cells(a, b));
    CHECK_EQ(a.rng().next_u64(), b.rng().next_u64());
}

TEST(sim_sleeping_does_not_change_remote_reaction_randomness) {
    World a(128, 64, 841), b(128, 64, 841);
    const auto prepare = [](World& world) {
        for (int y = 55; y < 64; ++y)
            for (int x = 0; x < 32; ++x) world.set(x, y, Material::Sand);
        for (int y = 28; y <= 32; ++y)
            for (int x = 104; x <= 108; ++x) world.set(x, y, Material::Metal);
        world.set(106, 30, Material::Acid);
        world.set(106, 31, Material::Wood);
        world.set(106, 29, Material::Wood);
    };
    prepare(a);
    prepare(b);
    Simulation sa(a), sb(b);
    for (int i = 0; i < 200; ++i) {
        sa.step();
        sb.wake_all();
        sb.step();
        REQUIRE(counts(a) == counts(b));
    }
    CHECK(same_cells(a, b));
    CHECK_EQ(a.rng().next_u64(), b.rng().next_u64());
}

TEST(sim_chain_blast_wakes_distant_sleeping_chunks_immediately) {
    World a(160, 64, 844), b(160, 64, 844);
    const auto prepare = [](World& world) {
        for (int x = 0; x < 160; ++x) world.set(x, 63, Material::Metal);
        for (int x = 4; x <= 148; x += 4) world.set(x, 62, Material::Gunpowder);
    };
    prepare(a);
    prepare(b);
    Simulation sa(a), sb(b);
    run(sa, 3);
    run(sb, 3);
    REQUIRE(sa.stats().active_chunks == 0);
    a.at(8, 62).flags |= kFlagIgnited;
    b.at(8, 62).flags |= kFlagIgnited;
    sa.wake_rect(8, 62, 8, 62);
    sb.wake_all();
    sa.step();
    sb.step();
    CHECK(counts(a) == counts(b));
    CHECK(same_cells(a, b));
    CHECK_EQ(a.rng().next_u64(), b.rng().next_u64());
}

TEST(sim_heat_is_stable_and_metal_conducts_faster_than_wood) {
    const auto conducted = [](Material material) {
        World world(3, 1, 66);
        fill(world, material);
        world.at(0, 0).temp = 250.0f;
        Simulation simulation(world);
        simulation.step();
        return world.at(1, 0).temp;
    };
    CHECK(conducted(Material::Metal) > conducted(Material::Wood) + 20.0f);
    World world(33, 17, 68);
    fill(world, Material::Metal);
    for (std::size_t i = 0; i < world.cells().size(); ++i)
        world.cells()[i].temp = i % 2 == 0 ? 20000.0f : -273.15f;
    world.at(0, 0).temp = std::numeric_limits<float>::quiet_NaN();
    world.at(1, 0).temp = std::numeric_limits<float>::infinity();
    world.at(2, 0).temp = -std::numeric_limits<float>::infinity();
    Simulation simulation(world);
    for (int tick = 0; tick < 100; ++tick) {
        simulation.step();
        for (const Cell& cell : world.cells()) {
            REQUIRE(std::isfinite(cell.temp));
            CHECK(cell.temp >= -273.15f && cell.temp <= 20000.0f);
        }
    }
    CHECK_EQ(world.count(Material::Metal), 33 * 17);
}

TEST(sim_performance_320x200_fully_awake) {
    World world(320, 200, 2026);
    mixed_world(world);
    Simulation simulation(world);
    double elapsed = 0.0;
    constexpr int ticks = 180;
    for (int tick = 0; tick < ticks; ++tick) {
        simulation.wake_all();
        simulation.step();
        elapsed += simulation.stats().step_ms;
        REQUIRE(simulation.stats().active_chunks == simulation.stats().total_chunks);
    }
    const double average = elapsed / ticks;
    std::cout << "    fully awake 320x200: " << average << " ms/step\n";
#ifdef NDEBUG
    CHECK(average < 4.0);
#endif
    World movement(320, 200, 2027);
    constexpr std::array cold_materials{Material::Sand, Material::Water, Material::Oil,
        Material::Gunpowder, Material::Empty};
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 320; ++x)
            movement.set(x, y, cold_materials[movement.rng().range(0, 4)]);
    Simulation moving(movement);
    double movement_ms = 0.0;
    for (int tick = 0; tick < ticks; ++tick) {
        moving.wake_all();
        moving.step();
        movement_ms += moving.stats().step_ms;
    }
    std::cout << "    fully awake dense movement 320x200: " << movement_ms / ticks << " ms/step\n";
#ifdef NDEBUG
    CHECK(movement_ms / ticks < 4.0);
#endif
    World settled(320, 200, 2026);
    for (int x = 0; x < 320; ++x) settled.set(x, 199, Material::Sand);
    Simulation sleeping(settled);
    run(sleeping, 2);
    const auto start = std::chrono::steady_clock::now();
    run(sleeping, 1000);
    const double asleep_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count() / 1000.0;
    std::cout << "    settled 320x200: " << asleep_ms << " ms/step\n";
    CHECK_EQ(sleeping.stats().active_chunks, 0);
}
