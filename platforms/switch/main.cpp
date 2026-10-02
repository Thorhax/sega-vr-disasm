// Virtua Racing Deluxe - Standalone Nintendo Switch Runner
// Hardware-accelerated Sega 32X runtime powered by devkitPro, libnx & SDL2

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <SDL2/SDL.h>

#if defined(__SWITCH__)
#include <switch.h>
#include <unistd.h>
#include <sys/stat.h>

#include "runtime/log.h"

static PadState g_nx_pad1;
static PadState g_nx_pad2;

extern "C" void userAppInit(void)
{
    romfsInit();
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/virtuaracing32x", 0777);
    mkdir("sdmc:/switch/virtuaracing32x/save", 0777);
    chdir("sdmc:/switch/virtuaracing32x");

    chaotix::log_open_file("sdmc:/switch/virtuaracing32x/debug.log");
    chaotix::log_set_level(chaotix::LogLevel::Warn);
    chaotix::log_msg(chaotix::LogLevel::Warn, "Init", "Virtua Racing Deluxe starting on Nintendo Switch");

    padConfigureInput(2, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&g_nx_pad1);
    padInitialize(&g_nx_pad2, HidNpadIdType_No2);
}

extern "C" void userAppExit(void)
{
    chaotix::log_msg(chaotix::LogLevel::Warn, "Exit", "Virtua Racing Deluxe shutting down cleanly");
    chaotix::log_close_file();
    romfsExit();
}
#endif

#include "runtime/system.h"
#include "runtime/rom.h"
#include "input/input.h"
#include "runtime/log.h"
#include "platform/frame_pacing.h"
#include "font8x8.h"

using namespace chaotix;

namespace {

constexpr int kSwitchWidth = 1280;
constexpr int kSwitchHeight = 720;
constexpr double kTargetFps = 60.0;

enum class AspectMode {
    True169 = 0,     // 1280x720 True Widescreen (wide_extra = 64, 448x224 buffer, 1:1 pixel aspect)
    Clean43 = 1,     // 960x720 Clean 4:3 (wide_extra = 0, 320x224 buffer, centered pillarbox)
    Integer3x = 2,   // 960x672 Pixel-Perfect 3x Integer Scale (wide_extra = 0, 320x224 buffer)
    Stretched169 = 3 // 1280x720 Fullscreen Stretched (wide_extra = 0, 320x224 stretched)
};

const char* aspect_mode_name(AspectMode m)
{
    switch (m) {
    case AspectMode::True169: return "True 16:9 Widescreen (Native 1:1 FOV)";
    case AspectMode::Clean43: return "Clean 4:3 (Original CRT Pillarbox)";
    case AspectMode::Integer3x: return "Pixel-Perfect 3x (960x672)";
    case AspectMode::Stretched169: return "16:9 Fullscreen (Stretched)";
    }
    return "Unknown";
}

const char* sh2_boost_name(int boost)
{
    switch (boost) {
    case 100: return "Stock 1.0x (~20 FPS Original)";
    case 150: return "Smooth 1.5x (~30 FPS Boost)";
    case 200: return "Ultra 2.0x (Target 60 FPS)";
    case 300: return "Turbo 3.0x (Maximum Framerate)";
    }
    return "Custom";
}

const char* edge_blend_name(EdgeBlendMode m)
{
    switch (m) {
    case EdgeBlendMode::None: return "Off (Raw Backdrop)";
    case EdgeBlendMode::SoftFeather: return "Soft Feather (Smooth 16px)";
    case EdgeBlendMode::Vignette: return "Subtle Vignette (Dark Edges)";
    case EdgeBlendMode::DarkPillars: return "Dark Pillars (Black Borders)";
    }
    return "None";
}

struct AppConfig {
    AspectMode aspect = AspectMode::True169;
    int sh2_boost = 200; // Ultra 2.0x 60 FPS target
    EdgeBlendMode edge_blend = EdgeBlendMode::SoftFeather;
    bool cheat_infinite_time = false;
    bool cheat_super_speed = false;
    bool cheat_freeze_ai = false;
    bool show_osd = false;
};

void load_config(AppConfig& cfg)
{
    std::ifstream f("sdmc:/switch/virtuaracing32x/config.ini");
    if (!f.is_open()) f.open("config.ini");
    if (!f.is_open()) return;

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        while (!key.empty() && key.back() == ' ') key.pop_back();
        while (!val.empty() && val.front() == ' ') val.erase(val.begin());

        if (key == "aspect") cfg.aspect = static_cast<AspectMode>(std::atoi(val.c_str()) % 4);
        else if (key == "sh2_boost") {
            int b = std::atoi(val.c_str());
            if (b >= 100 && b <= 300) cfg.sh2_boost = b;
        }
        else if (key == "edge_blend") cfg.edge_blend = static_cast<EdgeBlendMode>(std::atoi(val.c_str()) % 4);
        else if (key == "cheat_time") cfg.cheat_infinite_time = (std::atoi(val.c_str()) != 0);
        else if (key == "cheat_speed") cfg.cheat_super_speed = (std::atoi(val.c_str()) != 0);
        else if (key == "cheat_freeze") cfg.cheat_freeze_ai = (std::atoi(val.c_str()) != 0);
        else if (key == "show_osd") cfg.show_osd = (std::atoi(val.c_str()) != 0);
    }
}

void save_config(const AppConfig& cfg)
{
    std::ofstream f("sdmc:/switch/virtuaracing32x/config.ini");
    if (!f.is_open()) f.open("config.ini");
    if (!f.is_open()) return;

    f << "# Virtua Racing Deluxe Nintendo Switch Configuration\n";
    f << "aspect=" << static_cast<int>(cfg.aspect) << "\n";
    f << "sh2_boost=" << cfg.sh2_boost << "\n";
    f << "edge_blend=" << static_cast<int>(cfg.edge_blend) << "\n";
    f << "cheat_time=" << (cfg.cheat_infinite_time ? 1 : 0) << "\n";
    f << "cheat_speed=" << (cfg.cheat_super_speed ? 1 : 0) << "\n";
    f << "cheat_freeze=" << (cfg.cheat_freeze_ai ? 1 : 0) << "\n";
    f << "show_osd=" << (cfg.show_osd ? 1 : 0) << "\n";
}

void apply_config(Machine& m, const AppConfig& cfg)
{
    if (cfg.aspect == AspectMode::True169) {
        m.wide_extra = 64;
        m.wide_active = true;
    } else {
        m.wide_extra = 0;
        m.wide_active = false;
    }
    m.sh2_speed_multiplier = cfg.sh2_boost;
    m.edge_blend_mode = cfg.edge_blend;
}

struct ControllerMap {
    SDL_GameController* controller = nullptr;
    int instance_id = -1;
};

std::string find_rom_file()
{
    const std::vector<std::string> candidates = {
        "sdmc:/switch/virtuaracing32x/vr_rebuild.32x",
        "sdmc:/switch/virtuaracing32x/rom.bin",
        "sdmc:/switch/virtuaracing32x/Virtua Racing Deluxe (USA).32x",
        "romfs:/rom.bin",
        "romfs:/vr_rebuild.32x",
        "romfs:/rom/rom.bin",
        "vr_rebuild.32x",
        "build/vr_rebuild.32x"
    };

    for (const auto& path : candidates) {
        std::ifstream f(path, std::ios::binary);
        if (f.good()) {
            return path;
        }
    }
    return "";
}

const char* kSramPath = "sdmc:/switch/virtuaracing32x/save/vrd.srm";

void load_sram(Machine& m)
{
    std::ifstream f(kSramPath, std::ios::binary);
    if (f.good()) {
        f.read(reinterpret_cast<char*>(m.sram), sizeof(m.sram));
        std::printf("[SRAM] Loaded battery save from %s\n", kSramPath);
    }
}

void save_sram(Machine& m)
{
    std::ofstream f(kSramPath, std::ios::binary);
    if (f.good()) {
        f.write(reinterpret_cast<const char*>(m.sram), sizeof(m.sram));
        std::printf("[SRAM] Saved battery records to %s\n", kSramPath);
        m.sram_dirty = false;
    }
}

struct NavInput {
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool confirm = false;
    bool back = false;
    bool toggle_menu = false;
};

NavInput read_nav_input(SDL_GameController* pad)
{
    NavInput nav;
#if defined(__SWITCH__)
    padUpdate(&g_nx_pad1);
    const u64 kHeld = padGetButtons(&g_nx_pad1);
    const HidAnalogStickState stick = padGetStickPos(&g_nx_pad1, 0);
    constexpr int32_t kDead = 12000;

    if ((kHeld & HidNpadButton_AnyUp) || stick.y > kDead) nav.up = true;
    if ((kHeld & HidNpadButton_AnyDown) || stick.y < -kDead) nav.down = true;
    if ((kHeld & HidNpadButton_AnyLeft) || stick.x < -kDead) nav.left = true;
    if ((kHeld & HidNpadButton_AnyRight) || stick.x > kDead) nav.right = true;

    if (kHeld & (HidNpadButton_A | HidNpadButton_Plus)) nav.confirm = true;
    if (kHeld & (HidNpadButton_B | HidNpadButton_Minus)) nav.back = true;
    if (kHeld & HidNpadButton_Minus) nav.toggle_menu = true;
#endif

    if (pad) {
        int16_t lx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
        int16_t ly = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
        constexpr int16_t kDead = 15000;

        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP) || ly < -kDead) nav.up = true;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN) || ly > kDead) nav.down = true;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT) || lx < -kDead) nav.left = true;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || lx > kDead) nav.right = true;

        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A) || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START)) nav.confirm = true;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B) || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_BACK)) nav.back = true;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_BACK)) nav.toggle_menu = true;
    }

    return nav;
}

uint16_t read_game_pad(int player_idx, SDL_GameController* pad, bool* out_toggle_aspect, bool* out_toggle_menu, bool* out_exit_req)
{
    uint16_t btns = 0;

#if defined(__SWITCH__)
    if (player_idx == 0) {
        padUpdate(&g_nx_pad1);
        const u64 kHeld = padGetButtons(&g_nx_pad1);
        const HidAnalogStickState stick_l = padGetStickPos(&g_nx_pad1, 0);
        const HidAnalogStickState stick_r = padGetStickPos(&g_nx_pad1, 1);

        constexpr int32_t kDeadzone = 8000;

        // Steering & Horizontal Direction (Left/Right Stick + D-Pad)
        if (stick_l.x < -kDeadzone || stick_r.x < -kDeadzone || (kHeld & HidNpadButton_AnyLeft))
            btns |= PAD_LEFT;
        if (stick_l.x > kDeadzone || stick_r.x > kDeadzone || (kHeld & HidNpadButton_AnyRight))
            btns |= PAD_RIGHT;

        // Vertical Direction (Menu navigation / Views)
        if (stick_l.y > kDeadzone || stick_r.y > kDeadzone || (kHeld & HidNpadButton_AnyUp))
            btns |= PAD_UP;
        if (stick_l.y < -kDeadzone || stick_r.y < -kDeadzone || (kHeld & HidNpadButton_AnyDown))
            btns |= PAD_DOWN;

        // Driving / Acceleration / Braking
        if (kHeld & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_ZR)) btns |= PAD_B;
        if (kHeld & (HidNpadButton_Y | HidNpadButton_ZL)) btns |= (PAD_A | PAD_C);

        // 4 Camera Views
        if (kHeld & HidNpadButton_X) btns |= PAD_X;
        if (kHeld & HidNpadButton_L) btns |= PAD_Y;
        if (kHeld & HidNpadButton_R) btns |= PAD_Z;

        // Start / Pause
        if (kHeld & HidNpadButton_Plus) btns |= PAD_START;

        // Hotkeys & Menu trigger
        if (out_toggle_menu && (kHeld & HidNpadButton_Minus))
            *out_toggle_menu = true;
        if (out_toggle_aspect && (kHeld & (HidNpadButton_StickL | HidNpadButton_StickR)))
            *out_toggle_aspect = true;
        if (out_exit_req && ((kHeld & HidNpadButton_Plus) && (kHeld & HidNpadButton_Minus)))
            *out_exit_req = true;
    } else if (player_idx == 1 && padIsConnected(&g_nx_pad2)) {
        padUpdate(&g_nx_pad2);
        const u64 kHeld = padGetButtons(&g_nx_pad2);
        const HidAnalogStickState stick_l = padGetStickPos(&g_nx_pad2, 0);
        constexpr int32_t kDeadzone = 8000;

        if (stick_l.x < -kDeadzone || (kHeld & HidNpadButton_AnyLeft)) btns |= PAD_LEFT;
        if (stick_l.x > kDeadzone || (kHeld & HidNpadButton_AnyRight)) btns |= PAD_RIGHT;
        if (stick_l.y > kDeadzone || (kHeld & HidNpadButton_AnyUp)) btns |= PAD_UP;
        if (stick_l.y < -kDeadzone || (kHeld & HidNpadButton_AnyDown)) btns |= PAD_DOWN;

        if (kHeld & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_ZR)) btns |= PAD_B;
        if (kHeld & (HidNpadButton_Y | HidNpadButton_ZL)) btns |= (PAD_A | PAD_C);

        if (kHeld & HidNpadButton_X) btns |= PAD_X;
        if (kHeld & HidNpadButton_L) btns |= PAD_Y;
        if (kHeld & HidNpadButton_R) btns |= PAD_Z;
        if (kHeld & HidNpadButton_Plus) btns |= PAD_START;
    }
#endif

    if (pad) {
        const int16_t lx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
        const int16_t ly = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
        constexpr int16_t kSdlDeadzone = 10000;

        if (lx < -kSdlDeadzone || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) btns |= PAD_LEFT;
        if (lx > kSdlDeadzone || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) btns |= PAD_RIGHT;
        if (ly < -kSdlDeadzone || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP)) btns |= PAD_UP;
        if (ly > kSdlDeadzone || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) btns |= PAD_DOWN;

        const int16_t r_trig = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
        const int16_t l_trig = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT);

        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A) ||
            SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B) ||
            r_trig > 10000) btns |= PAD_B;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_Y) ||
            l_trig > 10000) btns |= (PAD_A | PAD_C);

        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_X)) btns |= PAD_X;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) btns |= PAD_Y;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) btns |= PAD_Z;

        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START)) btns |= PAD_START;

        if (out_toggle_menu && SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_BACK))
            *out_toggle_menu = true;
        if (out_toggle_aspect && (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_LEFTSTICK) ||
                                  SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_RIGHTSTICK)))
            *out_toggle_aspect = true;
        if (out_exit_req && (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START) &&
                             SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_BACK)))
            *out_exit_req = true;
    }

    return btns;
}

enum MenuItem {
    ITEM_RESUME = 0,
    ITEM_ASPECT,
    ITEM_SH2_BOOST,
    ITEM_EDGE_BLEND,
    ITEM_CHEAT_TIME,
    ITEM_CHEAT_SPEED,
    ITEM_CHEAT_FREEZE,
    ITEM_SHOW_OSD,
    ITEM_RESET,
    ITEM_EXIT,
    ITEM_TOTAL
};

void render_overlay(SDL_Renderer* ren, const AppConfig& cfg, int selected_item)
{
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    // Dark screen dimming
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 190);
    SDL_Rect screenRect{0, 0, kSwitchWidth, kSwitchHeight};
    SDL_RenderFillRect(ren, &screenRect);

    // Main dialog window (880 x 540) centered
    constexpr int pw = 880;
    constexpr int ph = 540;
    constexpr int px = (kSwitchWidth - pw) / 2;
    constexpr int py = (kSwitchHeight - ph) / 2;

    SDL_Rect panelBg{px, py, pw, ph};
    SDL_SetRenderDrawColor(ren, 15, 23, 42, 245); // Deep slate navy
    SDL_RenderFillRect(ren, &panelBg);

    // Accent borders
    SDL_Rect outerBorder{px, py, pw, ph};
    SDL_SetRenderDrawColor(ren, 59, 130, 246, 255); // Blue primary border
    SDL_RenderDrawRect(ren, &outerBorder);

    SDL_Rect innerBorder{px + 3, py + 3, pw - 6, ph - 6};
    SDL_SetRenderDrawColor(ren, 96, 165, 250, 160);
    SDL_RenderDrawRect(ren, &innerBorder);

    // Header banner
    SDL_Rect headerBg{px + 4, py + 4, pw - 8, 48};
    SDL_SetRenderDrawColor(ren, 30, 41, 59, 255);
    SDL_RenderFillRect(ren, &headerBg);

    const char* title = "VIRTUA RACING DELUXE - SETTINGS";
    int tw = ui::text_width(title, 2);
    ui::draw_text(ren, px + (pw - tw) / 2, py + 16, title, 251, 191, 36, 2); // Amber gold title

    // Menu items
    int item_y = py + 68;
    for (int i = 0; i < ITEM_TOTAL; ++i) {
        char label_buf[128] = {};
        switch (i) {
        case ITEM_RESUME:
            std::snprintf(label_buf, sizeof(label_buf), "Resume Game");
            break;
        case ITEM_ASPECT:
            std::snprintf(label_buf, sizeof(label_buf), "Screen Ratio:      < %s >", aspect_mode_name(cfg.aspect));
            break;
        case ITEM_SH2_BOOST:
            std::snprintf(label_buf, sizeof(label_buf), "3D Frame Rate:     < %s >", sh2_boost_name(cfg.sh2_boost));
            break;
        case ITEM_EDGE_BLEND:
            std::snprintf(label_buf, sizeof(label_buf), "16:9 Edge Blend:   < %s >", edge_blend_name(cfg.edge_blend));
            break;
        case ITEM_CHEAT_TIME:
            std::snprintf(label_buf, sizeof(label_buf), "Infinite Time:     < %s >", cfg.cheat_infinite_time ? "ON (Lock 99s)" : "OFF");
            break;
        case ITEM_CHEAT_SPEED:
            std::snprintf(label_buf, sizeof(label_buf), "Super Turbo Speed: < %s >", cfg.cheat_super_speed ? "ON (300+ km/h)" : "OFF");
            break;
        case ITEM_CHEAT_FREEZE:
            std::snprintf(label_buf, sizeof(label_buf), "Freeze AI Racers:  < %s >", cfg.cheat_freeze_ai ? "ON (Stopped)" : "OFF");
            break;
        case ITEM_SHOW_OSD:
            std::snprintf(label_buf, sizeof(label_buf), "Performance OSD:   < %s >", cfg.show_osd ? "ON" : "OFF");
            break;
        case ITEM_RESET:
            std::snprintf(label_buf, sizeof(label_buf), "Reset Game Session");
            break;
        case ITEM_EXIT:
            std::snprintf(label_buf, sizeof(label_buf), "Exit Application");
            break;
        }

        if (i == selected_item) {
            // Selected highlight bar
            SDL_Rect selRect{px + 20, item_y - 2, pw - 40, 32};
            SDL_SetRenderDrawColor(ren, 29, 78, 216, 220); // Royal blue
            SDL_RenderFillRect(ren, &selRect);

            SDL_SetRenderDrawColor(ren, 96, 165, 250, 255);
            SDL_RenderDrawRect(ren, &selRect);

            ui::draw_text(ren, px + 36, item_y + 6, "> ", 250, 204, 21, 2);
            ui::draw_text(ren, px + 68, item_y + 6, label_buf, 255, 255, 255, 2);
        } else {
            ui::draw_text(ren, px + 68, item_y + 6, label_buf, 203, 213, 225, 2);
        }

        item_y += 38;
    }

    // Footer divider line
    SDL_SetRenderDrawColor(ren, 51, 65, 85, 255);
    SDL_RenderDrawLine(ren, px + 16, py + ph - 46, px + pw - 16, py + ph - 46);

    // Footer navigation help
    const char* footer = "[D-Pad / Stick] Navigate    [Left / Right / A] Change    [B / -] Resume";
    int fw = ui::text_width(footer, 1);
    ui::draw_text(ren, px + (pw - fw) / 2, py + ph - 30, footer, 148, 163, 184, 1);
}

void render_osd(SDL_Renderer* ren, float fps, float emu_ms, int boost, AspectMode asp)
{
    char buf[128];
    const char* a_str = (asp == AspectMode::True169) ? "16:9 Wide" : (asp == AspectMode::Clean43) ? "4:3 CRT" : (asp == AspectMode::Integer3x) ? "3x Int" : "16:9 Stretch";
    std::snprintf(buf, sizeof(buf), "FPS: %.1f | Emu: %.2f ms | SH-2: %.1fx | %s", fps, emu_ms, float(boost) / 100.0f, a_str);

    int tw = ui::text_width(buf, 1);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 180);
    SDL_Rect bg{12, 12, tw + 16, 24};
    SDL_RenderFillRect(ren, &bg);
    SDL_SetRenderDrawColor(ren, 59, 130, 246, 200);
    SDL_RenderDrawRect(ren, &bg);

    ui::draw_text(ren, 20, 18, buf, 74, 222, 128, 1); // Mint green
}

} // namespace

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    std::printf("====================================================\n");
    std::printf("  Virtua Racing Deluxe - Nintendo Switch Edition     \n");
    std::printf("  Full-Speed Dual SH-2 Runtime with True 16:9 Wide   \n");
    std::printf("====================================================\n");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) != 0) {
        std::fprintf(stderr, "Failed to initialize SDL2: %s\n", SDL_GetError());
        return 1;
    }

    // Create 720p fullscreen window
    SDL_Window* window = SDL_CreateWindow(
        "Virtua Racing Deluxe",
        0, 0,
        kSwitchWidth, kSwitchHeight,
        SDL_WINDOW_FULLSCREEN | SDL_WINDOW_SHOWN
    );
    if (!window) {
        std::fprintf(stderr, "Failed to create SDL window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Hardware accelerated renderer synchronized to Switch display VSync (60 Hz)
    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    if (!renderer) {
        std::fprintf(stderr, "Failed to create SDL renderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Streaming texture sized for full widescreen canvas (448x240)
    SDL_Texture* texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_XRGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        kScreenWidth, kScreenHeight
    );
    if (!texture) {
        std::fprintf(stderr, "Failed to create texture: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);

    // Audio setup
    SDL_AudioDeviceID audio_dev = 0;
    SDL_AudioSpec want{}, have{};
    want.freq = int(kAudioRate + 0.5);
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    audio_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (audio_dev > 0) {
        SDL_PauseAudioDevice(audio_dev, 0);
        std::printf("[Audio] Initialized SDL audio device (freq=%d, channels=%d, samples=%d)\n",
            have.freq, have.channels, have.samples);
    } else {
        std::fprintf(stderr, "[Audio] Warning: Failed to open audio device: %s\n", SDL_GetError());
    }

    // Connect controllers
    std::vector<ControllerMap> controllers;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            SDL_GameController* c = SDL_GameControllerOpen(i);
            if (c) {
                controllers.push_back({c, SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(c))});
                std::printf("[Input] Connected controller %d: %s\n", i, SDL_GameControllerName(c));
            }
        }
    }

    // Locate and load ROM
    std::string rom_path = find_rom_file();
    if (rom_path.empty()) {
        std::fprintf(stderr, "[Error] Could not find Virtua Racing Deluxe ROM (vr_rebuild.32x or rom.bin)!\n");
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR,
            "Virtua Racing Deluxe",
            "ROM file not found!\nPlease place vr_rebuild.32x inside sdmc:/switch/virtuaracing32x/",
            window
        );
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    std::printf("[ROM] Loading ROM from: %s\n", rom_path.c_str());
    auto machine = std::make_unique<Machine>();
    std::string err;
    if (!machine->load_rom(rom_path, &err)) {
        std::fprintf(stderr, "[Error] Failed to load ROM: %s\n", err.c_str());
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR,
            "Virtua Racing Deluxe",
            err.c_str(),
            window
        );
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    machine->reset();
    machine->audio_enabled = (audio_dev > 0);
    machine->input.six_button[0] = true;
    machine->input.six_button[1] = true;

    // Load persistent user settings
    AppConfig cfg;
    load_config(cfg);
    apply_config(*machine, cfg);
    load_sram(*machine);

    std::printf("[Ready] System reset complete. Aspect: %s | SH-2 Boost: %s | Edge Blend: %s\n",
        aspect_mode_name(cfg.aspect), sh2_boost_name(cfg.sh2_boost), edge_blend_name(cfg.edge_blend));

    bool running = true;
    uint32_t sram_dirty_timer = 0;
    bool prev_aspect_btn = false;
    bool prev_menu_btn = false;

    bool menu_open = false;
    int menu_selected_item = 0;
    uint32_t nav_debounce = 0;

    // Performance timing counters
    float smoothed_fps = 60.0f;
    float smoothed_emu_ms = 2.0f;
    auto last_fps_time = std::chrono::steady_clock::now();
    int fps_frame_counter = 0;

    while (running) {
#if defined(__SWITCH__)
        if (!appletMainLoop()) {
            break;
        }
#endif

        // Event handling
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = false;
            } else if (ev.type == SDL_CONTROLLERDEVICEADDED) {
                if (SDL_IsGameController(ev.cdevice.which)) {
                    SDL_GameController* c = SDL_GameControllerOpen(ev.cdevice.which);
                    if (c) {
                        controllers.push_back({c, SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(c))});
                    }
                }
            } else if (ev.type == SDL_CONTROLLERDEVICEREMOVED) {
                controllers.erase(
                    std::remove_if(controllers.begin(), controllers.end(),
                        [&ev](const ControllerMap& cm) {
                            if (cm.instance_id == ev.cdevice.which) {
                                SDL_GameControllerClose(cm.controller);
                                return true;
                            }
                            return false;
                        }),
                    controllers.end()
                );
            }
        }

        SDL_GameController* pad1 = (!controllers.empty()) ? controllers[0].controller : nullptr;
        SDL_GameController* pad2 = (controllers.size() > 1) ? controllers[1].controller : nullptr;

        bool p2_connected = false;
#if defined(__SWITCH__)
        p2_connected = padIsConnected(&g_nx_pad2);
#endif
        if (pad2 != nullptr) p2_connected = true;

        if (menu_open) {
            // Overlay Navigation
            NavInput nav = read_nav_input(pad1);

            if (nav.toggle_menu && !prev_menu_btn) {
                menu_open = false;
                if (audio_dev > 0) SDL_PauseAudioDevice(audio_dev, 0);
            } else if (nav.back && nav_debounce == 0) {
                menu_open = false;
                if (audio_dev > 0) SDL_PauseAudioDevice(audio_dev, 0);
                nav_debounce = 15;
            } else if (nav.up && nav_debounce == 0) {
                menu_selected_item = (menu_selected_item + ITEM_TOTAL - 1) % ITEM_TOTAL;
                nav_debounce = 10;
            } else if (nav.down && nav_debounce == 0) {
                menu_selected_item = (menu_selected_item + 1) % ITEM_TOTAL;
                nav_debounce = 10;
            } else if ((nav.left || nav.right || nav.confirm) && nav_debounce == 0) {
                const int dir = nav.left ? -1 : 1;
                switch (menu_selected_item) {
                case ITEM_RESUME:
                    menu_open = false;
                    if (audio_dev > 0) SDL_PauseAudioDevice(audio_dev, 0);
                    break;
                case ITEM_ASPECT:
                    cfg.aspect = static_cast<AspectMode>((static_cast<int>(cfg.aspect) + dir + 4) % 4);
                    apply_config(*machine, cfg);
                    save_config(cfg);
                    break;
                case ITEM_SH2_BOOST: {
                    const int boosts[] = {100, 150, 200, 300};
                    int cur_idx = 0;
                    for (int k = 0; k < 4; ++k) if (boosts[k] == cfg.sh2_boost) cur_idx = k;
                    cur_idx = (cur_idx + dir + 4) % 4;
                    cfg.sh2_boost = boosts[cur_idx];
                    apply_config(*machine, cfg);
                    save_config(cfg);
                    break;
                }
                case ITEM_EDGE_BLEND:
                    cfg.edge_blend = static_cast<EdgeBlendMode>((static_cast<int>(cfg.edge_blend) + dir + 4) % 4);
                    apply_config(*machine, cfg);
                    save_config(cfg);
                    break;
                case ITEM_CHEAT_TIME:
                    cfg.cheat_infinite_time = !cfg.cheat_infinite_time;
                    save_config(cfg);
                    break;
                case ITEM_CHEAT_SPEED:
                    cfg.cheat_super_speed = !cfg.cheat_super_speed;
                    save_config(cfg);
                    break;
                case ITEM_CHEAT_FREEZE:
                    cfg.cheat_freeze_ai = !cfg.cheat_freeze_ai;
                    save_config(cfg);
                    break;
                case ITEM_SHOW_OSD:
                    cfg.show_osd = !cfg.show_osd;
                    save_config(cfg);
                    break;
                case ITEM_RESET:
                    machine->reset();
                    apply_config(*machine, cfg);
                    load_sram(*machine);
                    menu_open = false;
                    if (audio_dev > 0) SDL_PauseAudioDevice(audio_dev, 0);
                    break;
                case ITEM_EXIT:
                    running = false;
                    break;
                }
                nav_debounce = 12;
            }

            if (nav_debounce > 0) nav_debounce--;
            prev_menu_btn = nav.toggle_menu;

        } else {
            // Normal Gameplay
            bool toggle_aspect = false;
            bool toggle_menu = false;
            bool exit_req = false;

            uint16_t p1_buttons = read_game_pad(0, pad1, &toggle_aspect, &toggle_menu, &exit_req);
            uint16_t p2_buttons = p2_connected ? read_game_pad(1, pad2, nullptr, nullptr, nullptr) : 0;

            if (exit_req) {
                running = false;
                break;
            }

            if (toggle_menu && !prev_menu_btn) {
                menu_open = true;
                if (audio_dev > 0) SDL_PauseAudioDevice(audio_dev, 1);
                nav_debounce = 15;
            }
            prev_menu_btn = toggle_menu;

            // Aspect ratio quick cycling (L3 or R3 click)
            if (toggle_aspect && !prev_aspect_btn) {
                cfg.aspect = static_cast<AspectMode>((static_cast<int>(cfg.aspect) + 1) % 4);
                apply_config(*machine, cfg);
                save_config(cfg);
                std::printf("[Display] Switched aspect mode to: %s\n", aspect_mode_name(cfg.aspect));
            }
            prev_aspect_btn = toggle_aspect;

            machine->input.six_button[0] = true;
            machine->input.six_button[1] = p2_connected;
            machine->input.pad[0] = p1_buttons;
            machine->input.pad[1] = p2_buttons;

            // Apply active cheats
            if (cfg.cheat_infinite_time) {
                // Freeze race countdown timer ($FFFFC04E) at 99s
                machine->wram[0xC04E] = 0x00;
                machine->wram[0xC04F] = 0x63;
            }

            if (cfg.cheat_super_speed) {
                // Player 1 entity is at $FFFF9000, speed at +$04
                uint16_t spd = (uint16_t(machine->wram[0x9004]) << 8) | machine->wram[0x9005];
                if (spd > 0x0050 && (p1_buttons & PAD_B)) {
                    if (spd < 0x0750) {
                        spd += 0x000E;
                        machine->wram[0x9004] = uint8_t(spd >> 8);
                        machine->wram[0x9005] = uint8_t(spd & 0xFF);
                    }
                }
            }

            if (cfg.cheat_freeze_ai) {
                // Zero speeds of all AI entities ($9200-$9E00)
                for (uint32_t base = 0x9200; base <= 0x9E00; base += 0x0200) {
                    machine->wram[base + 0x04] = 0;
                    machine->wram[base + 0x05] = 0;
                }
            }

            // Measure emulated frame time
            auto emu_t0 = std::chrono::steady_clock::now();
            machine->run_frame();
            auto emu_t1 = std::chrono::steady_clock::now();
            float emu_ms = std::chrono::duration<float, std::milli>(emu_t1 - emu_t0).count();
            smoothed_emu_ms = smoothed_emu_ms * 0.9f + emu_ms * 0.1f;

            // Queue audio with adaptive rate management
            if (audio_dev > 0 && !machine->audio_out.empty()) {
                SDL_QueueAudio(
                    audio_dev,
                    machine->audio_out.data(),
                    uint32_t(machine->audio_out.size() * sizeof(int16_t))
                );
                machine->audio_out.clear();

                const uint32_t queued = SDL_GetQueuedAudioSize(audio_dev);
                constexpr uint32_t kTargetBufferSize = uint32_t(kAudioRate * 0.04 * 4); // ~40 ms target
                if (queued > kTargetBufferSize * 8) {
                    SDL_ClearQueuedAudio(audio_dev);
                }
            } else {
                machine->audio_out.clear();
            }

            // SRAM autosave handling
            if (machine->sram_dirty) {
                if (++sram_dirty_timer > 60) {
                    save_sram(*machine);
                    sram_dirty_timer = 0;
                }
            } else {
                sram_dirty_timer = 0;
            }
        }

        // Viewport & Rect calculations
        SDL_Rect srcRect{0, 0, machine->fb_width, machine->fb_height};
        SDL_Rect dstRect{0, 0, kSwitchWidth, kSwitchHeight};

        switch (cfg.aspect) {
        case AspectMode::True169:
            // 448x224 framebuffer fills 1280x720 screen with exact 1:1 pixel aspect
            dstRect = SDL_Rect{0, 0, kSwitchWidth, kSwitchHeight};
            break;
        case AspectMode::Clean43:
            // 960x720 centered pillarbox (clean 4:3)
            {
                constexpr int w43 = int(kSwitchHeight * (4.0 / 3.0));
                constexpr int xOff = (kSwitchWidth - w43) / 2;
                dstRect = SDL_Rect{xOff, 0, w43, kSwitchHeight};
            }
            break;
        case AspectMode::Integer3x:
            // 960x672 centered (exact 3x integer scaling: 320*3 x 224*3)
            dstRect = SDL_Rect{(kSwitchWidth - 960) / 2, (kSwitchHeight - 672) / 2, 960, 672};
            break;
        case AspectMode::Stretched169:
            // 1280x720 full anamorphic stretch
            dstRect = SDL_Rect{0, 0, kSwitchWidth, kSwitchHeight};
            break;
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        // Update texture with 32X composite framebuffer
        SDL_UpdateTexture(
            texture,
            nullptr,
            machine->framebuffer,
            kScreenWidth * sizeof(uint32_t)
        );

        SDL_RenderCopy(renderer, texture, &srcRect, &dstRect);

        // Calculate FPS
        fps_frame_counter++;
        auto now = std::chrono::steady_clock::now();
        float elapsed_sec = std::chrono::duration<float>(now - last_fps_time).count();
        if (elapsed_sec >= 0.5f) {
            smoothed_fps = float(fps_frame_counter) / elapsed_sec;
            fps_frame_counter = 0;
            last_fps_time = now;
        }

        // Render In-Game Performance OSD
        if (cfg.show_osd && !menu_open) {
            render_osd(renderer, smoothed_fps, smoothed_emu_ms, cfg.sh2_boost, cfg.aspect);
        }

        // Render Overlay Settings Menu if active
        if (menu_open) {
            render_overlay(renderer, cfg, menu_selected_item);
        }

        SDL_RenderPresent(renderer);
    }

    // Save SRAM before exit if dirty
    if (machine->sram_dirty) {
        save_sram(*machine);
    }

    std::printf("[Exit] Shutting down Virtua Racing Deluxe cleanly...\n");

    for (auto& cm : controllers) {
        if (cm.controller) SDL_GameControllerClose(cm.controller);
    }
    if (audio_dev > 0) SDL_CloseAudioDevice(audio_dev);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
