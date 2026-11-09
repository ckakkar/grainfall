#pragma once
// Save / load of worlds. OWNER: intern (agy).
//
// File format ".grain" (all integers little-endian):
//   offset size  field
//   0      4     magic "GRN1"
//   4      2     version (uint16) = 1
//   6      2     reserved, must be 0
//   8      4     width  (uint32), 1..4096
//   12     4     height (uint32), 1..4096
//   16     8     seed   (uint64)
//   24     8     tick   (uint64)
//   32     ...   cell section: run-length encoded cells in row-major order.
//                Each run is: count (uint16, 1..65535) followed by one Cell record:
//                  mat (uint8) shade (uint8) flags (uint8) life (int16) aux (int16) temp (float32)
//                = 11 bytes of record. A run repeats that exact record `count` times.
//                Cell::clock is NOT saved (always loads as 0).
//                Two cells are "the same" for RLE only if every saved field is equal
//                (compare temp bit-for-bit).
//   end-4  4     checksum: FNV-1a 32-bit over every byte before it.
//
// decode() must reject, with a clear error message: wrong magic, unknown version,
// nonzero reserved, out-of-range dimensions, a material byte >= Material::Count,
// runs that overflow width*height, too few cells, trailing bytes, truncation
// anywhere, and checksum mismatch. It must never crash or read out of bounds on
// arbitrary input.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/world.hpp"

namespace gf::io {

std::vector<std::uint8_t> encode(const World& world);
std::optional<World> decode(std::span<const std::uint8_t> bytes, std::string* error = nullptr);

bool save_world(const World& world, const std::string& path, std::string* error = nullptr);
std::optional<World> load_world(const std::string& path, std::string* error = nullptr);

// FNV-1a 32-bit, exposed for tests.
std::uint32_t fnv1a(std::span<const std::uint8_t> bytes);

}  // namespace gf::io
