// F1 settings menu (Dear ImGui), modeled on the Blitz 2003 / Tenchu Z ports' menus.
// Threads: the UI thread feeds window events and pad state, the present thread draws.
#pragma once
#include <cstdint>
union SDL_Event;
struct SDL_Window;

bool overlay_open();
void overlay_toggle();
bool overlay_pauses_game();          // the game stands still while the menu is open
bool overlay_holds_input();          // open, or closed but the buttons that closed it are still held

// UI thread
bool overlay_event(const SDL_Event& e);   // true: the menu consumed it (keyboard/mouse while open)
void overlay_pad(uint32_t held);          // OV_PAD_* bits of all controllers, sampled every few ms
enum : uint32_t { OV_PAD_UP = 1, OV_PAD_DOWN = 2, OV_PAD_LEFT = 4, OV_PAD_RIGHT = 8, OV_PAD_A = 16, OV_PAD_B = 32,
                  OV_PAD_LB = 64, OV_PAD_RB = 128, OV_PAD_START = 256, OV_PAD_BACK = 512 };
// Actions the UI thread carries out for the menu (window changes must happen there).
enum : uint32_t { OV_ACT_DISPLAY = 1, OV_ACT_QUIT = 2 };
uint32_t overlay_take_actions();

// Present thread, GL context current
void overlay_draw(SDL_Window* win, void* gl_context, int ww, int wh);

// For the controls page (gl_platform_sdl.cpp owns the bindings)
struct KeyBinding { const char* id; const char* label; uint32_t bit; const char* default_key; };
const KeyBinding* key_bindings(int* count);
void key_bindings_changed();
extern char g_perf_text[128];
extern bool g_vsync_dirty_from_menu;
