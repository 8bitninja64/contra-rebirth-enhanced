// Windowed platform layer (Windows, or Linux with -DUSE_SDL): SDL2 window with an
// OpenGL 3.3 core context, audio output and keyboard/gamepad input.
#if defined(_WIN32) || defined(USE_SDL)
#include "game.h"
#ifdef _WIN32
#include "glad.h"
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include "gx_render.h"
#include "config.h"
#include "hostinput.h"
#include "overlay.h"
#include "audio_ring.h"
#include <atomic>
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <vector>

static SDL_Window* g_win = nullptr;
static SDL_GLContext g_glc = nullptr;
static SDL_AudioDeviceID g_audio = 0;
static std::vector<SDL_GameController*> g_pads;   // every connected controller; their inputs are merged

// Open joystick `index` as a game controller. Pads SDL has no mapping for (not in its built-in
// list or gamecontrollerdb.txt) get a generic layout: hat = d-pad, axes 0/1 = left stick,
// 2/3 = right stick, buttons 0-3 = face buttons, 4/5 = shoulders, 6/7 = back/start.
static void open_pad(int index) {
    SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(index);
    for (auto* p : g_pads)
        if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(p)) == id) return;   // already open
    if (!SDL_IsGameController(index)) {
        char guid[64], map[512];
        SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(index), guid, sizeof guid);
        snprintf(map, sizeof map, "%s,%s,a:b0,b:b1,x:b2,y:b3,leftshoulder:b4,rightshoulder:b5,back:b6,start:b7,"
                 "leftx:a0,lefty:a1,rightx:a2,righty:a3,dpup:h0.1,dpdown:h0.4,dpleft:h0.8,dpright:h0.2",
                 guid, SDL_JoystickNameForIndex(index) ? SDL_JoystickNameForIndex(index) : "Gamepad");
        SDL_GameControllerAddMapping(map);
        fprintf(stderr, "[input] no mapping for %s (%s): using a generic layout; add it to gamecontrollerdb.txt\n",
                SDL_JoystickNameForIndex(index), guid);
    }
    if (SDL_GameController* p = SDL_GameControllerOpen(index)) {
        g_pads.push_back(p);
        fprintf(stderr, "[input] controller: %s\n", SDL_GameControllerName(p));
    }
}
static GLuint g_present_fbo = 0;
// Presentation runs on its own thread with a second (shared) GL context, so waiting for
// vsync never blocks the emulated CPU (which must keep servicing 3 ms audio interrupts).
static SDL_GLContext g_present_ctx = nullptr;
struct Slot { GLuint tex = 0; int w = 0, h = 0; GLsync fence = nullptr; };
static Slot g_slots[3];
static int g_ready = -1, g_showing = -1, g_last_shown = -1;
static std::mutex g_pm;
static std::condition_variable g_pcv;
static void present_thread();
static thread_local bool t_current = false;

// Presentation settings as plain ints for the present thread (the config strings are written
// on the emulation thread).
enum { SCALE_FIT, SCALE_INTEGER, SCALE_STRETCH, SCALE_STRETCH169 };
enum { FILTER_SMOOTH, FILTER_SHARP, FILTER_NEAREST };
static std::atomic<int> g_scale_mode{SCALE_FIT}, g_filter_mode{FILTER_SMOOTH};
static void update_present_modes() {
    g_scale_mode = g_cfg.scaling == "integer" ? SCALE_INTEGER : g_cfg.scaling == "stretch" ? SCALE_STRETCH
                 : g_cfg.scaling == "stretch169" ? SCALE_STRETCH169 : SCALE_FIT;
    g_filter_mode = g_cfg.filter == "sharp" ? FILTER_SHARP : g_cfg.filter == "nearest" ? FILTER_NEAREST : FILTER_SMOOTH;
}

static bool parse_resolution(int* w, int* h) {
    int rw, rh;
    if (g_cfg.resolution == "native") {
        SDL_DisplayMode m;
        if (SDL_GetDesktopDisplayMode(g_win ? std::max(0, SDL_GetWindowDisplayIndex(g_win)) : 0, &m) != 0) return false;
        *w = m.w; *h = m.h;
        return true;
    }
    if (sscanf(g_cfg.resolution.c_str(), "%dx%d", &rw, &rh) != 2 || rw < 320 || rh < 240) return false;
    *w = rw; *h = rh;
    return true;
}

// Windowed / borderless (desktop resolution) / exclusive fullscreen at the configured resolution.
static void apply_display() {
    update_present_modes();
    if (!g_cfg.fullscreen) {
        SDL_SetWindowFullscreen(g_win, 0);
        int w, h;
        if (parse_resolution(&w, &h)) SDL_SetWindowSize(g_win, w, h);
        return;
    }
    if (g_cfg.fullscreen_mode == "exclusive") {
        int di = std::max(0, SDL_GetWindowDisplayIndex(g_win));
        SDL_DisplayMode want = {}, got = {};
        SDL_GetDesktopDisplayMode(di, &want);
        parse_resolution(&want.w, &want.h);
        if (SDL_GetClosestDisplayMode(di, &want, &got)) SDL_SetWindowDisplayMode(g_win, &got);
        if (SDL_SetWindowFullscreen(g_win, SDL_WINDOW_FULLSCREEN) == 0) {
            fprintf(stderr, "[sdl] exclusive fullscreen %dx%d @ %d Hz\n", got.w, got.h, got.refresh_rate);
            return;
        }
        fprintf(stderr, "[sdl] exclusive fullscreen failed (%s), using borderless\n", SDL_GetError());
    }
    SDL_SetWindowFullscreen(g_win, SDL_WINDOW_FULLSCREEN_DESKTOP);
}

// The window belongs to a UI thread that pumps its events and samples input. Pumping on the
// emulation thread (once per presented frame) blocked for 18-35 ms now and then while the
// present thread swapped buffers, and the game missed one or two retraces each time
// (measured with the frame_stats enhancement).
static std::mutex g_ui_init_mx;
static std::condition_variable g_ui_init_cv;
static int g_ui_state = 0;   // 0 starting, 1 ready, -1 failed
static void ui_thread();
static void update_input();

bool gl_platform_init(int w, int h) {
    std::thread(ui_thread).detach();
    {
        std::unique_lock<std::mutex> lk(g_ui_init_mx);
        g_ui_init_cv.wait(lk, [] { return g_ui_state != 0; });
    }
    if (g_ui_state < 0) return false;
    SDL_GL_MakeCurrent(g_win, g_glc);
    t_current = true;
#ifdef _WIN32
    if (!gl_load(SDL_GL_GetProcAddress)) return false;
#endif
    SDL_GL_SetSwapInterval(0);
    std::thread(present_thread).detach();
    fprintf(stderr, "[gl] %s | %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    (void)w; (void)h;
    return true;
}

// UI thread: SDL, window and both GL contexts (released again; the emulation and present
// threads make them current), audio and controllers.
static bool ui_setup() {
    SDL_SetHint(SDL_HINT_JOYSTICK_THREAD, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "[sdl] init failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    int scale = g_cfg.window_scale;
    int ww = (g_cfg.widescreen || g_cfg.scaling == "stretch169" ? 854 : 640) * scale, wh = 480 * scale;
    if (!parse_resolution(&ww, &wh)) {
        // automatic size: keep the window inside the desktop's usable area (scaled laptop
        // screens are often smaller than 1708x960), so it never looks like fullscreen
        SDL_Rect r;
        if (SDL_GetDisplayUsableBounds(0, &r) == 0 && r.w > 0 && r.h > 0) {
            const float f = std::min(0.85f * r.w / ww, 0.85f * (r.h - 40) / wh);
            if (f < 1) { ww = (int)(ww * f); wh = (int)(wh * f); }
        }
    }
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    g_win = SDL_CreateWindow(PORT_NAME, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             ww, wh, flags);
    if (!g_win) { fprintf(stderr, "[sdl] window failed: %s\n", SDL_GetError()); return false; }
    apply_display();
    g_present_ctx = SDL_GL_CreateContext(g_win);              // presenter
    if (!g_present_ctx) { fprintf(stderr, "[sdl] GL context failed: %s\n", SDL_GetError()); return false; }
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
    g_glc = SDL_GL_CreateContext(g_win);                      // renderer (shares textures/syncs)
    if (!g_glc) { fprintf(stderr, "[sdl] GL context failed: %s\n", SDL_GetError()); return false; }
    SDL_GL_MakeCurrent(g_win, nullptr);                       // both contexts are used on other threads
    // audio: 32 kHz stereo s16, fed from the AX output
    SDL_AudioSpec want = {}, have = {};
    want.freq = 32000; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 512;
    want.callback = [](void*, Uint8* stream, int len) {
        int16_t* s = (int16_t*)stream;
        audio_ring_pull(s, len / 4);
        const int vol = g_cfg.mute ? 0 : g_cfg.volume;   // F1 > Audio
        if (vol < 100) for (int i = 0; i < len / 2; i++) s[i] = (int16_t)(s[i] * vol / 100);
    };
    g_audio = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (g_audio) SDL_PauseAudioDevice(g_audio, 0);
    // community controller mappings (SDL_GameControllerDB) next to the executable, if present
    if (char* base = SDL_GetBasePath()) {
        std::string db = std::string(base) + "gamecontrollerdb.txt";
        SDL_free(base);
        int n = SDL_GameControllerAddMappingsFromFile(db.c_str());
        if (n > 0) fprintf(stderr, "[input] %d controller mappings from %s\n", n, db.c_str());
    }
    for (int i = 0; i < SDL_NumJoysticks(); i++) open_pad(i);
    if (g_pads.empty()) fprintf(stderr, "[input] no controller found (keyboard active; controllers can be plugged in any time)\n");
    return true;
}

static void handle_event(const SDL_Event& e);
static void apply_actions();
static void ui_thread() {
    bool ok = ui_setup();
    {
        std::lock_guard<std::mutex> g(g_ui_init_mx);
        g_ui_state = ok ? 1 : -1;
    }
    g_ui_init_cv.notify_all();
    if (!ok) return;
    for (;;) {
        SDL_Event e;
        if (SDL_WaitEventTimeout(&e, 4)) {
            handle_event(e);
            while (SDL_PollEvent(&e)) handle_event(e);
        }
        update_input();
        apply_actions();   // at least every 4 ms, so a new press is seen within the game's next frame
    }
}

void gl_platform_make_current() {
    if (t_current) return;
    SDL_GL_MakeCurrent(g_win, g_glc);
    t_current = true;
}

void platform_audio_push(const int16_t* lr, int frames) {
    if (g_audio) audio_ring_push(lr, frames);
}

// Keyboard bindings (F1 > Controls; key_<id> = <SDL key name> in the .ini)
static const KeyBinding kKeys[] = {
    {"up", "Up", IN_UP, "Up"},                {"down", "Down", IN_DOWN, "Down"},
    {"left", "Left", IN_LEFT, "Left"},        {"right", "Right", IN_RIGHT, "Right"},
    {"jump", "Jump (2)", IN_SOUTH, "X"},      {"shoot", "Shoot (1)", IN_EAST, "Z"},
    {"a", "A button", IN_WEST, "A"},          {"b", "B button", IN_NORTH, "S"},
    {"start", "Start (+)", IN_START, "Return"}, {"select", "Select (-)", IN_BACK, "Backspace"},
    {"home", "Home", IN_GUIDE, "H"},          {"l", "L shoulder", IN_L1, "Q"},
    {"r", "R shoulder", IN_R1, "W"},          {"zl", "ZL", IN_L2, "1"},
    {"zr", "ZR", IN_R2, "2"},                 {"turbo", "Fast-forward (hold)", IN_TURBO, "Tab"},
};
static const int kKeyCount = (int)(sizeof kKeys / sizeof kKeys[0]);
static SDL_Scancode g_key_sc[kKeyCount];
static std::atomic<bool> g_keys_dirty{true};
const KeyBinding* key_bindings(int* count) { *count = kKeyCount; return kKeys; }
void key_bindings_changed() { g_keys_dirty = true; }
static void refresh_key_bindings() {
    if (!g_keys_dirty.exchange(false)) return;
    for (int i = 0; i < kKeyCount; i++) {
        auto it = g_cfg.keys.find(kKeys[i].id);
        g_key_sc[i] = SDL_GetScancodeFromName(it != g_cfg.keys.end() ? it->second.c_str() : kKeys[i].default_key);
    }
}

static void update_input() {
    const Uint8* k = SDL_GetKeyboardState(nullptr);
    uint32_t b = 0;
    float lx = 0, ly = 0;
    refresh_key_bindings();
    for (int i = 0; i < kKeyCount; i++)
        if (g_key_sc[i] != SDL_SCANCODE_UNKNOWN && k[g_key_sc[i]]) b |= kKeys[i].bit;
    if (b & (IN_UP | IN_DOWN | IN_LEFT | IN_RIGHT)) b |= IN_KB_ARROWS;
    if (k[SDL_SCANCODE_I]) b |= IN_ALT_UP;      // right-stick stand-in, not rebindable
    if (k[SDL_SCANCODE_K]) b |= IN_ALT_DOWN;
    if (k[SDL_SCANCODE_J]) b |= IN_ALT_LEFT;
    if (k[SDL_SCANCODE_L]) b |= IN_ALT_RIGHT;
    float rx = 0, ry = 0, lt = 0, rt = 0;
    if (k[SDL_SCANCODE_J]) rx = -1; if (k[SDL_SCANCODE_L]) rx = 1;
    if (k[SDL_SCANCODE_I]) ry = 1; if (k[SDL_SCANCODE_K]) ry = -1;
    for (SDL_GameController* g_pad : g_pads) {
        auto btn = [&](SDL_GameControllerButton x) { return SDL_GameControllerGetButton(g_pad, x) != 0; };
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_UP)) b |= IN_UP;
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN)) b |= IN_DOWN;
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT)) b |= IN_LEFT;
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) b |= IN_RIGHT;
        const bool sw = g_cfg.swap_ab_xy;
        if (btn(SDL_CONTROLLER_BUTTON_A)) b |= sw ? IN_EAST : IN_SOUTH;
        if (btn(SDL_CONTROLLER_BUTTON_B)) b |= sw ? IN_SOUTH : IN_EAST;
        if (btn(SDL_CONTROLLER_BUTTON_X)) b |= sw ? IN_NORTH : IN_WEST;
        if (btn(SDL_CONTROLLER_BUTTON_Y)) b |= sw ? IN_WEST : IN_NORTH;
        if (btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) b |= IN_L1;
        if (btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) b |= IN_R1;
        if (SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000) b |= IN_L2;
        if (SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000) b |= IN_R2;
        if (btn(SDL_CONTROLLER_BUTTON_START)) b |= IN_START;
        if (btn(SDL_CONTROLLER_BUTTON_BACK)) b |= IN_BACK;
        if (btn(SDL_CONTROLLER_BUTTON_GUIDE)) b |= IN_GUIDE;
        if (btn(SDL_CONTROLLER_BUTTON_RIGHTSTICK)) b |= IN_TURBO;
        auto axis = [&](SDL_GameControllerAxis a) {
            float v = SDL_GameControllerGetAxis(g_pad, a) / 32767.f;
            const float dz = 0.2f;   // dead zone
            if (std::fabs(v) < dz) return 0.f;
            return (v > 0 ? v - dz : v + dz) / (1 - dz);
        };
        float sx = axis(SDL_CONTROLLER_AXIS_LEFTX), sy = -axis(SDL_CONTROLLER_AXIS_LEFTY);
        if (sx || sy) { lx = sx; ly = sy; }
        float px = axis(SDL_CONTROLLER_AXIS_RIGHTX), py = -axis(SDL_CONTROLLER_AXIS_RIGHTY);
        if (px || py) { rx = px; ry = py; }
        lt = std::max(lt, SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) / 32767.f);
        rt = std::max(rt, SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) / 32767.f);
    }
    if (b & IN_L2) lt = 1;
    if (b & IN_R2) rt = 1;
    // the menu reads all pads and keys; it owns them while open and until they are released
    {
        uint32_t nav = 0;
        for (SDL_GameController* pad : g_pads) {
            auto btn = [&](SDL_GameControllerButton x) { return SDL_GameControllerGetButton(pad, x) != 0; };
            const int ax = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX), ay = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
            if (btn(SDL_CONTROLLER_BUTTON_DPAD_UP) || ay < -16000) nav |= OV_PAD_UP;
            if (btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN) || ay > 16000) nav |= OV_PAD_DOWN;
            if (btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT) || ax < -16000) nav |= OV_PAD_LEFT;
            if (btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || ax > 16000) nav |= OV_PAD_RIGHT;
            const bool sw = g_cfg.swap_ab_xy;
            if (btn(sw ? SDL_CONTROLLER_BUTTON_B : SDL_CONTROLLER_BUTTON_A)) nav |= OV_PAD_A;
            if (btn(sw ? SDL_CONTROLLER_BUTTON_A : SDL_CONTROLLER_BUTTON_B)) nav |= OV_PAD_B;
            if (btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) nav |= OV_PAD_LB;
            if (btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) nav |= OV_PAD_RB;
            if (btn(SDL_CONTROLLER_BUTTON_START)) nav |= OV_PAD_START;
            if (btn(SDL_CONTROLLER_BUTTON_BACK)) nav |= OV_PAD_BACK;
        }
        if (b || lx || ly || rx || ry) nav |= 0x80000000u;   // any game control still held
        overlay_pad(nav);
    }
    if (overlay_holds_input()) { b = 0; lx = ly = rx = ry = lt = rt = 0; }
    static const bool no_host_input = getenv("NO_HOST_INPUT") != nullptr;   // scripted tests: AUTOPRESS only
    if (no_host_input) { b = 0; lx = ly = rx = ry = lt = rt = 0; }
    g_host_input.rx = std::max(-1.f, std::min(1.f, rx));
    g_host_input.ry = std::max(-1.f, std::min(1.f, ry));
    g_host_input.lt = std::max(0.f, std::min(1.f, lt));
    g_host_input.rt = std::max(0.f, std::min(1.f, rt));
    g_host_input.buttons = b;
    g_host_input.lx = std::max(-1.f, std::min(1.f, lx));
    g_host_input.ly = std::max(-1.f, std::min(1.f, ly));
}

static std::atomic<bool> g_vsync_dirty{false};

static void apply_actions() {
    const uint32_t a = overlay_take_actions();
    if (a & OV_ACT_DISPLAY) apply_display();
    if (a & OV_ACT_QUIT) exit(0);
}

bool gl_platform_poll() { return true; }   // events are pumped on the UI thread

static void handle_event(const SDL_Event& e) {
    {
        if (e.type == SDL_QUIT) exit(0);
        if (overlay_event(e)) return;   // Esc / F1, key capture, and everything while the menu is open
        if (e.type == SDL_KEYDOWN) {
            SDL_Keycode k = e.key.keysym.sym;
            if (k == SDLK_F11 || (k == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT))) {
                g_cfg.fullscreen = !g_cfg.fullscreen;
                config_save();
                apply_display();
            }
        }
        if (e.type == SDL_CONTROLLERBUTTONDOWN) {   // Back + Start opens / closes the menu
            SDL_GameController* pad = SDL_GameControllerFromInstanceID(e.cbutton.which);
            const int btn = e.cbutton.button;
            if (pad && ((btn == SDL_CONTROLLER_BUTTON_START && SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_BACK)) ||
                        (btn == SDL_CONTROLLER_BUTTON_BACK && SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START))))
                overlay_toggle();
        }
        // hot-plug: JOYDEVICEADDED also covers pads without a mapping (no CONTROLLERDEVICEADDED for those)
        if (e.type == SDL_JOYDEVICEADDED) open_pad(e.jdevice.which);
        if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
            for (size_t i = 0; i < g_pads.size(); i++)
                if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_pads[i])) == e.cdevice.which) {
                    fprintf(stderr, "[input] controller removed: %s\n", SDL_GameControllerName(g_pads[i]));
                    SDL_GameControllerClose(g_pads[i]);
                    g_pads.erase(g_pads.begin() + i);
                    break;
                }
        }
    }
}

// Called on the emulation thread with the finished frame (top-down texture rows).
uint64_t g_ns_handoff = 0, g_ns_poll = 0;   // host time in the frame hand-off / event poll (frame_stats)
static uint64_t ns_now() { return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
void gl_platform_present(unsigned tex, int w, int h) {
    const uint64_t t_start = ns_now();
    if (!g_present_fbo) glGenFramebuffers(1, &g_present_fbo);
    static GLuint dst_fbo = 0;
    if (!dst_fbo) glGenFramebuffers(1, &dst_fbo);
    int s;
    {
        std::lock_guard<std::mutex> g(g_pm);
        for (s = 0; s < 3; s++) if (s != g_ready && s != g_showing) break;
    }
    Slot& sl = g_slots[s];
    if (!sl.tex || sl.w != w || sl.h != h) {
        if (!sl.tex) glGenTextures(1, &sl.tex);
        glBindTexture(GL_TEXTURE_2D, sl.tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        sl.w = w; sl.h = h;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, g_present_fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst_fbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sl.tex, 0);
    glDisable(GL_SCISSOR_TEST);
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    GLsync f = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glFlush();
    {
        std::lock_guard<std::mutex> g(g_pm);
        if (g_ready >= 0 && g_slots[g_ready].fence) { glDeleteSync(g_slots[g_ready].fence); g_slots[g_ready].fence = nullptr; }
        sl.fence = f;
        g_ready = s;
    }
    g_pcv.notify_one();
    const uint64_t t_poll = ns_now();
    g_ns_handoff += t_poll - t_start;
    gl_platform_poll();
    g_ns_poll += ns_now() - t_poll;
}

// Final scaling of the frame to the window. Downscaling (internal resolution above the
// window size) averages a grid of bilinear taps across each window pixel's footprint
// so the extra resolution becomes anti-aliasing instead of shimmer; upscaling is bilinear.
// "sharp" keeps texels square-edged and only blends across the one output pixel that
// straddles two texels (sharp bilinear); "nearest" reads raw texels.
static GLuint g_scale_prog = 0, g_scale_vao = 0;
static GLint g_u_src = -1, g_u_scale = -1, g_u_rect = -1, g_u_filter = -1;
static void init_scaler() {
    const char* vs = "#version 330 core\nuniform vec4 u_rect; out vec2 uv;\n"
        "void main(){ vec2 p = vec2(gl_VertexID & 1, gl_VertexID >> 1); uv = vec2(p.x, p.y);\n"
        " vec2 q = u_rect.xy + p * u_rect.zw; gl_Position = vec4(q.x * 2.0 - 1.0, 1.0 - q.y * 2.0, 0.0, 1.0); }\n";
    const char* fs = "#version 330 core\nin vec2 uv; uniform sampler2D u_src; uniform vec2 u_scale; uniform int u_filter; out vec4 o;\n"
        "void main(){\n"
        " vec2 ts = vec2(textureSize(u_src, 0));\n"
        " if (u_filter == 2) { o = vec4(texelFetch(u_src, ivec2(min(uv * ts, ts - 1.0)), 0).rgb, 1.0); return; }\n"
        " if (u_filter == 1 && u_scale.x <= 1.0 && u_scale.y <= 1.0) {\n"
        "  vec2 pre = max(floor(1.0 / u_scale), vec2(1.0));\n"         // whole output pixels per texel
        "  vec2 t = uv * ts, c = fract(t) - 0.5, r = 0.5 - 0.5 / pre;\n"
        "  vec2 f = (c - clamp(c, -r, r)) * pre + 0.5;\n"
        "  o = vec4(texture(u_src, (floor(t) + f) / ts).rgb, 1.0); return;\n"
        " }\n"
        " vec2 n = clamp(ceil(u_scale), vec2(1.0), vec2(4.0));\n"   // taps per axis
        " vec2 fp = u_scale / ts;\n"                                  // footprint in uv
        " vec4 acc = vec4(0.0);\n"
        " for (int j = 0; j < 4; j++) for (int i = 0; i < 4; i++) {\n"
        "  if (float(i) >= n.x || float(j) >= n.y) continue;\n"
        "  vec2 off = (vec2(i, j) + 0.5) / n - 0.5;\n"
        "  acc += texture(u_src, uv + off * fp);\n"
        " }\n"
        " o = vec4(acc.rgb / (n.x * n.y), 1.0); }\n";
    auto sh = [](GLenum t, const char* src) { GLuint s = glCreateShader(t); glShaderSource(s, 1, &src, nullptr); glCompileShader(s); return s; };
    g_scale_prog = glCreateProgram();
    glAttachShader(g_scale_prog, sh(GL_VERTEX_SHADER, vs));
    glAttachShader(g_scale_prog, sh(GL_FRAGMENT_SHADER, fs));
    glLinkProgram(g_scale_prog);
    g_u_src = glGetUniformLocation(g_scale_prog, "u_src");
    g_u_scale = glGetUniformLocation(g_scale_prog, "u_scale");
    g_u_rect = glGetUniformLocation(g_scale_prog, "u_rect");
    g_u_filter = glGetUniformLocation(g_scale_prog, "u_filter");
    glGenVertexArrays(1, &g_scale_vao);
}

static void present_thread() {
    SDL_GL_MakeCurrent(g_win, g_present_ctx);
    if (g_cfg.vsync) { if (SDL_GL_SetSwapInterval(-1) != 0) SDL_GL_SetSwapInterval(1); }
    else SDL_GL_SetSwapInterval(0);
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    for (;;) {
        int s;
        GLsync f;
        {
            std::unique_lock<std::mutex> lk(g_pm);
            g_pcv.wait_for(lk, std::chrono::milliseconds(16), [] { return g_ready >= 0; });
            if (g_ready >= 0) {
                s = g_ready; g_ready = -1; g_showing = s; g_last_shown = s;
                f = g_slots[s].fence; g_slots[s].fence = nullptr;
            } else if (overlay_open() && g_last_shown >= 0 && g_slots[g_last_shown].tex) {
                s = g_last_shown; g_showing = s; f = nullptr;   // paused under the F1 menu
            } else continue;
        }
        if (f) { glWaitSync(f, 0, GL_TIMEOUT_IGNORED); glDeleteSync(f); }
        Slot& sl = g_slots[s];
        int ww, wh;
        SDL_GL_GetDrawableSize(g_win, &ww, &wh);
        // picture rectangle: the game's aspect (4:3, or 16:9 when it is told the TV is widescreen)
        const int an = g_cfg.widescreen ? 16 : 4, ad = g_cfg.widescreen ? 9 : 3;
        const int smode = g_scale_mode.load();
        int dw = ww, dh = ww * ad / an;
        if (dh > wh) { dh = wh; dw = wh * an / ad; }
        if (smode == SCALE_INTEGER && wh >= 240) {
            // whole multiples of the 240-line playfield, so every source line is the same height
            dh = std::min(wh, (wh / 240) * 240);
            dw = dh * an / ad;
            while (dw > ww && dh > 240) { dh -= 240; dw = dh * an / ad; }
        } else if (smode == SCALE_STRETCH) { dw = ww; dh = wh; }
        else if (smode == SCALE_STRETCH169) {   // the picture stretched to a 16:9 area, centred
            dw = ww; dh = ww * 9 / 16;
            if (dh > wh) { dh = wh; dw = wh * 16 / 9; }
        }
        int dx = (ww - dw) / 2, dy = (wh - dh) / 2;
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sl.tex, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glViewport(0, 0, ww, wh);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (!g_scale_prog) init_scaler();
        glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
        glUseProgram(g_scale_prog);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sl.tex);
        glUniform1i(g_u_src, 0);
        glUniform2f(g_u_scale, (float)sl.w / dw, (float)sl.h / dh);
        glUniform1i(g_u_filter, g_filter_mode.load());
        glUniform4f(g_u_rect, (float)dx / ww, (float)dy / wh, (float)dw / ww, (float)dh / wh);
        glBindVertexArray(g_scale_vao);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        overlay_draw(g_win, g_present_ctx, ww, wh);
        // debug: PRESENT_SHOT=<file.ppm> PRESENT_SHOT_AT=<n> saves the n-th presented window image
        static const char* pshot = getenv("PRESENT_SHOT");
        static long pshot_at = getenv("PRESENT_SHOT_AT") ? atol(getenv("PRESENT_SHOT_AT")) : 600;
        static long presented = 0;
        if (pshot && ++presented == pshot_at) {
            std::vector<uint8_t> px((size_t)ww * wh * 3);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, ww, wh, GL_RGB, GL_UNSIGNED_BYTE, px.data());
            if (FILE* f = fopen(pshot, "wb")) {
                fprintf(f, "P6\n%d %d\n255\n", ww, wh);
                for (int y = wh - 1; y >= 0; y--) fwrite(&px[(size_t)y * ww * 3], 1, (size_t)ww * 3, f);
                fclose(f);
                fprintf(stderr, "[sdl] window %dx%d saved to %s\n", ww, wh, pshot);
            }
        }
        if (g_vsync_dirty.exchange(false) || g_vsync_dirty_from_menu) {
            g_vsync_dirty_from_menu = false;
            if (g_cfg.vsync) { if (SDL_GL_SetSwapInterval(-1) != 0) SDL_GL_SetSwapInterval(1); }
            else SDL_GL_SetSwapInterval(0);
        }
        SDL_GL_SwapWindow(g_win);
        { std::lock_guard<std::mutex> g(g_pm); g_showing = -1; }
    }
}
#endif
