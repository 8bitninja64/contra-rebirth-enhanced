// True widescreen: shows a 16:9 slice of the playfield instead of the pillarboxed 4:3 one.
// Needs the game's own 16:9 mode (widescreen = 1 in the ini), which makes it compose the
// frame on an 854-wide canvas.
//
// Render side (gx_render.cpp, g_ws_on): the 320x240 playfield pass is widened to 426.67
// units around the same camera centre, the EFB copy takes the wider region and the
// composite quad fills the 16:9 canvas.
// Game side (here): fn_800DFC30 copies a view's rectangle (left, top, right, bottom at
// view+0x80, centred on the camera) for its callers; the tile layer (fn_80111A70) and the
// sprite paths cull against it.
// When it is the 320x240 playfield (-160,-120,160,120) callers get it 53.33 wider each side.
#include "enhancements.h"
#include "config.h"
#include "hle.h"
#include <cstdlib>

extern bool g_ws_on;
static bool g_active = false;
static const float kExtra = (320.f * (16.f / 9.f) / (4.f / 3.f) - 320.f) / 2;   // 53.33

static void sync() { g_ws_on = g_active && g_cfg.widescreen; }

static void hook_view_rect(Ctx* c, GuestFn orig) {
    const uint32_t out = c->r[3];
    orig(c);
    static int logged = 0;
    static uint32_t seen[64][2];
    bool fresh = getenv("WS_LOG") && logged < 64;
    for (int i = 0; fresh && i < logged; i++)
        if (seen[i][0] == c->lr && seen[i][1] == R32(out + 8)) fresh = false;
    if (fresh) {
        seen[logged][0] = c->lr; seen[logged][1] = R32(out + 8);
        logged++;
        fprintf(g_log, "[ws] rect %.1f %.1f %.1f %.1f view %08X lr %08X\n", RF32(out), RF32(out + 4), RF32(out + 8),
                RF32(out + 12), c->r[4], c->lr);
    }
    if (!g_ws_on) return;
    if (RF32(out) == -160 && RF32(out + 4) == -120 && RF32(out + 8) == 160 && RF32(out + 12) == 120) {
        WF32(out, -160 - kExtra);
        WF32(out + 8, 160 + kExtra);
    }
}

static bool init() {
    enh_on_frame(sync);
    return enh_hook("fn_800DFC30", hook_view_rect);
}

static const Enhancement widescreen = {
    "widescreen", "True widescreen", "16:9 playfield instead of 4:3 with bars (needs Aspect 16:9)", false, init, &g_active};
REGISTER_ENHANCEMENT(widescreen);
