// Diagnostic enhancement (off by default): times the game's own frames (the manager's
// VIWaitForRetrace hook) and logs, every 300 frames, the frame rate, how many frames took
// longer than one retrace (17.5 ms) or 1.5 retraces (25 ms), and the slowest frame.
// The smallest example of an enhancement.
#include "enhancements.h"
#include "hle.h"
#include <chrono>
#include <algorithm>

static bool g_active = false;

static void tick() {
    if (!g_active) return;
    using clk = std::chrono::steady_clock;
    static auto t0 = clk::now(), prev = clk::now();
    static unsigned n = 0, over17 = 0, over25 = 0;
    static double worst = 0;
    auto now = clk::now();
    double dt = std::chrono::duration<double, std::milli>(now - prev).count();
    prev = now;
    if (dt > 17.5) over17++;
    if (dt > 25) over25++;
    worst = std::max(worst, dt);
    if (++n == 300) {
        double s = std::chrono::duration<double>(now - t0).count();
        fprintf(g_log, "[frame_stats] %.2f game frames/s, %u frames >17.5 ms, %u >25 ms, slowest %.1f ms\n",
                n / s, over17, over25, worst);
        t0 = now;
        n = over17 = over25 = 0;
        worst = 0;
    }
}

static bool init() { enh_on_frame(tick); return true; }

static const Enhancement frame_stats = {
    "frame_stats", "Frame statistics", "Log the game's own frame rate and slow frames (diagnostics)", false, init, &g_active};
REGISTER_ENHANCEMENT(frame_stats);
