// True widescreen: shows a 16:9 slice of the playfield instead of the pillarboxed 4:3 one.
// Needs the game's own 16:9 mode (widescreen = 1 in the ini), which makes it compose the
// frame on an 854-wide canvas.
//
// Render side (gx_render.cpp, g_ws_on): the 320x240 playfield pass is widened to 426.67
// units plus a 53.33 pad on each side around the same camera centre, the EFB copy takes the
// wider region and the composite quad fills the 16:9 canvas with a window of it. The window
// is centred unless g_ws_view (decided here) slides it toward the level art at a map edge.
//
// Game side (here), all of it display-only (game logic never sees a changed value):
// - fn_800DFC30 copies a view's rectangle (left, top, right, bottom at view+0x80, centred
//   on the camera). Two users get it widened:
//   . the tile layer (fn_80111A70, return address 80111C94), a little past the 16:9 edge so
//     the layer's real extent can be measured;
//   . the object layers (CTPObjLayer/MObjLayer fn_8010DF80, return address 8010DFE0), to
//     exactly the presented 16:9 window. Placements (enemy generators, item carriers) are
//     created when they enter this region, so they appear at the 16:9 edge instead of the old
//     4:3 edge, and sprites are drawn across it.
// - fn_8000A9B0, every stage engine's "is on screen" test (|pos - camera| <= 160x120 + margin),
//   counts the presented 16:9 window. Enemies and generators wait for it before acting;
//   without it a generator created at the 16:9 edge waited forever and no walkers came.
// - Tile layers are timed through fn_80111A70: each layer's drawn extent is recorded. A
//   layer that only covers the old 4:3 width (a background picture: Stage 2 sky, Stage 3
//   vortex) is stretched to the 16:9 width.
// - Map edges (the view clamp of the Hangtime port): the game stops its camera where the 4:3
//   view meets the end of the map, so the level's layers then end exactly on the old 4:3 edge
//   and the 16:9 view would show what lies past it. When a layer has ended there for a few
//   frames while reaching past the other side, the presented window slides toward the art by
//   up to 53.33 units (g_ws_view). The game's camera, positions and scrolling are untouched;
//   only the picture, the spawn region and the on-screen test follow the window.
// - Rooms that are only 4:3 wide (every layer ends on the old 4:3 edges, e.g. the Stage 4
//   skull room and descent shaft) have no art to show at the sides. There the playfield copy
//   fills them from the nearest 4:3 edge column, softened and darkened outward
//   (g_ws_fill43, gx_render.cpp).
#include "enhancements.h"
#include "config.h"
#include "hle.h"
#include <cstdlib>
#include <unordered_map>
#include <algorithm>
#include <cmath>

extern bool g_ws_on, g_ws_in_layer, g_ws_lany, g_ws_fill43;
extern float g_ws_layer_stretch, g_ws_view, g_ws_lmin, g_ws_lmax;
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
    float half, shift = 0;
    if (caller == 0x80111C94u) half = kHalf + kMargin;                       // the tile layer
    else if (caller == 0x8010DFE0u && spawn_margin >= 0) half = kHalf + spawn_margin, shift = g_ws_view * kHalf;
    else return;
    // the 320x240 playfield around the camera
    const float l = RF32(out), t = RF32(out + 4), r = RF32(out + 8), b = RF32(out + 12);
    if (std::fabs(r - l - 320) < 0.01f && std::fabs(b - t - 240) < 0.01f) {
        const float cx = (l + r) / 2 + shift;
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
    const float dx = RF32(pos) - RF32(cam) - g_ws_view * kHalf, dy = RF32(pos + 4) - RF32(cam + 4);
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

// once per game frame: decide the presented window offset from this frame's layer extents
static void frame() {
    g_ws_on = g_active && g_cfg.widescreen;
    if (!g_ws_on) { g_ws_view = 0; g_ws_fill43 = false; g_layers.clear(); g_layer_order = 0; return; }
    // A layer at least as wide as the old 4:3 view that ends inside the 16:9 view on one side
    // and reaches past it on the other is the end of the map (the game's camera stops once the
    // 4:3 view meets it). The window slides toward the art: by the pad at most, and only as far
    // as that layer has art on the other side. Small pieces (props, single objects) don't count.
    const float kPad = 1.f - kOld;                                  // 0.25: the render pad
    float want_lo = 0, want_hi = 0;                                 // slide right / left
    for (auto& [id, li] : g_layers) {
        if (!li.seen || li.narrow || li.hi - li.lo < 2 * kOld - 0.05f) continue;
        if (li.lo > -1.f && li.hi > 1.01f) want_lo = std::max(want_lo, std::min({kPad, li.lo + 1.f, li.hi - 1.f}));
        if (li.hi < 1.f && li.lo < -1.01f) want_hi = std::max(want_hi, std::min({kPad, 1.f - li.hi, -1.f - li.lo}));
    }
    float target = want_lo > 0 && want_hi > 0 ? 0 : want_lo - want_hi;
    // a 4:3 room: something as wide as the 4:3 view is drawn, and nothing reaches past it
    bool room43 = false, inside = true;
    for (auto& [id, li] : g_layers) {
        if (!li.seen) continue;
        if (li.lo < -kOld - 0.03f || li.hi > kOld + 0.03f) inside = false;
        if (li.hi - li.lo >= 2 * kOld - 0.1f) room43 = true;
    }
    room43 = room43 && inside;
    static int room_held = 0;
    room_held = room43 == g_ws_fill43 ? 0 : room_held + 1;
    if (room_held >= 4) { g_ws_fill43 = room43; room_held = 0; }
    if (g_ws_fill43) target = 0;
    // the camera passes the 4:3 edge for single frames while scrolling: act on it only once it holds
    static float pending = 0; static int held = 0;
    if (target != 0 && std::fabs(target - pending) < 0.02f) held++; else held = 0;
    pending = target;
    if (target != 0 && held < 4) target = g_ws_view;
    const float step = 0.02f;                                       // ~4 px per frame
    g_ws_view += std::max(-step, std::min(step, target - g_ws_view));
    if (std::fabs(g_ws_view) < 1e-4f) g_ws_view = 0;
    static int n = 0;
    if (g_log_ws && ++n % 120 == 0) {
        fprintf(g_log, "[ws] view %.3f target %.3f%s layers:", g_ws_view, target, g_ws_fill43 ? " fill43" : "");
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
           enh_hook("fn_8000A9B0", hook_on_screen);
}

static const Enhancement widescreen = {
    "widescreen", "True widescreen", "16:9 playfield instead of 4:3 with bars (needs Aspect 16:9)", false, init, &g_active};
REGISTER_ENHANCEMENT(widescreen);
