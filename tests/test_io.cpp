#include <bit>
#include <cstdio>
#include <string>
#include <vector>

#include "core/material.hpp"
#include "core/rng.hpp"
#include "core/world.hpp"
#include "io/save.hpp"
#include "testing.hpp"

using namespace gf;
using namespace gf::io;

namespace {

void fix_checksum(std::vector<std::uint8_t>& buf) {
    if (buf.size() < 4) return;
    const std::uint32_t ck = fnv1a(std::span<const std::uint8_t>(buf.data(), buf.size() - 4));
    buf[buf.size() - 4] = static_cast<std::uint8_t>(ck & 0xffu);
    buf[buf.size() - 3] = static_cast<std::uint8_t>((ck >> 8) & 0xffu);
    buf[buf.size() - 2] = static_cast<std::uint8_t>((ck >> 16) & 0xffu);
    buf[buf.size() - 1] = static_cast<std::uint8_t>((ck >> 24) & 0xffu);
}

}  // namespace

TEST(io_fnv1a_vector) {
    const std::uint8_t a = 'a';
    CHECK_EQ(fnv1a(std::span<const std::uint8_t>(&a, 1)), 0xe40c292cu);

    // Empty span should produce the standard 32-bit FNV offset basis
    CHECK_EQ(fnv1a({}), 0x811c9dc5u);
}

TEST(io_empty_world_size_under_100_bytes) {
    World w(320, 200, 0x12345ull);
    const auto bytes = encode(w);
    CHECK(bytes.size() < 100);

    // Header 32 + 1 run (13 bytes) + 4 checksum = 49 bytes
    CHECK_EQ(bytes.size(), 49u);

    std::string err;
    auto loaded = decode(bytes, &err);
    REQUIRE(loaded.has_value());
    CHECK_EQ(loaded->width(), 320);
    CHECK_EQ(loaded->height(), 200);
    CHECK_EQ(loaded->seed(), 0x12345ull);
    CHECK_EQ(loaded->tick(), 0ull);
    for (int y = 0; y < 200; ++y) {
        for (int x = 0; x < 320; ++x) {
            CHECK(loaded->at(x, y).mat == Material::Empty);
        }
    }
}

TEST(io_exact_round_trip_all_materials) {
    const int w = 45;
    const int h = 20;
    World world(w, h, 0xcafe'babe'dead'beefull);
    world.set_tick(9876543210ull);

    std::vector<bool> material_included(kMaterialCount, false);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int idx = y * w + x;
            const auto mat = static_cast<Material>(idx % kMaterialCount);
            material_included[static_cast<std::size_t>(mat)] = true;

            Cell c;
            c.mat = mat;
            c.shade = static_cast<std::uint8_t>((x * 17 + y * 31) & 0xff);
            c.clock = static_cast<std::uint8_t>((x + y) & 0xff);  // Must NOT be saved; loads as 0
            c.flags = static_cast<std::uint8_t>((x % 3 == 0) ? kFlagBurning : ((x % 3 == 1) ? kFlagIgnited : 0));
            c.life = static_cast<std::int16_t>((x * 100) - (y * 50));
            c.aux = static_cast<std::int16_t>((y * 300) - (x * 75));

            // Varied temperatures including negative, zero, fraction, high temp
            switch ((x + y) % 5) {
                case 0: c.temp = -273.15f; break;
                case 1: c.temp = 0.0f; break;
                case 2: c.temp = 20.0f; break;
                case 3: c.temp = 100.25f; break;
                default: c.temp = 1450.875f; break;
            }

            world.at(x, y) = c;
        }
    }

    // Verify all materials were tested
    for (int m = 0; m < kMaterialCount; ++m) {
        REQUIRE(material_included[static_cast<std::size_t>(m)]);
    }

    const auto bytes = encode(world);
    std::string err;
    auto loaded = decode(bytes, &err);
    REQUIRE(loaded.has_value());
    CHECK(err.empty());

    CHECK_EQ(loaded->width(), world.width());
    CHECK_EQ(loaded->height(), world.height());
    CHECK_EQ(loaded->seed(), world.seed());
    CHECK_EQ(loaded->tick(), world.tick());

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const Cell& orig = world.at(x, y);
            const Cell& dec = loaded->at(x, y);

            CHECK(dec.mat == orig.mat);
            CHECK_EQ(dec.shade, orig.shade);
            CHECK_EQ(dec.clock, 0);  // Cell::clock always loads as 0
            CHECK_EQ(dec.flags, orig.flags);
            CHECK_EQ(dec.life, orig.life);
            CHECK_EQ(dec.aux, orig.aux);
            // Compare temp bit-for-bit
            CHECK_EQ(std::bit_cast<std::uint32_t>(dec.temp), std::bit_cast<std::uint32_t>(orig.temp));
        }
    }
}

TEST(io_reject_wrong_magic) {
    World w(10, 10, 1);
    auto bytes = encode(w);
    bytes[0] = 'X';
    fix_checksum(bytes);

    std::string err;
    auto res = decode(bytes, &err);
    CHECK(!res.has_value());
    CHECK(err.find("wrong magic") != std::string::npos);
}

TEST(io_reject_unknown_version) {
    World w(10, 10, 1);
    auto bytes = encode(w);
    bytes[4] = 2;
    bytes[5] = 0;
    fix_checksum(bytes);

    std::string err;
    auto res = decode(bytes, &err);
    CHECK(!res.has_value());
    CHECK(err.find("unknown version") != std::string::npos);
}

TEST(io_reject_nonzero_reserved) {
    World w(10, 10, 1);
    auto bytes = encode(w);
    bytes[6] = 1;
    bytes[7] = 0;
    fix_checksum(bytes);

    std::string err;
    auto res = decode(bytes, &err);
    CHECK(!res.has_value());
    CHECK(err.find("nonzero reserved") != std::string::npos);
}

TEST(io_reject_out_of_range_dimensions) {
    World w(10, 10, 1);

    // width = 0
    {
        auto bytes = encode(w);
        bytes[8] = 0; bytes[9] = 0; bytes[10] = 0; bytes[11] = 0;
        fix_checksum(bytes);
        std::string err;
        auto res = decode(bytes, &err);
        CHECK(!res.has_value());
        CHECK(err.find("out-of-range dimensions") != std::string::npos);
    }
    // width = 4097
    {
        auto bytes = encode(w);
        bytes[8] = 0x01; bytes[9] = 0x10; bytes[10] = 0; bytes[11] = 0;
        fix_checksum(bytes);
        std::string err;
        auto res = decode(bytes, &err);
        CHECK(!res.has_value());
        CHECK(err.find("out-of-range dimensions") != std::string::npos);
    }
    // height = 0
    {
        auto bytes = encode(w);
        bytes[12] = 0; bytes[13] = 0; bytes[14] = 0; bytes[15] = 0;
        fix_checksum(bytes);
        std::string err;
        auto res = decode(bytes, &err);
        CHECK(!res.has_value());
        CHECK(err.find("out-of-range dimensions") != std::string::npos);
    }
    // height = 4097
    {
        auto bytes = encode(w);
        bytes[12] = 0x01; bytes[13] = 0x10; bytes[14] = 0; bytes[15] = 0;
        fix_checksum(bytes);
        std::string err;
        auto res = decode(bytes, &err);
        CHECK(!res.has_value());
        CHECK(err.find("out-of-range dimensions") != std::string::npos);
    }
}

TEST(io_reject_invalid_material) {
    World w(4, 4, 1);
    auto bytes = encode(w);
    // Offset 32 is start of cell section: 32..33 count, 34 mat
    bytes[34] = static_cast<std::uint8_t>(kMaterialCount);
    fix_checksum(bytes);

    std::string err;
    auto res = decode(bytes, &err);
    CHECK(!res.has_value());
    CHECK(err.find("material byte >= Material::Count") != std::string::npos);

    // Also test 255
    bytes[34] = 255;
    fix_checksum(bytes);
    res = decode(bytes, &err);
    CHECK(!res.has_value());
    CHECK(err.find("material byte >= Material::Count") != std::string::npos);
}

TEST(io_reject_runs_overflow) {
    World w(2, 2, 1);  // 4 cells
    auto bytes = encode(w);
    // Modify run count from 4 to 5
    bytes[32] = 5;
    bytes[33] = 0;
    fix_checksum(bytes);

    std::string err;
    auto res = decode(bytes, &err);
    CHECK(!res.has_value());
    CHECK(err.find("runs that overflow width*height") != std::string::npos);
}

TEST(io_reject_too_few_cells) {
    World w(2, 2, 1);  // 4 cells
    auto bytes = encode(w);
    // Modify run count from 4 to 3
    bytes[32] = 3;
    bytes[33] = 0;
    fix_checksum(bytes);

    std::string err;
    auto res = decode(bytes, &err);
    CHECK(!res.has_value());
    CHECK(err.find("too few cells") != std::string::npos);
}

TEST(io_reject_trailing_bytes) {
    World w(2, 2, 1);
    auto bytes = encode(w);
    // Insert an extra byte before checksum
    bytes.insert(bytes.end() - 4, 0x77);
    fix_checksum(bytes);

    std::string err;
    auto res = decode(bytes, &err);
    CHECK(!res.has_value());
    CHECK(err.find("trailing bytes") != std::string::npos);
}

TEST(io_reject_truncation) {
    World w(4, 4, 1);
    const auto valid = encode(w);

    const std::vector<std::size_t> truncate_lens = {
        0, 1, 2, 3, 4, 10, 31, 32, 33, 34, 35
    };

    for (std::size_t len : truncate_lens) {
        if (len >= valid.size()) continue;
        std::vector<std::uint8_t> truncated(valid.begin(), valid.begin() + len);
        std::string err;
        auto res = decode(truncated, &err);
        CHECK(!res.has_value());
        CHECK(err.find("truncation") != std::string::npos);
    }
}

TEST(io_reject_checksum_mismatch) {
    World w(10, 10, 1);
    auto bytes = encode(w);

    // Corrupt a byte in the payload without updating checksum
    bytes[16] ^= 0x55;
    std::string err;
    auto res = decode(bytes, &err);
    CHECK(!res.has_value());
    CHECK(err.find("checksum mismatch") != std::string::npos);

    // Corrupt the checksum itself
    auto bytes2 = encode(w);
    bytes2.back() ^= 0x01;
    res = decode(bytes2, &err);
    CHECK(!res.has_value());
    CHECK(err.find("checksum mismatch") != std::string::npos);
}

TEST(io_rejects_tiny_file_claiming_huge_world) {
    std::vector<std::uint8_t> bytes;
    // Magic "GRN1"
    bytes.push_back('G'); bytes.push_back('R'); bytes.push_back('N'); bytes.push_back('1');
    // Version 1
    bytes.push_back(1); bytes.push_back(0);
    // Reserved 0
    bytes.push_back(0); bytes.push_back(0);
    // Width 4096 (0x1000)
    bytes.push_back(0x00); bytes.push_back(0x10); bytes.push_back(0x00); bytes.push_back(0x00);
    // Height 4096 (0x1000)
    bytes.push_back(0x00); bytes.push_back(0x10); bytes.push_back(0x00); bytes.push_back(0x00);
    // Seed 0
    for (int i = 0; i < 8; ++i) bytes.push_back(0);
    // Tick 0
    for (int i = 0; i < 8; ++i) bytes.push_back(0);
    // 0 runs (32 bytes header), append 4 bytes for checksum
    bytes.resize(36, 0);
    fix_checksum(bytes);

    std::string err;
    auto res = decode(bytes, &err);
    CHECK(!res.has_value());
    CHECK(!err.empty());
    CHECK(err.find("too few cells") != std::string::npos || err.find("too small") != std::string::npos);
}

TEST(io_fuzz_random_flips_and_truncation) {
    World w(16, 16, 42);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            w.set(x, y, static_cast<Material>((x + y) % kMaterialCount));
            w.at(x, y).temp = static_cast<float>(x * 15 - y * 10);
            w.at(x, y).life = static_cast<std::int16_t>(x + y * 2);
            w.at(x, y).aux = static_cast<std::int16_t>(y - x);
            w.at(x, y).flags = static_cast<std::uint8_t>(x & 0x3);
            w.at(x, y).shade = static_cast<std::uint8_t>((x * 37) & 0xff);
        }
    }
    const auto valid_bytes = encode(w);
    Rng rng(0x1337c0deull);

    for (int iter = 0; iter < 2000; ++iter) {
        auto corrupted = valid_bytes;
        const int mode = rng.range(0, 2);
        if (mode == 0 || mode == 2) {
            const std::size_t new_len = static_cast<std::size_t>(rng.range(0, static_cast<int>(corrupted.size())));
            corrupted.resize(new_len);
        }
        if (mode == 1 || mode == 2) {
            if (!corrupted.empty()) {
                const int num_flips = rng.range(1, 6);
                for (int f = 0; f < num_flips; ++f) {
                    const std::size_t idx = static_cast<std::size_t>(rng.range(0, static_cast<int>(corrupted.size()) - 1));
                    corrupted[idx] ^= static_cast<std::uint8_t>(rng.range(1, 255));
                }
            }
        }
        std::string err;
        auto res = decode(corrupted, &err);
        (void)res;
    }
}

TEST(io_save_and_load_world_file) {
    World w(40, 25, 9999);
    w.set_tick(123456);
    w.set(5, 5, Material::Lava);
    w.set(10, 10, Material::Water);
    w.at(5, 5).temp = 900.0f;
    w.at(5, 5).flags = kFlagBurning;
    w.at(10, 10).life = 50;

    const std::string tmp_path = "test_io_temp.grain";
    std::string err;
    bool ok = save_world(w, tmp_path, &err);
    CHECK(ok);
    CHECK(err.empty());

    auto loaded = load_world(tmp_path, &err);
    REQUIRE(loaded.has_value());
    CHECK_EQ(loaded->width(), 40);
    CHECK_EQ(loaded->height(), 25);
    CHECK_EQ(loaded->seed(), 9999ull);
    CHECK_EQ(loaded->tick(), 123456ull);
    CHECK(loaded->at(5, 5).mat == Material::Lava);
    CHECK_EQ(loaded->at(5, 5).temp, 900.0f);
    CHECK_EQ(loaded->at(5, 5).flags, kFlagBurning);
    CHECK(loaded->at(10, 10).mat == Material::Water);
    CHECK_EQ(loaded->at(10, 10).life, 50);

    std::remove(tmp_path.c_str());

    std::string non_existent = "non_existent_file_12345.grain";
    auto fail_load = load_world(non_existent, &err);
    CHECK(!fail_load.has_value());
    CHECK(!err.empty());
}

TEST(io_large_run_count_splitting) {
    // 300x300 = 90,000 cells > 65,535
    World w(300, 300, 777);
    const auto bytes = encode(w);

    // 90000 cells should split into 2 runs:
    // Header (32) + 2 runs (13 * 2 = 26) + Checksum (4) = 62 bytes
    CHECK_EQ(bytes.size(), 62u);

    std::string err;
    auto loaded = decode(bytes, &err);
    REQUIRE(loaded.has_value());
    CHECK_EQ(loaded->width(), 300);
    CHECK_EQ(loaded->height(), 300);
    CHECK_EQ(loaded->seed(), 777ull);
    for (int y = 0; y < 300; ++y) {
        for (int x = 0; x < 300; ++x) {
            CHECK(loaded->at(x, y).mat == Material::Empty);
        }
    }
}
