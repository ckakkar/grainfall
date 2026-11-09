#include "app/app.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <utility>

#include "io/save.hpp"
#include "scenes/scenes.hpp"

namespace gf {

namespace {

constexpr int kPaletteHeight = 44;
constexpr float kText = 8.0f;  // SDL debug font cell size in pixels

struct PaletteEntry {
    Material mat;
    SDL_Keycode key;   // 0 = no hotkey
    const char* label; // hotkey label shown on the swatch
};

constexpr std::array<PaletteEntry, 14> kPalette{{
    {Material::Sand, SDLK_1, "1"},
    {Material::Water, SDLK_2, "2"},
    {Material::Stone, SDLK_3, "3"},
    {Material::Wood, SDLK_4, "4"},
    {Material::Oil, SDLK_5, "5"},
    {Material::Fire, SDLK_6, "6"},
    {Material::Lava, SDLK_7, "7"},
    {Material::Ice, SDLK_8, "8"},
    {Material::Acid, SDLK_9, "9"},
    {Material::Gunpowder, SDLK_0, "0"},
    {Material::Metal, SDLK_M, "M"},
    {Material::Plant, SDLK_G, "G"},
    {Material::Steam, 0, ""},
    {Material::Smoke, 0, ""},
}};

const char* tool_name(Tool t) {
    switch (t) {
        case Tool::Paint: return "Paint";
        case Tool::Bomb: return "Bomb";
        case Tool::Heat: return "Heat gun";
        default: return "?";
    }
}

std::uint64_t now_ms() { return SDL_GetTicks(); }

void set_color(SDL_Renderer* r, std::uint32_t argb, std::uint8_t alpha = 255) {
    SDL_SetRenderDrawColor(r, static_cast<Uint8>((argb >> 16) & 0xff), static_cast<Uint8>((argb >> 8) & 0xff),
                           static_cast<Uint8>(argb & 0xff), alpha);
}

void fill(SDL_Renderer* r, float x, float y, float w, float h, std::uint32_t rgb, std::uint8_t alpha = 255) {
    set_color(r, rgb, alpha);
    const SDL_FRect rect{x, y, w, h};
    SDL_RenderFillRect(r, &rect);
}

void outline(SDL_Renderer* r, float x, float y, float w, float h, std::uint32_t rgb) {
    set_color(r, rgb);
    const SDL_FRect rect{x, y, w, h};
    SDL_RenderRect(r, &rect);
}

// Draw debug text at window pixel (x, y) with an integer scale factor.
void text(SDL_Renderer* r, float x, float y, const char* str, float scale = 2.0f, std::uint32_t rgb = 0xe8ecf4,
          bool shadow = true) {
    SDL_SetRenderScale(r, scale, scale);
    if (shadow) {
        // Drop shadow keeps text readable over bright materials.
        SDL_SetRenderDrawColor(r, 0, 0, 0, 200);
        SDL_RenderDebugText(r, x / scale + 1.0f, y / scale + 1.0f, str);
    }
    set_color(r, rgb);
    SDL_RenderDebugText(r, x / scale, y / scale, str);
    SDL_SetRenderScale(r, 1.0f, 1.0f);
}

float text_width(const char* str, float scale = 2.0f) {
    return static_cast<float>(std::char_traits<char>::length(str)) * kText * scale;
}

std::uint64_t world_hash(const World& w) {
    std::uint64_t h = 1469598103934665603ull;
    for (const Cell& c : w.cells()) {
        h ^= static_cast<std::uint64_t>(c.mat);
        h *= 1099511628211ull;
    }
    return h;
}

}  // namespace

App::App(AppOptions opts) : opts_(std::move(opts)) {
    world_ = std::make_unique<World>(opts_.world_w, opts_.world_h, opts_.seed);
    sim_ = std::make_unique<Simulation>(*world_);
    load_scene(opts_.scene);
}

App::~App() {
    if (texture_) SDL_DestroyTexture(texture_);
}

void App::load_scene(int index) {
    const auto scenes = scenes::all();
    if (scenes.empty()) return;
    const int n = static_cast<int>(scenes.size());
    scene_index_ = ((index % n) + n) % n;
    // Fresh world with the same seed so every scene load is reproducible.
    sim_.reset();
    world_ = std::make_unique<World>(opts_.world_w, opts_.world_h, opts_.seed + static_cast<std::uint64_t>(scene_index_));
    scenes[static_cast<std::size_t>(scene_index_)].build(*world_);
    sim_ = std::make_unique<Simulation>(*world_);
    sim_->wake_all();
    flash(std::string("Scene: ") + std::string(scenes[static_cast<std::size_t>(scene_index_)].name));
}

void App::flash(std::string message) {
    flash_text_ = std::move(message);
    flash_until_ms_ = now_ms_ + 2200;
}

bool App::screen_to_world(float sx, float sy, int& wx, int& wy) const {
    wx = static_cast<int>(std::floor((sx - view_x_) / view_scale_));
    wy = static_cast<int>(std::floor((sy - view_y_) / view_scale_));
    return world_->in_bounds(wx, wy);
}

void App::apply_tool(float fx, float fy, bool primary, bool shift) {
    const int x = static_cast<int>(std::floor(fx));
    const int y = static_cast<int>(std::floor(fy));
    const int r = brush_radius_;

    switch (tool_) {
        case Tool::Paint: {
            const Material m = primary ? brush_mat_ : Material::Empty;
            if (m == Material::Fire) {
                // Fire fills empty space and sets flammable things alight.
                const int r2 = r * r;
                for (int dy = -r; dy <= r; ++dy) {
                    for (int dx = -r; dx <= r; ++dx) {
                        if (dx * dx + dy * dy > r2) continue;
                        Cell* c = world_->try_at(x + dx, y + dy);
                        if (!c) continue;
                        if (c->mat == Material::Empty) {
                            *c = world_->make_cell(Material::Fire);
                        } else if (info(c->mat).flammability > 0.0f) {
                            c->temp = std::max(c->temp, info(c->mat).ignite_temp + 80.0f);
                        }
                    }
                }
            } else {
                world_->paint_circle(x, y, r, m, shift && primary);
            }
            break;
        }
        case Tool::Heat: {
            const int r2 = r * r;
            for (int dy = -r; dy <= r; ++dy) {
                for (int dx = -r; dx <= r; ++dx) {
                    if (dx * dx + dy * dy > r2) continue;
                    Cell* c = world_->try_at(x + dx, y + dy);
                    if (!c) continue;
                    c->temp = primary ? std::min(c->temp + 45.0f, 3000.0f) : std::max(c->temp - 30.0f, -120.0f);
                }
            }
            break;
        }
        case Tool::Bomb:
            break;  // bombs trigger on click, not on drag (see run())
        default:
            break;
    }
    sim_->wake_rect(x - r - 2, y - r - 2, x + r + 2, y + r + 2);
}

void App::stroke_to(float wx, float wy, bool primary, bool shift) {
    // Interpolate so fast mouse moves leave a continuous stroke.
    const float dx = wx - last_wx_;
    const float dy = wy - last_wy_;
    const float dist = std::sqrt(dx * dx + dy * dy);
    const float step = std::max(1.0f, static_cast<float>(brush_radius_) * 0.5f);
    const int steps = std::max(1, static_cast<int>(dist / step));
    for (int i = 1; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        apply_tool(last_wx_ + dx * t, last_wy_ + dy * t, primary, shift);
    }
    last_wx_ = wx;
    last_wy_ = wy;
}

void App::handle_key(int key, bool shift) {
    for (const auto& p : kPalette) {
        if (p.key != 0 && static_cast<SDL_Keycode>(key) == p.key) {
            brush_mat_ = p.mat;
            tool_ = Tool::Paint;
            return;
        }
    }
    switch (key) {
        case SDLK_SPACE: paused_ = !paused_; flash(paused_ ? "Paused" : "Running"); break;
        case SDLK_PERIOD:
            if (paused_) sim_->step();
            break;
        case SDLK_TAB: {
            int idx = 0;
            for (int i = 0; i < static_cast<int>(kPalette.size()); ++i)
                if (kPalette[static_cast<std::size_t>(i)].mat == brush_mat_) idx = i;
            const int n = static_cast<int>(kPalette.size());
            idx = (idx + (shift ? n - 1 : 1)) % n;
            brush_mat_ = kPalette[static_cast<std::size_t>(idx)].mat;
            tool_ = Tool::Paint;
            break;
        }
        case SDLK_P: tool_ = Tool::Paint; break;
        case SDLK_B: tool_ = Tool::Bomb; flash("Bomb: click to detonate"); break;
        case SDLK_H: tool_ = Tool::Heat; flash("Heat gun: left heats, right chills"); break;
        case SDLK_LEFTBRACKET: brush_radius_ = std::max(0, brush_radius_ - 1); break;
        case SDLK_RIGHTBRACKET: brush_radius_ = std::min(40, brush_radius_ + 1); break;
        case SDLK_UP: ticks_per_frame_ = std::min(8, ticks_per_frame_ + 1); break;
        case SDLK_DOWN: ticks_per_frame_ = std::max(1, ticks_per_frame_ - 1); break;
        case SDLK_V: {
            const int n = static_cast<int>(ViewMode::Count);
            view_ = static_cast<ViewMode>((static_cast<int>(view_) + 1) % n);
            flash(std::string("View: ") + view_name(view_));
            break;
        }
        case SDLK_C:
            world_->clear();
            sim_->wake_all();
            flash("Cleared");
            break;
        case SDLK_R: load_scene(scene_index_); break;
        case SDLK_RIGHT: load_scene(scene_index_ + 1); break;
        case SDLK_LEFT: load_scene(scene_index_ - 1); break;
        case SDLK_F1:
        case SDLK_SLASH: show_help_ = !show_help_; break;
        case SDLK_F5: {
            std::string err;
            if (io::save_world(*world_, opts_.save_path, &err)) {
                flash("Saved " + opts_.save_path);
            } else {
                flash("Save failed: " + err);
            }
            break;
        }
        case SDLK_F9: {
            std::string err;
            auto loaded = io::load_world(opts_.save_path, &err);
            if (loaded) {
                auto fresh = std::make_unique<World>(std::move(*loaded));
                sim_.reset();
                world_ = std::move(fresh);
                sim_ = std::make_unique<Simulation>(*world_);
                sim_->wake_all();
                flash("Loaded " + opts_.save_path);
            } else {
                flash("Load failed: " + err);
            }
            break;
        }
        default: break;
    }
}

void App::ensure_texture(SDL_Renderer* r) {
    if (texture_ && tex_w_ == world_->width() && tex_h_ == world_->height()) return;
    if (texture_) SDL_DestroyTexture(texture_);
    texture_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, world_->width(),
                                 world_->height());
    if (!texture_) {
        std::fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        return;
    }
    SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_NEAREST);
    tex_w_ = world_->width();
    tex_h_ = world_->height();
}

void App::upload_world() {
    shade_world(*world_, sim_.get(), view_, pixels_);
    if (texture_) {
        SDL_UpdateTexture(texture_, nullptr, pixels_.data(), world_->width() * static_cast<int>(sizeof(std::uint32_t)));
    }
}

void App::draw(SDL_Renderer* r, int out_w, int out_h, double fps) {
    // Layout: world fills the area above the palette, aspect-correct, centred.
    palette_y_ = out_h - kPaletteHeight;
    const float area_w = static_cast<float>(out_w);
    const float area_h = static_cast<float>(std::max(palette_y_, 1));
    view_scale_ = std::min(area_w / static_cast<float>(world_->width()), area_h / static_cast<float>(world_->height()));
    const float vw = view_scale_ * static_cast<float>(world_->width());
    const float vh = view_scale_ * static_cast<float>(world_->height());
    view_x_ = (area_w - vw) * 0.5f;
    view_y_ = (area_h - vh) * 0.5f;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 6, 7, 10, 255);
    SDL_RenderClear(r);

    if (texture_) {
        const SDL_FRect dst{view_x_, view_y_, vw, vh};
        SDL_RenderTexture(r, texture_, nullptr, &dst);
    }

    // Brush cursor.
    if (world_->in_bounds(cursor_x_, cursor_y_)) {
        const float rad = (static_cast<float>(brush_radius_) + 0.5f) * view_scale_;
        const float cx = view_x_ + (static_cast<float>(cursor_x_) + 0.5f) * view_scale_;
        const float cy = view_y_ + (static_cast<float>(cursor_y_) + 0.5f) * view_scale_;
        const std::uint32_t col = tool_ == Tool::Bomb ? 0xff4040 : tool_ == Tool::Heat ? 0xffa030 : 0xffffff;
        set_color(r, col, 170);
        constexpr int kSegments = 40;
        std::array<SDL_FPoint, kSegments + 1> pts{};
        for (int i = 0; i <= kSegments; ++i) {
            const float a = static_cast<float>(i) / kSegments * 6.2831853f;
            pts[static_cast<std::size_t>(i)] = {cx + std::cos(a) * rad, cy + std::sin(a) * rad};
        }
        SDL_RenderLines(r, pts.data(), static_cast<int>(pts.size()));
    }

    draw_palette(r, out_w, out_h);
    draw_hud(r, out_w, out_h, fps);
    if (show_help_) draw_help(r, out_w, out_h);
}

void App::draw_hud(SDL_Renderer* r, int out_w, int /*out_h*/, double fps) {
    const auto& st = sim_->stats();
    const auto scenes = scenes::all();
    const std::string_view scene_name =
        scenes.empty() ? std::string_view{"-"} : scenes[static_cast<std::size_t>(scene_index_)].name;

    char line1[256];
    std::snprintf(line1, sizeof line1, "GRAINFALL  %.*s (%d/%d)  tick %llu  %3.0f fps  sim %.2f ms  awake %d/%d",
                  static_cast<int>(scene_name.size()), scene_name.data(), scene_index_ + 1,
                  static_cast<int>(scenes.size()), static_cast<unsigned long long>(world_->tick()), fps, st.step_ms,
                  st.active_chunks, st.total_chunks);

    char line2[256];
    if (tool_ == Tool::Paint) {
        std::snprintf(line2, sizeof line2, "%s %.*s  r=%d  x%d  view:%s%s  (F1 help)", tool_name(tool_),
                      static_cast<int>(name_of(brush_mat_).size()), name_of(brush_mat_).data(), brush_radius_,
                      ticks_per_frame_, view_name(view_), paused_ ? "  PAUSED" : "");
    } else {
        std::snprintf(line2, sizeof line2, "%s  r=%d  x%d  view:%s%s  (F1 help)", tool_name(tool_), brush_radius_,
                      ticks_per_frame_, view_name(view_), paused_ ? "  PAUSED" : "");
    }

    // Text shrinks to 1x on narrow windows so the two HUD lines never collide.
    const float ts = out_w >= 1100 ? 2.0f : 1.0f;
    const float lh = kText * ts + 4.0f;
    fill(r, 0, 0, static_cast<float>(out_w), lh * 2.0f + 8.0f, 0x000000, 140);
    text(r, 10, 4, line1, ts);
    text(r, 10, 4 + lh, line2, ts, paused_ ? 0xffd166 : 0xa9b4c8);

    // Right side of line 2: the cell under the cursor, or the help hint.
    char probe[128] = "F1 help";
    if (const Cell* c = world_->try_at(cursor_x_, cursor_y_)) {
        std::snprintf(probe, sizeof probe, "(%d,%d) %.*s %.0fC", cursor_x_, cursor_y_,
                      static_cast<int>(name_of(c->mat).size()), name_of(c->mat).data(), static_cast<double>(c->temp));
    }
    const float px = static_cast<float>(out_w) - text_width(probe, ts) - 10;
    if (px > 10 + text_width(line2, ts) + 16) text(r, px, 4 + lh, probe, ts, 0x8090a8);

    // Transient message.
    if (!flash_text_.empty() && now_ms_ < flash_until_ms_) {
        const float fs = out_w >= 1100 ? 3.0f : 2.0f;
        const float fw = text_width(flash_text_.c_str(), fs);
        const float fx = (static_cast<float>(out_w) - fw) * 0.5f;
        fill(r, fx - 14, 60, fw + 28, kText * fs + 16, 0x000000, 170);
        text(r, fx, 68, flash_text_.c_str(), fs, 0xffffff);
    }
}

void App::draw_palette(SDL_Renderer* r, int out_w, int /*out_h*/) {
    const float y = static_cast<float>(palette_y_);
    fill(r, 0, y, static_cast<float>(out_w), kPaletteHeight, 0x0e1118);
    const float sw = static_cast<float>(out_w) / static_cast<float>(kPalette.size());
    for (std::size_t i = 0; i < kPalette.size(); ++i) {
        const auto& p = kPalette[i];
        const float x = static_cast<float>(i) * sw;
        const bool selected = tool_ == Tool::Paint && p.mat == brush_mat_;
        fill(r, x + 3, y + 4, sw - 6, kPaletteHeight - 8, swatch_color(p.mat) & 0xffffff);
        if (selected) {
            outline(r, x + 1, y + 2, sw - 2, kPaletteHeight - 4, 0xffffff);
            outline(r, x + 2, y + 3, sw - 4, kPaletteHeight - 6, 0xffffff);
        }
        const std::string_view name = name_of(p.mat);
        char label[32];
        std::snprintf(label, sizeof label, "%s%s%.*s", p.label, p.label[0] ? " " : "", static_cast<int>(name.size()),
                      name.data());
        // One label size for the whole bar: 2x only if every label fits at 2x.
        const float scale = sw >= 12 * kText * 2.0f ? 2.0f : 1.0f;
        // Truncate labels that still don't fit (very narrow windows).
        const auto max_chars = static_cast<std::size_t>(std::max(1.0f, (sw - 6.0f) / (kText * scale)));
        if (std::char_traits<char>::length(label) > max_chars) label[max_chars] = '\0';
        const float tw = text_width(label, scale);
        // Dark text on light swatches, light text on dark ones.
        const std::uint32_t c = swatch_color(p.mat);
        const float luma = 0.299f * static_cast<float>((c >> 16) & 0xff) + 0.587f * static_cast<float>((c >> 8) & 0xff) +
                           0.114f * static_cast<float>(c & 0xff);
        const bool light = luma > 140.0f;
        text(r, x + (sw - tw) * 0.5f, y + (kPaletteHeight - kText * scale) * 0.5f, label, scale,
             light ? 0x101218 : 0xffffff, /*shadow=*/!light);
    }
}

void App::draw_help(SDL_Renderer* r, int out_w, int out_h) {
    static constexpr std::array<const char*, 17> kLines{{
        "GRAINFALL - controls",
        "",
        "left drag     paint selected material",
        "right drag    erase (heat gun: chill)",
        "shift+drag    paint only into empty space",
        "1-0 M G Tab   pick material (or click palette)",
        "P / B / H     paint / bomb / heat gun",
        "[ ]  wheel    brush size",
        "space  .      pause / single step",
        "up / down     simulation speed",
        "V             view: normal / heat / chunks",
        "left / right  previous / next scene,  R reset",
        "C             clear world",
        "F5 / F9       save / load grainfall.grain",
        "F1 or /       toggle this help",
        "",
        "esc           quit",
    }};
    const float scale = 2.0f;
    const float lh = kText * scale + 8;
    const float w = 560;
    const float h = lh * static_cast<float>(kLines.size()) + 40;
    const float x = (static_cast<float>(out_w) - w) * 0.5f;
    const float y = (static_cast<float>(out_h) - h) * 0.5f;
    fill(r, x, y, w, h, 0x0b0d14, 235);
    outline(r, x, y, w, h, 0x3a4256);
    for (std::size_t i = 0; i < kLines.size(); ++i) {
        text(r, x + 24, y + 20 + static_cast<float>(i) * lh, kLines[i], scale, i == 0 ? 0xffd166 : 0xd8deea);
    }
}

int App::run() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    const int win_w = opts_.world_w * opts_.scale;
    const int win_h = opts_.world_h * opts_.scale + kPaletteHeight;
    if (!SDL_CreateWindowAndRenderer("Grainfall", win_w, win_h, SDL_WINDOW_RESIZABLE, &window, &renderer)) {
        std::fprintf(stderr, "SDL_CreateWindowAndRenderer failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);

    bool running = true;
    double fps = 60.0;
    auto last = std::chrono::steady_clock::now();
    bool left_down = false;
    bool right_down = false;

    while (running) {
        now_ms_ = now_ms();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_EVENT_QUIT: running = false; break;
                case SDL_EVENT_KEY_DOWN:
                    if (e.key.key == SDLK_ESCAPE) {
                        if (show_help_) show_help_ = false;
                        else running = false;
                    } else if (!e.key.repeat || e.key.key == SDLK_PERIOD || e.key.key == SDLK_LEFTBRACKET ||
                               e.key.key == SDLK_RIGHTBRACKET) {
                        handle_key(static_cast<int>(e.key.key), (e.key.mod & SDL_KMOD_SHIFT) != 0);
                    }
                    break;
                case SDL_EVENT_MOUSE_WHEEL:
                    brush_radius_ = std::clamp(brush_radius_ + (e.wheel.y > 0 ? 1 : e.wheel.y < 0 ? -1 : 0), 0, 40);
                    break;
                case SDL_EVENT_MOUSE_MOTION: {
                    int wx, wy;
                    screen_to_world(e.motion.x, e.motion.y, wx, wy);
                    cursor_x_ = wx;
                    cursor_y_ = wy;
                    break;
                }
                case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                    // Palette click?
                    if (e.button.y >= static_cast<float>(palette_y_)) {
                        int ow = 0, oh = 0;
                        SDL_GetRenderOutputSize(renderer, &ow, &oh);
                        const float sw = static_cast<float>(ow) / static_cast<float>(kPalette.size());
                        const auto idx = static_cast<std::size_t>(std::clamp(e.button.x / sw, 0.0f, 13.0f));
                        brush_mat_ = kPalette[idx].mat;
                        tool_ = Tool::Paint;
                        break;
                    }
                    const bool primary = e.button.button == SDL_BUTTON_LEFT;
                    const bool secondary = e.button.button == SDL_BUTTON_RIGHT;
                    if (!primary && !secondary) break;
                    const float fx = (e.button.x - view_x_) / view_scale_;
                    const float fy = (e.button.y - view_y_) / view_scale_;
                    if (tool_ == Tool::Bomb) {
                        if (primary) {
                            const int bx = static_cast<int>(fx);
                            const int by = static_cast<int>(fy);
                            sim_->explode(bx, by, brush_radius_ * 2 + 6);
                            const int br = brush_radius_ * 2 + 12;
                            sim_->wake_rect(bx - br, by - br, bx + br, by + br);
                        }
                        break;
                    }
                    if (primary) left_down = true;
                    if (secondary) right_down = true;
                    stroking_ = true;
                    stroke_primary_ = primary;
                    last_wx_ = fx;
                    last_wy_ = fy;
                    apply_tool(fx, fy, primary, (SDL_GetModState() & SDL_KMOD_SHIFT) != 0);
                    break;
                }
                case SDL_EVENT_MOUSE_BUTTON_UP:
                    if (e.button.button == SDL_BUTTON_LEFT) left_down = false;
                    if (e.button.button == SDL_BUTTON_RIGHT) right_down = false;
                    if (!left_down && !right_down) stroking_ = false;
                    break;
                default: break;
            }
        }

        // Keep pouring while a button is held, even if the mouse is still.
        if (stroking_) {
            float mx = 0, my = 0;
            SDL_GetMouseState(&mx, &my);
            const float fx = (mx - view_x_) / view_scale_;
            const float fy = (my - view_y_) / view_scale_;
            stroke_to(fx, fy, stroke_primary_, (SDL_GetModState() & SDL_KMOD_SHIFT) != 0);
        }

        if (!paused_) {
            for (int i = 0; i < ticks_per_frame_; ++i) sim_->step();
        }

        ensure_texture(renderer);
        upload_world();
        int ow = 0, oh = 0;
        SDL_GetRenderOutputSize(renderer, &ow, &oh);
        draw(renderer, ow, oh, fps);
        SDL_RenderPresent(renderer);

        const auto now = std::chrono::steady_clock::now();
        const double dt = std::chrono::duration<double>(now - last).count();
        last = now;
        if (dt > 0) fps = fps * 0.9 + (1.0 / dt) * 0.1;
    }

    if (texture_) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

int App::bench(int ticks) {
    const auto scenes = scenes::all();
    std::printf("bench: scene \"%.*s\", world %dx%d, seed %llu, %d ticks\n",
                static_cast<int>(scenes[static_cast<std::size_t>(scene_index_)].name.size()),
                scenes[static_cast<std::size_t>(scene_index_)].name.data(), world_->width(), world_->height(),
                static_cast<unsigned long long>(opts_.seed), ticks);

    std::vector<double> times;
    times.reserve(static_cast<std::size_t>(ticks));
    long long awake_sum = 0;
    for (int i = 0; i < ticks; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        sim_->step();
        const auto t1 = std::chrono::steady_clock::now();
        times.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        awake_sum += sim_->stats().active_chunks;
    }
    if (times.empty()) return 0;
    std::vector<double> sorted = times;
    std::sort(sorted.begin(), sorted.end());
    const double avg = std::accumulate(times.begin(), times.end(), 0.0) / static_cast<double>(times.size());
    const auto pct = [&](double p) { return sorted[static_cast<std::size_t>(p * static_cast<double>(sorted.size() - 1))]; };
    std::printf("step ms: avg %.3f  p50 %.3f  p95 %.3f  max %.3f\n", avg, pct(0.5), pct(0.95), sorted.back());
    std::printf("awake chunks: avg %.1f of %d\n", static_cast<double>(awake_sum) / ticks, sim_->stats().total_chunks);
    std::printf("materials after %d ticks:\n", ticks);
    for (int m = 1; m < kMaterialCount; ++m) {
        const int n = world_->count(static_cast<Material>(m));
        if (n > 0) {
            const auto name = name_of(static_cast<Material>(m));
            std::printf("  %-10.*s %6d\n", static_cast<int>(name.size()), name.data(), n);
        }
    }
    float tmin = 1e9f, tmax = -1e9f;
    bool nan = false;
    for (const Cell& c : world_->cells()) {
        if (std::isnan(c.temp)) nan = true;
        tmin = std::min(tmin, c.temp);
        tmax = std::max(tmax, c.temp);
    }
    std::printf("temp range: %.1f .. %.1f C%s\n", static_cast<double>(tmin), static_cast<double>(tmax),
                nan ? "  (NaN FOUND!)" : "");
    std::printf("world hash: %016llx\n", static_cast<unsigned long long>(world_hash(*world_)));
    return nan ? 2 : 0;
}

int App::screenshot(int ticks, ViewMode view, const std::string& path) {
    for (int i = 0; i < ticks; ++i) sim_->step();
    view_ = view;
    const int out_w = opts_.world_w * opts_.scale;
    const int out_h = opts_.world_h * opts_.scale + kPaletteHeight;
    SDL_Surface* surface = SDL_CreateSurface(out_w, out_h, SDL_PIXELFORMAT_ARGB8888);
    if (!surface) {
        std::fprintf(stderr, "SDL_CreateSurface failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Renderer* r = SDL_CreateSoftwareRenderer(surface);
    if (!r) {
        std::fprintf(stderr, "SDL_CreateSoftwareRenderer failed: %s\n", SDL_GetError());
        SDL_DestroySurface(surface);
        return 1;
    }
    // Pretend the cursor hovers mid-world so the probe readout and brush show up.
    cursor_x_ = world_->width() / 2;
    cursor_y_ = world_->height() / 2;
    ensure_texture(r);
    upload_world();
    draw(r, out_w, out_h, 60.0);
    SDL_RenderPresent(r);
    const bool ok = SDL_SaveBMP(surface, path.c_str());
    if (!ok) std::fprintf(stderr, "SDL_SaveBMP failed: %s\n", SDL_GetError());
    if (texture_) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
    }
    SDL_DestroyRenderer(r);
    SDL_DestroySurface(surface);
    if (ok) std::printf("wrote %s (%dx%d) after %d ticks\n", path.c_str(), out_w, out_h, ticks);
    return ok ? 0 : 1;
}

}  // namespace gf
