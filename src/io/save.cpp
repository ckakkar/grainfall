// STUB written by the lead so the project builds. The intern (agy) replaces this file.
#include "io/save.hpp"

namespace gf::io {

std::vector<std::uint8_t> encode(const World&) { return {}; }

std::optional<World> decode(std::span<const std::uint8_t>, std::string* error) {
    if (error) *error = "not implemented";
    return std::nullopt;
}

bool save_world(const World&, const std::string&, std::string* error) {
    if (error) *error = "not implemented";
    return false;
}

std::optional<World> load_world(const std::string&, std::string* error) {
    if (error) *error = "not implemented";
    return std::nullopt;
}

std::uint32_t fnv1a(std::span<const std::uint8_t>) { return 0; }

}  // namespace gf::io
