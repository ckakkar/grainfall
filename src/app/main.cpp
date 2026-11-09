// Grainfall entry point.
//
//   grainfall                         open the sandbox window
//   grainfall --scene 2               start on a specific scene
//   grainfall --bench 2000            headless: step 2000 ticks, print timing
//   grainfall --shot out.bmp --ticks 300 [--view heat]
//                                     headless: render one frame to a BMP

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "app/app.hpp"

namespace {

void usage() {
    std::puts(
        "usage: grainfall [options]\n"
        "  --scene N          start on scene N (0-based)\n"
        "  --seed S           world seed (default 24301)\n"
        "  --size WxH         world size in cells (default 320x180)\n"
        "  --scale K          window pixels per cell (default 4)\n"
        "  --save PATH        save/load file for F5/F9 (default grainfall.grain)\n"
        "  --bench N          headless: run N ticks and print timing\n"
        "  --shot PATH        headless: render a frame to a BMP file\n"
        "  --ticks N          ticks to simulate before --shot (default 240)\n"
        "  --view MODE        normal | heat | chunks (for --shot)\n"
        "  --help             this text");
}

}  // namespace

int main(int argc, char** argv) {
    gf::AppOptions opts;
    int bench_ticks = -1;
    int shot_ticks = 240;
    std::string shot_path;
    gf::ViewMode view = gf::ViewMode::Normal;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", arg.c_str());
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--help" || arg == "-h") {
            usage();
            return 0;
        } else if (arg == "--scene") {
            opts.scene = std::atoi(next());
        } else if (arg == "--seed") {
            opts.seed = std::strtoull(next(), nullptr, 10);
        } else if (arg == "--size") {
            int w = 0, h = 0;
            if (std::sscanf(next(), "%dx%d", &w, &h) != 2 || w < 16 || h < 16 || w > 4096 || h > 4096) {
                std::fprintf(stderr, "--size must look like 320x180 (16..4096)\n");
                return 2;
            }
            opts.world_w = w;
            opts.world_h = h;
        } else if (arg == "--scale") {
            opts.scale = std::max(1, std::atoi(next()));
        } else if (arg == "--save") {
            opts.save_path = next();
        } else if (arg == "--bench") {
            bench_ticks = std::max(1, std::atoi(next()));
        } else if (arg == "--shot") {
            shot_path = next();
        } else if (arg == "--ticks") {
            shot_ticks = std::max(0, std::atoi(next()));
        } else if (arg == "--view") {
            const std::string v = next();
            if (v == "normal") view = gf::ViewMode::Normal;
            else if (v == "heat") view = gf::ViewMode::Heat;
            else if (v == "chunks") view = gf::ViewMode::Chunks;
            else {
                std::fprintf(stderr, "unknown view: %s\n", v.c_str());
                return 2;
            }
        } else {
            std::fprintf(stderr, "unknown option: %s\n\n", arg.c_str());
            usage();
            return 2;
        }
    }

    gf::App app(opts);
    if (bench_ticks > 0) return app.bench(bench_ticks);
    if (!shot_path.empty()) return app.screenshot(shot_ticks, view, shot_path);
    return app.run();
}
