// Diagnostics (enh spawn_log): logs every game object (CTBehave list, head at 0x802E0178,
// next at +0x0C) when it appears and disappears, with its x relative to the view centre.
#include "enhancements.h"
#include "hle.h"
#include <cstdlib>
#include <unordered_map>

float g_dbg_view_cx = 0;
extern uint64_t g_frame;
struct Obj { uint32_t vt; float x0, x; uint64_t born; };
static std::unordered_map<uint32_t, Obj> g_objs;

static void frame() {
    static uint32_t last_vt = 0;
    const uint32_t v = R32(0x802E0120u), vt = v ? R32(v) : 0;
    if (vt != last_vt) { last_vt = vt; fprintf(g_log, "[spawn] view %08X vt %08X slot20 %08X slot1C %08X slot24 %08X\n", v, vt, vt ? R32(vt + 0x20) : 0, vt ? R32(vt + 0x1C) : 0, vt ? R32(vt + 0x24) : 0); }
    std::unordered_map<uint32_t, bool> seen;
    int n = 0;
    for (uint32_t o = R32(0x802E0178u); o && n < 4096; o = R32(o + 0x0C), n++) {
        seen[o] = true;
        const float x = RF32(o + 0x2C) - g_dbg_view_cx;
        auto it = g_objs.find(o);
        if (it == g_objs.end() || it->second.vt != R32(o)) {
            g_objs[o] = {R32(o), x, x, g_frame};
            fprintf(g_log, "[spawn] f%llu + %08X vt %08X x %.0f y %.0f\n", (unsigned long long)g_frame, o, R32(o), x, RF32(o + 0x30));
        } else it->second.x = x;
    }
    for (auto it = g_objs.begin(); it != g_objs.end();)
        if (!seen.count(it->first)) {
            fprintf(g_log, "[spawn] f%llu - %08X vt %08X x %.0f (born x %.0f, %llu frames)\n", (unsigned long long)g_frame, it->first,
                    it->second.vt, it->second.x, it->second.x0, (unsigned long long)(g_frame - it->second.born));
            it = g_objs.erase(it);
        } else ++it;
}

static bool g_active = false;
static bool init() { enh_on_frame(frame); return true; }
static const Enhancement spawn_log = {"spawn_log", "Spawn log", "Log game objects as they appear and vanish (diagnostics)", false, init, &g_active};
REGISTER_ENHANCEMENT(spawn_log);
