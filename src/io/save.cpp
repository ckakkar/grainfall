#include "io/save.hpp"

#include <bit>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gf::io {

namespace {

inline void write_u8(std::vector<std::uint8_t>& out, std::uint8_t val) {
    out.push_back(val);
}

inline void write_u16(std::vector<std::uint8_t>& out, std::uint16_t val) {
    out.push_back(static_cast<std::uint8_t>(val & 0xffu));
    out.push_back(static_cast<std::uint8_t>((val >> 8) & 0xffu));
}

inline void write_i16(std::vector<std::uint8_t>& out, std::int16_t val) {
    write_u16(out, static_cast<std::uint16_t>(val));
}

inline void write_u32(std::vector<std::uint8_t>& out, std::uint32_t val) {
    out.push_back(static_cast<std::uint8_t>(val & 0xffu));
    out.push_back(static_cast<std::uint8_t>((val >> 8) & 0xffu));
    out.push_back(static_cast<std::uint8_t>((val >> 16) & 0xffu));
    out.push_back(static_cast<std::uint8_t>((val >> 24) & 0xffu));
}

inline void write_u64(std::vector<std::uint8_t>& out, std::uint64_t val) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::uint8_t>((val >> (i * 8)) & 0xffu));
    }
}

inline void write_f32(std::vector<std::uint8_t>& out, float val) {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(val);
    write_u32(out, bits);
}

inline std::uint8_t read_u8(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return bytes[offset];
}

inline std::uint16_t read_u16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset]) |
           (static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
}

inline std::int16_t read_i16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return static_cast<std::int16_t>(read_u16(bytes, offset));
}

inline std::uint32_t read_u32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

inline std::uint64_t read_u64(std::span<const std::uint8_t> bytes, std::size_t offset) {
    std::uint64_t val = 0;
    for (int i = 0; i < 8; ++i) {
        val |= (static_cast<std::uint64_t>(bytes[offset + i]) << (i * 8));
    }
    return val;
}

inline float read_f32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    const std::uint32_t bits = read_u32(bytes, offset);
    return std::bit_cast<float>(bits);
}

inline bool cells_equal_for_save(const Cell& a, const Cell& b) {
    return a.mat == b.mat &&
           a.shade == b.shade &&
           a.flags == b.flags &&
           a.life == b.life &&
           a.aux == b.aux &&
           std::bit_cast<std::uint32_t>(a.temp) == std::bit_cast<std::uint32_t>(b.temp);
}

}  // namespace

std::uint32_t fnv1a(std::span<const std::uint8_t> bytes) {
    std::uint32_t hash = 0x811c9dc5u;
    for (std::uint8_t b : bytes) {
        hash ^= b;
        hash *= 0x01000193u;
    }
    return hash;
}

std::vector<std::uint8_t> encode(const World& world) {
    std::vector<std::uint8_t> out;
    out.reserve(32 + world.cells().size() * 13 + 4);

    // Header
    // 0..3: magic "GRN1"
    out.push_back('G');
    out.push_back('R');
    out.push_back('N');
    out.push_back('1');
    // 4..5: version = 1
    write_u16(out, 1);
    // 6..7: reserved = 0
    write_u16(out, 0);
    // 8..11: width
    write_u32(out, static_cast<std::uint32_t>(world.width()));
    // 12..15: height
    write_u32(out, static_cast<std::uint32_t>(world.height()));
    // 16..23: seed
    write_u64(out, world.seed());
    // 24..31: tick
    write_u64(out, world.tick());

    // Cell section
    const auto& cells = world.cells();
    const std::size_t n = cells.size();
    std::size_t i = 0;
    while (i < n) {
        std::size_t run_len = 1;
        while (i + run_len < n && run_len < 0xffff && cells_equal_for_save(cells[i], cells[i + run_len])) {
            ++run_len;
        }

        write_u16(out, static_cast<std::uint16_t>(run_len));
        const Cell& c = cells[i];
        write_u8(out, static_cast<std::uint8_t>(c.mat));
        write_u8(out, c.shade);
        write_u8(out, c.flags);
        write_i16(out, c.life);
        write_i16(out, c.aux);
        write_f32(out, c.temp);

        i += run_len;
    }

    // Checksum: FNV-1a 32-bit over every byte before it
    const std::uint32_t checksum = fnv1a(out);
    write_u32(out, checksum);

    return out;
}

std::optional<World> decode(std::span<const std::uint8_t> bytes, std::string* error) {
    auto fail = [&](std::string msg) -> std::optional<World> {
        if (error) *error = std::move(msg);
        return std::nullopt;
    };

    if (bytes.size() < 4) {
        return fail("truncation: file too small for magic");
    }

    if (bytes[0] != 'G' || bytes[1] != 'R' || bytes[2] != 'N' || bytes[3] != '1') {
        return fail("wrong magic: expected GRN1");
    }

    if (bytes.size() < 32) {
        return fail("truncation: file too small for header");
    }

    const std::uint16_t version = read_u16(bytes, 4);
    if (version != 1) {
        return fail("unknown version: expected 1");
    }

    const std::uint16_t reserved = read_u16(bytes, 6);
    if (reserved != 0) {
        return fail("nonzero reserved");
    }

    const std::uint32_t width = read_u32(bytes, 8);
    const std::uint32_t height = read_u32(bytes, 12);
    if (width < 1 || width > 4096 || height < 1 || height > 4096) {
        return fail("out-of-range dimensions");
    }

    const std::uint64_t seed = read_u64(bytes, 16);
    const std::uint64_t tick = read_u64(bytes, 24);

    if (bytes.size() < 36) {
        return fail("truncation: missing checksum");
    }

    const std::size_t checksum_offset = bytes.size() - 4;

    // (1) Verify checksum first
    const std::uint32_t expected_checksum = fnv1a(bytes.subspan(0, checksum_offset));
    const std::uint32_t file_checksum = read_u32(bytes, checksum_offset);
    if (file_checksum != expected_checksum) {
        return fail("checksum mismatch");
    }

    // (2) Reject files that are too small to possibly hold width*height cells
    const std::size_t total_expected = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const std::size_t min_runs = (total_expected + 65534) / 65535;
    const std::size_t min_cell_bytes = min_runs * 13;
    const std::size_t available_cell_bytes = checksum_offset - 32;
    if (available_cell_bytes < min_cell_bytes) {
        return fail("too few cells: file too small for dimensions");
    }

    // Safe to construct World after validating header, checksum, and minimum cell size
    World world(static_cast<int>(width), static_cast<int>(height), seed);
    world.set_tick(tick);
    auto& world_cells = world.cells();

    std::size_t offset = 32;
    std::size_t cells_read = 0;

    while (offset < checksum_offset && cells_read < total_expected) {
        if (checksum_offset - offset < 2) {
            return fail("truncation: incomplete run count");
        }
        const std::uint16_t count = read_u16(bytes, offset);
        offset += 2;
        if (count == 0) {
            return fail("invalid run count: zero");
        }
        if (cells_read + count > total_expected) {
            return fail("runs that overflow width*height");
        }

        if (checksum_offset - offset < 11) {
            return fail("truncation: incomplete cell record");
        }

        const std::uint8_t mat_byte = read_u8(bytes, offset);
        if (mat_byte >= static_cast<std::uint8_t>(Material::Count)) {
            return fail("a material byte >= Material::Count");
        }
        const std::uint8_t shade = read_u8(bytes, offset + 1);
        const std::uint8_t flags = read_u8(bytes, offset + 2);
        const std::int16_t life = read_i16(bytes, offset + 3);
        const std::int16_t aux = read_i16(bytes, offset + 5);
        const float temp = read_f32(bytes, offset + 7);
        offset += 11;

        Cell cell;
        cell.mat = static_cast<Material>(mat_byte);
        cell.shade = shade;
        cell.clock = 0;
        cell.flags = flags;
        cell.life = life;
        cell.aux = aux;
        cell.temp = temp;

        std::fill_n(world_cells.begin() + cells_read, count, cell);
        cells_read += count;
    }

    if (cells_read < total_expected) {
        return fail("too few cells");
    }

    if (offset != checksum_offset) {
        return fail("trailing bytes");
    }

    return world;
}

bool save_world(const World& world, const std::string& path, std::string* error) {
    const auto data = encode(world);
    std::ofstream os(path, std::ios::binary);
    if (!os.is_open()) {
        if (error) *error = "cannot open file for writing: " + path;
        return false;
    }
    os.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!os.good()) {
        if (error) *error = "failed to write data to file: " + path;
        return false;
    }
    return true;
}

std::optional<World> load_world(const std::string& path, std::string* error) {
    std::ifstream is(path, std::ios::binary | std::ios::ate);
    if (!is.is_open()) {
        if (error) *error = "cannot open file for reading: " + path;
        return std::nullopt;
    }
    const auto file_size = is.tellg();
    if (file_size < 0) {
        if (error) *error = "failed to determine file size: " + path;
        return std::nullopt;
    }
    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(file_size));
    is.seekg(0, std::ios::beg);
    if (file_size > 0) {
        is.read(reinterpret_cast<char*>(buffer.data()), file_size);
        if (!is.good() && !is.eof()) {
            if (error) *error = "failed to read file data: " + path;
            return std::nullopt;
        }
    }
    return decode(buffer, error);
}

}  // namespace gf::io