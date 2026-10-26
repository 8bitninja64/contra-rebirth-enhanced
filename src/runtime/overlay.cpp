// F1 settings menu, Dear ImGui (tools/fetch_imgui.py). Same design as the Blitz 2003 and
// Tenchu Z ports: a section list on the left, the section's options on the right, button
// hints with Resume / Quit game in the footer. Everything applies at once and is saved
// to the .ini; options that need a restart are grouped under "Applied on the next start".
//
// Threads: the UI thread (gl_platform_sdl.cpp) feeds SDL events and pad state; the present
// thread draws. g_mx serializes the two. Open/close: Esc or F1, Back+Start on a controller.
// The game stands still while the menu is open (no retraces, no audio periods).
#if defined(_WIN32) || defined(USE_SDL)
#include "game.h"
#include "overlay.h"
#include "config.h"
#include "enhancements.h"
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"
#include <SDL2/SDL.h>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <cstdlib>

bool g_vsync_dirty_from_menu = false;

namespace {
std::recursive_mutex g_mx;
std::atomic<bool> g_open{false};
std::atomic<bool> g_hold{false};        // closed, but the input that closed it is still held
std::atomic<uint32_t> g_actions{0};
std::atomic<uint32_t> g_pad_held{0};
std::atomic<int> g_capture{-1};          // key binding waiting for a key (-1 none)
bool g_init = false;
uint32_t g_pad_prev = 0, g_pad_pressed = 0;

enum Section { kDisplay, kGraphics, kAudio, kControls, kGameplay, kCheats, kAbout, kSectionCount };
const char* kSectionNames[kSectionCount] = {"Display", "Graphics", "Audio", "Controls", "Gameplay", "Cheats", "About"};
int g_section = 0;
enum class Focus { None, Sections, Content };
Focus g_focus = Focus::Sections;

// Contra colours: charcoal panels, crimson and steel accents, gold highlights.
const ImVec4 kCrimson(0.72f, 0.09f, 0.12f, 1.0f);
const ImVec4 kSteel(0.33f, 0.40f, 0.52f, 1.0f);
const ImVec4 kGold(0.95f, 0.76f, 0.30f, 1.0f);
const ImVec4 kGreen(0.30f, 0.85f, 0.40f, 1.0f);
ImFont* g_body = nullptr;
ImFont* g_title = nullptr;
constexpr float kFontPx = 40.0f;   // rasterized large, scaled down to the window

void set_open(bool open) {
    if (g_open.exchange(open) == open) return;
    g_focus = Focus::Sections;
    g_capture = -1;
    g_hold = true;   // the game gets the controls back once everything is released
    std::lock_guard<std::recursive_mutex> lk(g_mx);
    if (g_init) { ImGui::GetIO().ClearInputKeys(); ImGui::GetIO().ClearInputMouse(); }
}

void apply_style() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 10; s.ChildRounding = 8; s.FrameRounding = 6; s.GrabRounding = 6; s.PopupRounding = 6;
    s.WindowBorderSize = 0; s.FrameBorderSize = 0;
    s.WindowPadding = ImVec2(18, 16); s.FramePadding = ImVec2(10, 6); s.ItemSpacing = ImVec2(10, 9);
    s.ScrollbarSize = 14;
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.07f, 0.96f);
    c[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.10f, 0.12f, 1.0f);
    c[ImGuiCol_PopupBg] = ImVec4(0.09f, 0.09f, 0.11f, 0.98f);
    c[ImGuiCol_Text] = ImVec4(0.95f, 0.94f, 0.92f, 1.0f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.58f, 0.58f, 0.62f, 1.0f);
    c[ImGuiCol_FrameBg] = ImVec4(0.17f, 0.17f, 0.20f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.24f, 0.28f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.30f, 0.30f, 0.35f, 1.0f);
    c[ImGuiCol_CheckMark] = kGold;
    c[ImGuiCol_SliderGrab] = kCrimson;
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.92f, 0.20f, 0.22f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.20f, 0.20f, 0.24f, 1.0f);
    c[ImGuiCol_ButtonHovered] = kSteel;
    c[ImGuiCol_ButtonActive] = kCrimson;
    c[ImGuiCol_Header] = ImVec4(0.22f, 0.22f, 0.27f, 1.0f);
    c[ImGuiCol_HeaderHovered] = kSteel;
    c[ImGuiCol_HeaderActive] = kCrimson;
    c[ImGuiCol_Separator] = ImVec4(0.30f, 0.30f, 0.36f, 1.0f);
    c[ImGuiCol_NavHighlight] = kGold;
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.03f);
}

// The system UI font (nothing to ship), else ImGui's built-in one.
void load_fonts() {
    ImGuiIO& io = ImGui::GetIO();
    auto load = [&](std::initializer_list<const char*> paths, float px) -> ImFont* {
        for (const char* p : paths)
            if (std::filesystem::exists(p))
                if (ImFont* f = io.Fonts->AddFontFromFileTTF(p, px)) return f;
        return nullptr;
    };
#ifdef _WIN32
    g_body = load({"C:/Windows/Fonts/segoeui.ttf"}, kFontPx);
    g_title = load({"C:/Windows/Fonts/seguibl.ttf", "C:/Windows/Fonts/segoeuib.ttf"}, kFontPx * 1.4f);
#else
    g_body = load({"/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf", "/usr/share/fonts/noto/NotoSans-Regular.ttf",
                   "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf"}, kFontPx);
    g_title = load({"/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf", "/usr/share/fonts/noto/NotoSans-Bold.ttf",
                    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf"},
                   kFontPx * 1.4f);
#endif
    if (!g_body) { ImFontConfig cfg; cfg.SizePixels = 13; g_body = io.Fonts->AddFontDefault(&cfg); }
    if (!g_title) g_title = g_body;
    io.FontDefault = g_body;
}

// Pads drive the menu as keyboard navigation.
void feed_navigation() {
    static uint32_t previous = 0;
    const uint32_t held = g_pad_held.load();
    ImGuiIO& io = ImGui::GetIO();
    const uint32_t changed = held ^ previous;
    auto key = [&](uint32_t bit, ImGuiKey k) { if (changed & bit) io.AddKeyEvent(k, (held & bit) != 0); };
    key(OV_PAD_UP, ImGuiKey_UpArrow); key(OV_PAD_DOWN, ImGuiKey_DownArrow);
    key(OV_PAD_LEFT, ImGuiKey_LeftArrow); key(OV_PAD_RIGHT, ImGuiKey_RightArrow);
    key(OV_PAD_A, ImGuiKey_Space);   // activate / toggle
    key(OV_PAD_B, ImGuiKey_Escape);  // cancel
    g_pad_pressed = held & ~previous;
    previous = held;
}

void focus_first_option() {
    if (g_focus != Focus::Content) return;
    ImGui::SetKeyboardFocusHere();
    g_focus = Focus::None;
}
void help(const char* text) {
    ImGui::Indent();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
    ImGui::Unindent();
}
void next_start_group() { ImGui::Spacing(); ImGui::Separator(); ImGui::TextDisabled("Applied on the next start:"); }

bool combo(const char* label, int* value, const char* const* names, int n) {
    bool changed = false;
    if (ImGui::BeginCombo(label, names[*value])) {
        for (int i = 0; i < n; i++)
            if (ImGui::Selectable(names[i], *value == i)) { *value = i; changed = true; }
        ImGui::EndCombo();
    }
    return changed;
}

// ---------------------------------------------------------------- pages
bool draw_display() {
    bool changed = false;
    focus_first_option();
    bool fs = g_cfg.fullscreen;
    if (ImGui::Checkbox("Fullscreen (F11 / Alt+Enter)", &fs)) { g_cfg.fullscreen = fs; g_actions |= OV_ACT_DISPLAY; changed = true; }
    {
        static const char* names[] = {"Borderless (desktop resolution)", "Exclusive (output resolution)"};
        int v = g_cfg.fullscreen_mode == "exclusive" ? 1 : 0;
        if (combo("Fullscreen mode", &v, names, 2)) { g_cfg.fullscreen_mode = v ? "exclusive" : "borderless"; g_actions |= OV_ACT_DISPLAY; changed = true; }
    }
    {
        static const char* names[] = {"Auto (fits the desktop)", "1280x720", "1920x1080", "2560x1440", "3840x2160", "Native (desktop)"};
        static const char* values[] = {"auto", "1280x720", "1920x1080", "2560x1440", "3840x2160", "native"};
        int v = 0;
        for (int i = 0; i < 6; i++) if (g_cfg.resolution == values[i]) v = i;
        if (combo("Output resolution", &v, names, 6)) { g_cfg.resolution = values[v]; g_actions |= OV_ACT_DISPLAY; changed = true; }
        help("Window size, and the display mode in exclusive fullscreen. Custom sizes: resolution = WxH in the .ini.");
    }
    {
        static const char* names[] = {"Fit (keep aspect)", "Integer (whole multiples of 240 lines)", "Stretch to window",
                                      "Stretch to 16:9"};
        static const char* values[] = {"fit", "integer", "stretch", "stretch169"};
        int v = g_cfg.scaling == "integer" ? 1 : g_cfg.scaling == "stretch" ? 2 : g_cfg.scaling == "stretch169" ? 3 : 0;
        if (combo("Scaling", &v, names, 4)) { g_cfg.scaling = values[v]; g_actions |= OV_ACT_DISPLAY; changed = true; }
        if (v == 3) help("The 4:3 picture stretched wide to fill a 16:9 area (bars only if the screen is not 16:9).");
    }
    {
        static const char* names[] = {"Smooth", "Sharp (crisp pixels, no shimmer)", "Nearest (raw pixels)"};
        static const char* values[] = {"smooth", "sharp", "nearest"};
        int v = g_cfg.filter == "sharp" ? 1 : g_cfg.filter == "nearest" ? 2 : 0;
        if (combo("Filter", &v, names, 3)) { g_cfg.filter = values[v]; g_actions |= OV_ACT_DISPLAY; changed = true; }
    }
    if (ImGui::Checkbox("VSync", &g_cfg.vsync)) { g_vsync_dirty_from_menu = true; changed = true; }
    changed |= ImGui::Checkbox("Show FPS counter", &g_cfg.show_perf);
    if (kOfferWidescreen) {   // shelved for now (config.h)
        next_start_group();
        static const char* names[] = {"4:3 (original)", "16:9 widescreen"};
        int v = g_cfg.widescreen ? 1 : 0;
        if (combo("Aspect ratio", &v, names, 2)) { g_cfg.widescreen = v; changed = true; }
        help("The game lays out its menus for this. True widescreen (Graphics) needs 16:9.");
    }
    return changed;
}

bool enh_checkbox(const char* id, const char* label) {
    bool on = enh_enabled(id);
    if (ImGui::Checkbox(label, &on)) { enh_set_enabled(id, on); return true; }
    return false;
}

bool draw_graphics() {
    bool changed = false;
    focus_first_option();
    if (kOfferWidescreen) {   // shelved for now (config.h)
        enh_checkbox("widescreen", "True widescreen");
        help(g_cfg.widescreen ? "Shows more of the level at the sides instead of black bars."
                              : "Needs Aspect ratio 16:9 (Display), applied on the next start.");
    }
    {
        static const char* names[] = {"1x  640x528 (original)", "2x  1280x1056", "3x  1920x1584", "4x  2560x2112"};
        int v = std::max(0, std::min(3, g_cfg.resolution_scale - 1));
        if (combo("Internal resolution", &v, names, 4)) { g_cfg.resolution_scale = v + 1; changed = true; }
    }
    {
        static const char* names[] = {"Off", "2x MSAA", "4x MSAA", "8x MSAA"};
        int v = g_cfg.msaa >= 8 ? 3 : g_cfg.msaa >= 4 ? 2 : g_cfg.msaa >= 2 ? 1 : 0;
        if (combo("Anti-aliasing", &v, names, 4)) { g_cfg.msaa = v ? 1 << v : 1; changed = true; }
    }
    {
        static const char* names[] = {"Off", "2x", "4x", "8x", "16x"};
        int v = g_cfg.anisotropy >= 16 ? 4 : g_cfg.anisotropy >= 8 ? 3 : g_cfg.anisotropy >= 4 ? 2 : g_cfg.anisotropy >= 2 ? 1 : 0;
        if (combo("Anisotropic filter", &v, names, 5)) { g_cfg.anisotropy = v ? 1 << v : 1; changed = true; }
    }
    changed |= ImGui::Checkbox("Texture smoothing (trilinear mipmaps)", &g_cfg.smooth_textures);
    return changed;
}

bool draw_audio() {
    bool changed = false;
    focus_first_option();
    changed |= ImGui::SliderInt("Volume", &g_cfg.volume, 0, 100, "%d%%");
    changed |= ImGui::Checkbox("Mute", &g_cfg.mute);
    return changed;
}

bool draw_controls() {
    bool changed = false;
    focus_first_option();
    {
        static const char* names[] = {"Wii Remote sideways", "Wii Remote + Nunchuk", "Classic Controller"};
        static const char* values[] = {"sideways", "nunchuk", "classic"};
        int v = g_cfg.controls == "nunchuk" ? 1 : g_cfg.controls == "classic" ? 2 : 0;
        if (combo("Controller type", &v, names, 3)) { g_cfg.controls = values[v]; changed = true; }
        help("Choose the same grip in the game's own menu.");
    }
    changed |= ImGui::Checkbox("Swap A/B and X/Y (Nintendo button positions)", &g_cfg.swap_ab_xy);
    ImGui::Spacing();
    ImGui::TextDisabled("Controllers:");
    int pads = 0;
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i)) { ImGui::BulletText("%s", SDL_GameControllerNameForIndex(i)); pads++; }
    if (!pads) ImGui::BulletText("none connected (plug one in at any time)");
    ImGui::Spacing();
    ImGui::TextDisabled("Keyboard:");
    int n = 0;
    const KeyBinding* kb = key_bindings(&n);
    if (ImGui::BeginTable("##keys", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        for (int i = 0; i < n; i++) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(kb[i].label);
            ImGui::TableNextColumn();
            auto it = g_cfg.keys.find(kb[i].id);
            const std::string key = it != g_cfg.keys.end() ? it->second : kb[i].default_key;
            if (g_capture == i) ImGui::TextColored(kGold, "press a key (Esc cancels)");
            else ImGui::TextUnformatted(key.c_str());
            ImGui::TableNextColumn();
            ImGui::PushID(i);
            if (ImGui::SmallButton("Change")) g_capture = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (ImGui::Button("Reset keyboard to defaults")) { g_cfg.keys.clear(); key_bindings_changed(); changed = true; }
    help("Arrows move, X jumps, Z shoots by default (Wii Remote held sideways). Tab: fast-forward.");
    return changed;
}

bool draw_gameplay() {
    bool changed = false;
    focus_first_option();
    {
        static const char* names[] = {"Normal", "2x", "3x", "4x", "5x", "6x", "7x", "8x"};
        int v = std::max(0, std::min(7, g_cfg.speed - 1));
        if (combo("Game speed", &v, names, 8)) { g_cfg.speed = v + 1; changed = true; }
        help("Fast-forward. Hold Tab (keyboard) or click the right stick for a burst.");
    }
    enh_checkbox("skip_strap_screen", "Skip the wrist-strap screen");
    enh_checkbox("skip_konami_logo", "Skip the Konami logo (start with the opening cinematic)");
    enh_checkbox("frame_stats", "Frame statistics in the log (diagnostics)");
    return changed;
}

bool draw_cheats() {
    bool changed = false;
    focus_first_option();
    enh_checkbox("cheat_lives", "Infinite lives");
    help("Lives never drop below 3.");
    enh_checkbox("cheat_invincible", "Invincibility");
    help("Enemies and shots cannot kill you. Pits still can: use with infinite lives.");
    {
        static const char* names[] = {"Off (what you pick up)", "Spread (S)", "Laser (L)", "Homing (H)"};
        static const char* ids[] = {nullptr, "cheat_spread", "cheat_laser", "cheat_homing"};
        int v = 0;
        for (int i = 1; i < 4; i++) if (enh_enabled(ids[i])) v = i;
        if (combo("Always this weapon", &v, names, 4))
            for (int i = 1; i < 4; i++) enh_set_enabled(ids[i], i == v);
    }
    enh_checkbox("cheat_stage", "Stage select");
    {
        static const char* names[] = {"Stage 1", "Stage 2", "Stage 3", "Stage 4", "Stage 5"};
        int v = std::max(0, std::min(4, g_cfg.start_stage - 1));
        if (combo("Start at", &v, names, 5)) { g_cfg.start_stage = v + 1; changed = true; }
        help("A new game (from the title screen) starts at this stage.");
    }
    ImGui::Spacing();
    if (ImGui::Button("All cheats off")) {
        for (const char* id : {"cheat_lives", "cheat_invincible", "cheat_spread", "cheat_laser", "cheat_homing", "cheat_stage"})
            enh_set_enabled(id, false);
    }
    return changed;
}

void draw_about() {
    ImGui::TextUnformatted(PORT_NAME " - native PC port of the WiiWare game (WKTE)");
    ImGui::TextDisabled("Static recompilation by darkuni; enhancements: github.com/8bitninja64/contra-rebirth-enhanced");
    ImGui::Spacing();
    ImGui::TextWrapped("Contra ReBirth is (c) Konami. This program contains no Konami code or data: it runs your own copy of the game.");
    ImGui::Spacing();
    ImGui::TextDisabled("Dear ImGui (MIT), SDL2 (zlib), SDL_GameControllerDB (zlib), Droid Sans fonts (Apache 2.0).");
}

void hint(const char* button, ImVec4 color, const char* action) {
    ImGui::TextColored(color, "%s", button);
    ImGui::SameLine(0, 4);
    ImGui::TextDisabled("%s", action);
    ImGui::SameLine(0, 22);
}

bool g_popup = false;
void draw_menu() {
    ImGuiIO& io = ImGui::GetIO();
    if (g_pad_pressed) ImGui::SetNavCursorVisible(true);
    if (g_pad_pressed & (OV_PAD_LB | OV_PAD_RB)) {
        g_section = (g_section + ((g_pad_pressed & OV_PAD_LB) ? kSectionCount - 1 : 1)) % kSectionCount;
        g_focus = Focus::Sections;
    }
    const bool back = (g_pad_pressed & OV_PAD_B) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false);
    if (back && !g_popup && !ImGui::IsAnyItemActive() && g_capture < 0) g_focus = Focus::Sections;

    const ImVec2 size(io.DisplaySize.x * 0.72f, io.DisplaySize.y * 0.80f);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    if (!ImGui::Begin("##settings", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }
    // Header: title over a crimson/steel stripe.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetWindowPos();
    const float stripe = ImGui::GetFontSize() / 3.0f;
    dl->AddRectFilled(o, ImVec2(o.x + size.x, o.y + stripe), ImGui::GetColorU32(kCrimson), 10, ImDrawFlags_RoundCornersTop);
    dl->AddRectFilled(ImVec2(o.x, o.y + stripe), ImVec2(o.x + size.x, o.y + stripe * 1.6f), ImGui::GetColorU32(kSteel));
    ImGui::Dummy(ImVec2(0, stripe));
    ImGui::PushFont(g_title);
    ImGui::TextColored(ImVec4(1, 1, 1, 1), PORT_NAME_UPPER);
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetTextLineHeight() * 0.35f);
    ImGui::TextDisabled("SETTINGS  -  game paused");
    ImGui::Spacing();

    const float footer = ImGui::GetFrameHeightWithSpacing() * 1.6f;
    bool changed = false;
    ImGui::BeginChild("##sections", ImVec2(size.x * 0.22f, -footer), ImGuiChildFlags_NavFlattened);
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.1f, 0.5f));
    for (int i = 0; i < kSectionCount; ++i) {
        const bool sel = g_section == i;
        if (sel) ImGui::PushStyleColor(ImGuiCol_Header, kCrimson);
        if (sel && g_focus == Focus::Sections) { ImGui::SetKeyboardFocusHere(); g_focus = Focus::None; }
        if (ImGui::Selectable(kSectionNames[i], sel, 0, ImVec2(0, ImGui::GetTextLineHeight() * 1.8f))) {
            g_section = i;
            g_focus = Focus::Content;
        }
        if (sel) ImGui::PopStyleColor();
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##content", ImVec2(0, -footer), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened);
    ImGui::PushFont(g_title);
    ImGui::TextColored(kGold, "%s", kSectionNames[g_section]);
    ImGui::PopFont();
    ImGui::Separator();
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
    switch (g_section) {
        case kDisplay: changed |= draw_display(); break;
        case kGraphics: changed |= draw_graphics(); break;
        case kAudio: changed |= draw_audio(); break;
        case kControls: changed |= draw_controls(); break;
        case kGameplay: changed |= draw_gameplay(); break;
        case kCheats: changed |= draw_cheats(); break;
        case kAbout: draw_about(); break;
    }
    ImGui::PopItemWidth();
    if (g_focus == Focus::Content) g_focus = Focus::None;
    ImGui::EndChild();
    // Footer: controller hints, Resume and Quit game.
    ImGui::Spacing();
    hint("A", kGreen, "select");
    hint("B", kCrimson, "back");
    hint("LB / RB", kSteel, "section");
    hint("Esc / F1 / Back+Start", kGold, "close");
    const ImGuiStyle& st = ImGui::GetStyle();
    auto width = [&](const char* t) { return ImGui::CalcTextSize(t).x + st.FramePadding.x * 2; };
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - width("Resume") - width("Quit game") - st.ItemSpacing.x);
    if (ImGui::Button("Resume")) set_open(false);
    ImGui::SameLine();
    if (ImGui::Button("Quit game")) ImGui::OpenPopup("Quit?");
    if (ImGui::BeginPopupModal("Quit?", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::Text("Quit Contra ReBirth?");
        ImGui::TextDisabled("Progress since the game last saved is lost.");
        ImGui::Spacing();
        if (ImGui::Button("Quit", ImVec2(width("Cancel") * 1.6f, 0))) g_actions |= OV_ACT_QUIT;
        ImGui::SameLine();
        ImGui::SetItemDefaultFocus();
        if (ImGui::Button("Cancel", ImVec2(width("Cancel") * 1.6f, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    g_popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    ImGui::End();
    if (changed) config_save();
}

void draw_fps() {
    ImGui::SetNextWindowPos(ImVec2(8, 8), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.45f);
    const ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                               ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("##fps", nullptr, f)) ImGui::TextUnformatted(g_perf_text);
    ImGui::End();
}
}  // namespace

void overlay_apply_theme() { load_fonts(); apply_style(); }
ImFont* overlay_title_font() { return g_title; }
bool overlay_open() { return g_open; }
void overlay_toggle() { set_open(!g_open); }
bool overlay_pauses_game() { return g_open; }
bool overlay_holds_input() { return g_open || g_hold; }
uint32_t overlay_take_actions() { return g_actions.exchange(0); }

void overlay_pad(uint32_t held) {
    if (!g_open && held == 0) g_hold = false;
    g_pad_held = g_open ? held : 0;
}

bool overlay_event(const SDL_Event& e) {
    if (e.type == SDL_KEYDOWN && !e.key.repeat) {
        const SDL_Keycode k = e.key.keysym.sym;
        if (g_capture >= 0) {   // rebinding: the next key
            int n = 0;
            const KeyBinding* kb = key_bindings(&n);
            const int i = g_capture.exchange(-1);
            if (k != SDLK_ESCAPE && i >= 0 && i < n) {
                g_cfg.keys[kb[i].id] = SDL_GetScancodeName(e.key.keysym.scancode);
                key_bindings_changed();
                config_save();
            }
            return true;
        }
        if (k == SDLK_ESCAPE || k == SDLK_F1) {
            if (!(g_open && g_popup)) { set_open(!g_open); return true; }
        }
    }
    if (!g_open) return false;
    {
        std::lock_guard<std::recursive_mutex> lk(g_mx);
        if (g_init) ImGui_ImplSDL2_ProcessEvent(&e);
    }
    // keyboard and mouse belong to the menu while it is open
    return e.type == SDL_KEYDOWN || e.type == SDL_KEYUP || e.type == SDL_TEXTINPUT || e.type == SDL_MOUSEMOTION ||
           e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEWHEEL;
}

void overlay_draw(SDL_Window* win, void* gl_context, int ww, int wh) {
    (void)ww; (void)wh;
    {   // tests: MENU_OPEN_AT=<presented frame> opens the menu, MENU_SECTION=<n> picks the section
        static long open_at = getenv("MENU_OPEN_AT") ? atol(getenv("MENU_OPEN_AT")) : -1;
        static long presents = 0;
        if (open_at >= 0 && ++presents == open_at) {
            if (getenv("MENU_SECTION")) g_section = atoi(getenv("MENU_SECTION")) % kSectionCount;
            set_open(true);
        }
    }
    if (!g_open && !(g_cfg.show_perf && g_perf_text[0])) return;
    std::lock_guard<std::recursive_mutex> lk(g_mx);
    if (!g_init) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NoMouseCursorChange;
        load_fonts();
        apply_style();
        ImGui_ImplSDL2_InitForOpenGL(win, gl_context);
        ImGui_ImplSDL2_SetGamepadMode(ImGui_ImplSDL2_GamepadMode_Manual, nullptr, 0);   // the menu reads pads itself
        ImGui_ImplOpenGL3_Init("#version 330");
        g_init = true;
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    float px = 18.0f * io.DisplaySize.y / 540.0f;
    px = px < 15.0f ? 15.0f : px > 72.0f ? 72.0f : px;
    io.FontGlobalScale = px / (g_body && g_body->FontSize > 20 ? kFontPx : 13.0f);
    if (g_open) feed_navigation(); else g_pad_pressed = 0;
    ImGui::NewFrame();
    if (g_cfg.show_perf && g_perf_text[0]) draw_fps();
    if (g_open) draw_menu();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
#else
// headless build: no menu
bool overlay_pauses_game() { return false; }
#endif
