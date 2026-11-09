#pragma once
// The interactive SDL3 app: window, input, tools, HUD. Also exposes the
// headless modes used for testing (--bench, --shot).

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "app/renderer.hpp"
#include "core/world.hpp"
#include "sim/simulation.hpp"

struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Window;

namespace gf {

struct AppOptions {
    int world_w = 320;
    int world_h = 180;
    int scale = 4;              // window pixels per cell
    std::uint64_t seed = 24301;
    int scene = 0;              // index into scenes::all()
    std::string save_path = "grainfall.grain";
};

enum class Tool : std::uint8_t { Paint, Bomb, Heat, Count };

class App {
public:
    explicit App(AppOptions opts);
    ~App();

    // Interactive window. Returns the process exit code.
    int run();

    // Headless: build the scene, step `ticks` times, print timing + material counts.
    int bench(int ticks);

    // Headless: build the scene, step `ticks` times, render one frame (world + HUD)
    // with the software renderer and save it as a BMP.
    int screenshot(int ticks, ViewMode view, const std::string& path);

private:
    void load_scene(int index);
    void handle_key(int key, bool shift);
    void apply_tool(float wx, float wy, bool primary, bool shift);
    void stroke_to(float wx, float wy, bool primary, bool shift);
    void draw(SDL_Renderer* r, int out_w, int out_h, double fps);
    void draw_hud(SDL_Renderer* r, int out_w, int out_h, double fps);
    void draw_palette(SDL_Renderer* r, int out_w, int out_h);
    void draw_help(SDL_Renderer* r, int out_w, int out_h);
    bool screen_to_world(float sx, float sy, int& wx, int& wy) const;
    void flash(std::string message);

    AppOptions opts_;
    std::unique_ptr<World> world_;
    std::unique_ptr<Simulation> sim_;
    std::vector<std::uint32_t> pixels_;

    // UI state
    Material brush_mat_ = Material::Sand;
    Tool tool_ = Tool::Paint;
    ViewMode view_ = ViewMode::Normal;
    int brush_radius_ = 4;
    int ticks_per_frame_ = 1;
    bool paused_ = false;
    bool show_help_ = false;
    int scene_index_ = 0;
    bool stroking_ = false;
    bool stroke_primary_ = true;
    float last_wx_ = 0, last_wy_ = 0;
    int cursor_x_ = -1, cursor_y_ = -1;
    std::string flash_text_;
    std::uint64_t flash_until_ms_ = 0;
    std::uint64_t now_ms_ = 0;

    // Layout (window pixels), recomputed every frame.
    float view_x_ = 0, view_y_ = 0, view_scale_ = 1;
    int palette_y_ = 0;

    SDL_Texture* texture_ = nullptr;
    int tex_w_ = 0, tex_h_ = 0;
    void ensure_texture(SDL_Renderer* r);
    void upload_world();
};

}  // namespace gf
