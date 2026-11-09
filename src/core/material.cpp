#include "core/material.hpp"

namespace gf {

namespace {

// clang-format off
constexpr std::array<MaterialInfo, kMaterialCount> kTable{{
    //  name        phase          dens   cond  spawnC   color     jit flam  igniteC  life      disp
    {"Empty",     Phase::Empty,    0.0f, 0.02f,   20.f, 0x0b0d12,   0, 0.00f, kNever,    0,   0, 0},
    {"Stone",     Phase::Static,   9.0f, 0.15f,   20.f, 0x6b6e75,  18, 0.00f, kNever,    0,   0, 0},
    {"Sand",      Phase::Powder,   5.0f, 0.20f,   20.f, 0xd9bf7a,  22, 0.00f, kNever,    0,   0, 0},
    {"Water",     Phase::Liquid,   3.0f, 0.45f,   20.f, 0x2f6fdc,  10, 0.00f, kNever,    0,   0, 5},
    {"Oil",       Phase::Liquid,   2.0f, 0.15f,   20.f, 0x5b3a1e,  12, 0.65f,  220.f,    0,   0, 3},
    {"Lava",      Phase::Liquid,   6.0f, 0.35f, 1400.f, 0xff5a1a,  30, 0.00f, kNever,    0,   0, 1},
    {"Ice",       Phase::Static,   2.9f, 0.55f,  -30.f, 0xa8dcf5,  12, 0.00f, kNever,    0,   0, 0},
    {"Steam",     Phase::Gas,      0.3f, 0.10f,  130.f, 0xc9d3dd,  18, 0.00f, kNever,  300, 900, 4},
    {"Smoke",     Phase::Gas,      0.4f, 0.05f,  120.f, 0x3c3c40,  14, 0.00f, kNever,  120, 360, 2},
    {"Fire",      Phase::Gas,      0.2f, 0.60f,  900.f, 0xff9a2e,  50, 0.00f, kNever,   20,  60, 1},
    {"Wood",      Phase::Static,   7.0f, 0.08f,   20.f, 0x7a4a24,  14, 0.06f,  300.f,    0,   0, 0},
    {"Plant",     Phase::Static,   6.5f, 0.10f,   20.f, 0x39a845,  20, 0.18f,  180.f,    0,   0, 0},
    {"Acid",      Phase::Liquid,   3.5f, 0.30f,   20.f, 0x9be82c,  16, 0.00f, kNever,    0,   0, 3},
    {"Gunpowder", Phase::Powder,   4.5f, 0.12f,   20.f, 0x2a2a2a,  16, 1.00f,  160.f,    0,   0, 0},
    {"Metal",     Phase::Static,  10.0f, 0.95f,   20.f, 0x8c96a6,   8, 0.00f, kNever,    0,   0, 0},
}};
// clang-format on

}  // namespace

const std::array<MaterialInfo, kMaterialCount>& material_table() { return kTable; }

const MaterialInfo& info(Material m) { return kTable[static_cast<std::size_t>(m)]; }

}  // namespace gf
