// Diagnostic enhancement (off by default): counts the game's VIWaitForRetrace calls and logs
// how many game frames run per second. Also the smallest working example of a hook.
#include "enhancements.h"
#include "hle.h"
#include <chrono>

static bool g_active = false;

static void hook_VIWaitForRetrace(Ctx* c, GuestFn orig) {
    orig(c);
    if (!g_active) return;
    using clk = std::chrono::steady_clock;
    static auto t0 = clk::now();
    static unsigned n = 0;
    if (++n == 300) {
        double s = std::chrono::duration<double>(clk::now() - t0).count();
        fprintf(g_log, "[frame_stats] %.2f game frames/s\n", n / s);
        t0 = clk::now();
        n = 0;
    }
}

static bool init() { return enh_hook("VIWaitForRetrace", hook_VIWaitForRetrace); }

static const Enhancement frame_stats = {
    "frame_stats", "Frame statistics", "Log the game's own frame rate (diagnostics)", false, init, &g_active};
REGISTER_ENHANCEMENT(frame_stats);
