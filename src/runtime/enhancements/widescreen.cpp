// True widescreen: shows a 16:9 slice of the playfield instead of the pillarboxed 4:3 one.
// Needs the game's own 16:9 mode (widescreen = 1 in the ini), which makes it compose the
// frame on an 854-wide canvas.
//
// Render side (gx_render.cpp, g_ws_on): the 320x240 playfield pass is widened to 426.67
// units around the same camera centre, the EFB copy takes the wider region and the
// composite quad fills the 16:9 canvas.
//
// Game side (here), all of it display-only (game logic never sees a changed value):
// - fn_800DFC30 copies a view's rectangle (left, top, right, bottom at view+0x80, centred
//   on the camera). Two users get it widened:
//   . the tile layer (fn_80111A70, return address 80111C94), a little past the 16:9 edge so
//     the layer's real extent can be measured;
//   . the object layers (CTPObjLayer/MObjLayer fn_8010DF80, return address 8010DFE0), to
//     exactly 16:9. Placements (enemy generators, item carriers) are created when they enter
//     this region, so they now appear at the 16:9 edge instead of the old 4:3 edge.
// - fn_8000A9B0, every stage engine's "is on screen" test (|pos - camera| <= 160x120 + margin),
//   counts the 16:9 width. Enemies and generators wait for it before acting; without it a
//   generator created at the 16:9 edge waited forever and no walkers came.
// - Tile layers are timed through fn_80111A70: each layer's drawn extent is recorded. A
//   layer that only covers the old 4:3 width (a background picture: Stage 2 sky, Stage 3
//   vortex) is stretched to the 16:9 width.
// - The 2DCameraLocator (fn_800FA260, position at +0x30) is moved sideways while it draws
//   when the level's tiles end inside the 16:9 view, so the camera stops at the map edge
//   instead of showing empty space. Rooms narrower than 16:9 stay centred.
#include "enhancements.h"
#include "config.h"
#include "hle.h"
#include <cstdlib>
#include <unordered_map>
#include <algorithm>
#include <cmath>

extern bool g_ws_on, g_ws_in_layer, g_ws_lany;
extern float g_ws_layer_stretch, g_ws_shift, g_ws_lmin, g_ws_lmax;
static bool g_active = false;
static const float kHalf = 320.f * (16.f / 9.f) / (4.f / 3.f) / 2;   // 213.33: half the 16:9 width
static const float kOld = 160.f / kHalf;                           // 0.75: the old 4:3 edge in clip units
static const float kMargin = 64.f;                                 // culling slack past the view edge

struct LayerInfo { float lo = 0, hi = 0; bool seen = false, narrow = false; int order = 0; };
static std::unordered_map<uint32_t, LayerInfo> g_layers;           // by layer object
static int g_layer_order = 0;
static const bool g_log_ws = getenv("WS_LOG") != nullptr;

static void hook_view_rect(Ctx* c, GuestFn orig) {
    const uint32_t out = c->r[3];
    const uint32_t caller = c->lr;
    orig(c);
    extern float g_dbg_view_cx;
    if (caller == 0x80111C94u) g_dbg_view_cx = (RF32(out) + RF32(out + 8)) / 2;
    if (!g_ws_on) return;
    static const char* sp = getenv("WS_SPAWN");
    static const float spawn_margin = sp ? (float)atof(sp) : 0;   // WS_SPAWN=<px> tests other spawn distances
    float half;
    if (caller == 0x80111C94u) half = kHalf + kMargin;                       // the tile layer
    else if (caller == 0x8010DFE0u && spawn_margin >= 0) half = kHalf + spawn_margin;
    else return;
    // the 320x240 playfield, wherever the camera (and our shift) put its centre
    const float l = RF32(out), t = RF32(out + 4), r = RF32(out + 8), b = RF32(out + 12);
    if (std::fabs(r - l - 320) < 0.01f && std::fabs(b - t - 240) < 0.01f) {
        const float cx = (l + r) / 2;
        WF32(out, cx - half);
        WF32(out + 8, cx + half);
    }
}

// Engine method (vtable slot 0x20 of every stage engine): true when pos (r4: x, y) lies within
// the 320x240 screen around the camera, grown by the margin in f1. Enemies and their
// generators wait for it before they start walking in or shooting, so in 16:9 they must count
// the extra width as screen, or they idle (or never start) in plain sight.
static void hook_on_screen(Ctx* c, GuestFn orig) {
    if (!g_ws_on) { orig(c); return; }
    const uint32_t engine = c->r[3], pos = c->r[4];
    const double margin = c->f[1];
    orig(c);
    if (c->r[3]) return;
    c->r[3] = engine; c->r[4] = 0;
    dispatch(c, R32(R32(engine) + 0x18));            // camera position (x, y)
    const uint32_t cam = c->r[3];
    const float dx = RF32(pos) - RF32(cam), dy = RF32(pos + 4) - RF32(cam + 4);
    c->r[3] = std::fabs(dx) <= kHalf + margin && std::fabs(dy) <= 120 + margin;
}

static void hook_tile_layer(Ctx* c, GuestFn orig) {
    if (!g_ws_on) { orig(c); return; }
    const uint32_t layer = c->r[3];
    LayerInfo& li = g_layers[layer];
    li.order = g_layer_order++;
    // backmost picture-only layer: stretch it over the 16:9 width
    g_ws_layer_stretch = (li.narrow && li.order == 0) ? 1.f / kOld : 1.f;
    g_ws_in_layer = true;
    g_ws_lany = false;
    orig(c);
    g_ws_in_layer = false;
    g_ws_layer_stretch = 1.f;
    li.seen = g_ws_lany;
    if (g_ws_lany) {
        li.lo = g_ws_lmin; li.hi = g_ws_lmax;
        // covers the old 4:3 width and nothing more, although culling allowed more
        li.narrow = li.hi <= kOld + 0.03f && li.lo >= -kOld - 0.03f && li.hi - li.lo >= 2 * kOld - 0.1f;
    }
}

static void hook_camera(Ctx* c, GuestFn orig) {
    const uint32_t loc = c->r[3];
    if (!g_ws_on || g_ws_shift == 0) { orig(c); return; }
    const float x = RF32(loc + 0x30), scale = RF32(loc + 0x40);
    WF32(loc + 0x30, x + g_ws_shift * kHalf / (scale > 0.01f ? scale : 1.f));
    orig(c);
    WF32(loc + 0x30, x);
}

// once per game frame: decide next frame's camera shift from this frame's layer extents
static void frame() {
    g_ws_on = g_active && g_cfg.widescreen;
    if (!g_ws_on) { g_ws_shift = 0; g_layers.clear(); g_layer_order = 0; return; }
    float lo = 1e9f, hi = -1e9f;
    for (auto& [id, li] : g_layers)
        if (li.seen && !li.narrow) { lo = std::min(lo, li.lo); hi = std::max(hi, li.hi); }
    // Keep the view [-1+s, 1+s] inside the content [lo, hi]: s in [lo+1, hi-1], closest to 0.
    // Content reaching the culling limit goes on past it. Content narrower than the view
    // (a 4:3 room) stays centred.
    float target = 0;
    if (hi > lo) {
        const float lim = 1.f + kMargin / kHalf - 0.03f;
        const bool lo_open = lo <= -lim, hi_open = hi >= lim;
        if (lo_open && !hi_open) target = std::min(0.f, hi - 1.f);
        else if (hi_open && !lo_open) target = std::max(0.f, lo + 1.f);
        else if (!lo_open && !hi_open) target = hi - lo >= 2.f ? std::max(lo + 1.f, std::min(hi - 1.f, 0.f)) : (lo + hi) / 2;
    }
    target = std::max(-1.f + kOld, std::min(1.f - kOld, target));   // never more than the extra width
    // The camera shift is experimental (WS_SHIFT=1): some layers follow their own camera, so
    // moving the locator split the scene, and a shifted 4:3 background no longer measured as
    // narrow (lost its stretch). The background stretch already fills the 4:3 arenas.
    static const bool use_shift = getenv("WS_SHIFT") != nullptr;
    if (!use_shift) target = 0;
    const float step = 0.04f;                                       // ~8 px per frame
    g_ws_shift += std::max(-step, std::min(step, target - g_ws_shift));
    if (std::fabs(g_ws_shift) < 1e-4f) g_ws_shift = 0;
    static int n = 0;
    if (g_log_ws && ++n % 120 == 0) {
        fprintf(g_log, "[ws] shift %.3f target %.3f content %.2f..%.2f layers:", g_ws_shift, target, lo, hi);
        for (auto& [id, li] : g_layers)
            if (li.seen) fprintf(g_log, " #%d %08X %.2f..%.2f%s", li.order, id, li.lo, li.hi, li.narrow ? " narrow" : "");
        fputc('\n', g_log);
    }
    for (auto it = g_layers.begin(); it != g_layers.end();)   // forget layers not drawn this frame
        if (!it->second.seen) it = g_layers.erase(it); else { it->second.seen = false; ++it; }
    g_layer_order = 0;
}

static bool init() {
    enh_on_frame(frame);
    return enh_hook("fn_800DFC30", hook_view_rect) && enh_hook("fn_80111A70", hook_tile_layer) &&
           enh_hook("fn_800FA260", hook_camera) &&
           enh_hook("fn_8000A9B0", hook_on_screen);
}

static const Enhancement widescreen = {
    "widescreen", "True widescreen", "16:9 playfield instead of 4:3 with bars (needs Aspect 16:9)", false, init, &g_active};
REGISTER_ENHANCEMENT(widescreen);
