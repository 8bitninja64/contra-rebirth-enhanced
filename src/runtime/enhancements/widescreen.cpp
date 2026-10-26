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
// - Rooms whose playfield is only 4:3 wide (a layer covering exactly the 4:3 width besides the
//   backdrop) never slide. Background layers that stop at the map edge there (the Stage 2
//   elevator's city skyline) are drawn a second time, moved half a view over, to fill the side
//   past the edge.
// - Rooms where every layer is 4:3 (the vertical Stage 4 skull room and descent shaft) keep
//   black bars: the playfield copy blacks out the sides (g_ws_fill43, gx_render.cpp).
// - A backdrop picture that is fixed to the screen and repeated side by side (the Stage 3
//   truck sky: its tiles stay on the 4:3 edges while the scenery scrolls) is stretched like a
//   4:3 one instead of showing the repeats.
#include "enhancements.h"
#include "config.h"
#include "hle.h"
#include <cstdlib>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

extern bool g_ws_on, g_ws_in_layer, g_ws_lany, g_ws_fill43, g_ws_ledge_lo, g_ws_ledge_hi;
extern float g_ws_layer_stretch, g_ws_view, g_ws_lmin, g_ws_lmax, g_ws_lymin, g_ws_lymax;
struct WsDup { bool on = false; float dx = 0, ysc = 1, yoff = 0, clip_lo = 0, clip_hi = 0; };
extern WsDup g_ws_dup;
void gx_flush();
static bool g_active = false;
static const float kHalf = 320.f * (16.f / 9.f) / (4.f / 3.f) / 2;   // 213.33: half the 16:9 width
static const float kOld = 160.f / kHalf;                           // 0.75: the old 4:3 edge in clip units
static const float kMargin = 64.f;                                 // culling slack past the view edge

struct LayerInfo {
    float lo = 0, hi = 0, ylo = 0, yhi = 0; bool seen = false, narrow = false; int order = 0;
    int locked = 0;            // the scene scrolls while its tiles stay on the 4:3 edges (screen-fixed picture)
    float plo = 0, phi = 0;    // extent last frame
    int slo = 0, shi = 0;      // frames the left / right end has held still
    std::vector<WsDup> dups;   // extra draws decided last frame
};
static std::unordered_map<uint32_t, LayerInfo> g_layers;           // by layer object
static int g_layer_order = 0;
static bool g_cam_moved = false;   // some layer scrolled last frame (the camera, or the scenery past a fixed one)
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
    // backmost 4:3 picture (alone, or repeated side by side): stretch it over the 16:9 width
    g_ws_layer_stretch = ((li.narrow || li.locked >= 20) && li.order == 0) ? 1.f / kOld : 1.f;
    g_ws_in_layer = true;
    g_ws_lany = false; g_ws_ledge_lo = g_ws_ledge_hi = false;
    static Ctx before, after;
    const size_t regs = offsetof(Ctx, spr);
    if (!li.dups.empty()) memcpy(&before, c, regs);
    orig(c);
    const bool any = g_ws_lany;
    const float lo = g_ws_lmin, hi = g_ws_lmax, ylo = g_ws_lymin, yhi = g_ws_lymax;
    const bool on_edges = g_ws_ledge_lo && g_ws_ledge_hi;
    if (!li.dups.empty()) {   // the same layer again, moved/clipped by the renderer
        memcpy(&after, c, regs);
        for (const WsDup& d : li.dups) {
            memcpy(c, &before, regs);
            gx_flush();
            g_ws_dup = d; g_ws_dup.on = true;
            orig(c);
            gx_flush();
            g_ws_dup.on = false;
        }
        memcpy(c, &after, regs);
    }
    g_ws_in_layer = false;
    g_ws_layer_stretch = 1.f;
    li.seen = any;
    if (any) {
        li.lo = lo; li.hi = hi; li.ylo = ylo; li.yhi = yhi;
        if (g_cam_moved)   // only frames where the camera moved tell a screen-fixed picture apart
            li.locked = on_edges && lo < -1.f && hi > 1.f ? std::min(li.locked + 1, 1000) : 0;   // repeated past both sides
        // covers the old 4:3 width and nothing more, although culling allowed more
        li.narrow = li.hi <= kOld + 0.03f && li.lo >= -kOld - 0.03f && li.hi - li.lo >= 2 * kOld - 0.1f;
    }
}

// once per game frame: decide the presented window offset from this frame's layer extents
static void frame() {
    g_ws_on = g_active && g_cfg.widescreen;
    if (!g_ws_on) { g_ws_view = 0; g_ws_fill43 = false; g_layers.clear(); g_layer_order = 0; return; }
    g_cam_moved = false;
    for (auto& [id, li] : g_layers) {
        const bool mlo = std::fabs(li.lo - li.plo) > 0.002f, mhi = std::fabs(li.hi - li.phi) > 0.002f;
        if (li.seen && li.order > 0 && li.hi - li.lo >= 2 * kOld - 0.05f && (mlo || mhi)) g_cam_moved = true;   // scenery, not small animated pieces
        li.slo = mlo ? 0 : std::min(li.slo + 1, 1000);
        li.shi = mhi ? 0 : std::min(li.shi + 1, 1000);
        li.plo = li.lo; li.phi = li.hi;
    }
    // A layer at least as wide as the old 4:3 view that ends inside the 16:9 view on one side
    // and reaches past it on the other, at an end that holds still, is the end of the map (the
    // game's camera stops once the 4:3 view meets it). The window slides toward the art: by the
    // pad at most, and only as far as that layer has art on the other side. An end that moves
    // is a scrolling strip drawn only as wide as 4:3 (the Stage 2 elevator's skyline): that one
    // is continued past its end instead (below), and the window holds where it is.
    // Small pieces (props, single objects) don't count.
    const float kPad = 1.f - kOld;                                  // 0.25: the render pad
    const int kStill = 8;                                           // frames an end must hold still
    auto wide = [](const LayerInfo& li) { return li.seen && !li.narrow && li.hi - li.lo >= 2 * kOld - 0.05f; };
    float want_lo = 0, want_hi = 0;                                 // slide right / left
    bool moving_edge = false;
    for (auto& [id, li] : g_layers) {
        if (!wide(li)) continue;
        if (li.lo > -1.f && li.hi > 1.01f) {
            if (li.slo >= kStill) want_lo = std::max(want_lo, std::min({kPad, li.lo + 1.f, li.hi - 1.f}));
            else moving_edge = true;
        }
        if (li.hi < 1.f && li.lo < -1.01f) {
            if (li.shi >= kStill) want_hi = std::max(want_hi, std::min({kPad, 1.f - li.hi, -1.f - li.lo}));
            else moving_edge = true;
        }
    }
    float target = want_lo > 0 && want_hi > 0 ? 0 : want_lo - want_hi;
    if (target == 0 && moving_edge) target = g_ws_view;
    // 4:3 rooms: a playfield layer (not the backdrop) covers exactly the 4:3 width; "all 4:3"
    // when also nothing reaches past it
    bool frame43 = false, room43 = false, inside = true, edge_lo = false, edge_hi = false;
    for (auto& [id, li] : g_layers) {
        if (!li.seen) continue;
        if (li.lo < -kOld - 0.03f || li.hi > kOld + 0.03f) inside = false;
        if (li.hi - li.lo >= 2 * kOld - 0.1f) room43 = true;
        if (li.narrow && li.order > 0) frame43 = true;
        // structure that stops exactly on a 4:3 edge and stays inside the 4:3 width
        if (li.order > 0 && li.lo >= -kOld - 0.02f && li.hi <= kOld + 0.02f) {
            if (std::fabs(li.lo + kOld) < 0.02f) edge_lo = true;
            if (std::fabs(li.hi - kOld) < 0.02f) edge_hi = true;
        }
    }
    room43 = room43 && inside;
    frame43 = frame43 || (edge_lo && edge_hi);
    static int room_held = 0, frame_held = 0;
    static bool all43 = false, part43 = false;
    room_held = room43 == all43 ? 0 : room_held + 1;
    if (room_held >= 4) { all43 = room43; room_held = 0; }
    frame_held = frame43 == part43 ? 0 : frame_held + 1;
    if (frame_held >= 4) { part43 = frame43; frame_held = 0; }
    if (all43 || part43) target = 0;
    // extra draws for the next frame
    // Backgrounds that stop inside the view (at the edge of a 4:3 room, or a scrolling strip whose
    // end moves): continue them past their end with a copy of themselves one view width over.
    for (auto& [id, li] : g_layers) li.dups.clear();
    if (!all43)
        for (auto& [id, li] : g_layers) {
            if (!wide(li)) continue;
            WsDup d;
            if (li.lo > -1.f && li.hi > 1.01f && (part43 || li.slo < kStill)) {
                d.dx = -1.f; d.clip_lo = -1.3f; d.clip_hi = li.lo; li.dups.push_back(d);
            }
            if (li.hi < 1.f && li.lo < -1.01f && (part43 || li.shi < kStill)) {
                d.dx = 1.f; d.clip_lo = li.hi; d.clip_hi = 1.3f; li.dups.push_back(d);
            }
        }
    g_ws_fill43 = all43;   // vertical stages keep black bars (the owner's choice)
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
            if (li.seen) fprintf(g_log, " #%d %08X %.2f..%.2f y %.2f..%.2f L%d%s%s", li.order, id, li.lo, li.hi, li.ylo, li.yhi, li.locked,
                                 li.narrow ? " narrow" : li.locked >= 20 ? " locked" : "", li.dups.empty() ? "" : " dup");
        for (auto& [id, li] : g_layers) if (li.order == 0) fprintf(g_log, " L%d", li.locked);
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
