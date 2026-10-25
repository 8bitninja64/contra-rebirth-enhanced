// Diagnostic enhancement (off by default): times the game's own frames (the manager's
// VIWaitForRetrace hook) and logs, every 300 frames, the frame rate, how many frames took
// longer than one retrace (17.5 ms) or 1.5 retraces (25 ms), and the slowest frame.
// The smallest example of an enhancement.
#include "enhancements.h"
#include "hle.h"
#include <chrono>
#include <algorithm>
#include <atomic>

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
    // where a slow frame's time went: guest threads idle, GX draws, EFB copies (+present), retraces
    extern std::atomic<uint64_t> g_idle_ns;
    extern uint64_t g_ns_draw, g_ns_copy, g_vi_count, g_ns_handoff, g_ns_poll;
    extern std::atomic<uint64_t> g_ns_audio, g_ns_switch;
    extern uint64_t g_n_progs, g_n_tex_uploads;
    static uint64_t np0 = 0, nt0 = 0;
    static uint64_t idle0 = 0, draw0 = 0, copy0 = 0, vi0 = 0, hand0 = 0, poll0 = 0, aud0 = 0, sw0 = 0;
    static int detailed = 0;
    uint64_t idle = g_idle_ns.load(), draw = g_ns_draw, copy = g_ns_copy, vi = g_vi_count, hand = g_ns_handoff, poll = g_ns_poll, aud = g_ns_audio.load(), sw = g_ns_switch.load();
    if (dt > 25 && detailed < 40) {
        detailed++;
        fprintf(g_log, "[frame_stats] slow frame %.1f ms: idle %.1f, draw %.1f, copy %.1f (hand-off %.1f, event poll %.1f), audio %.1f, thread-switch wait %.1f ms, %llu retraces, %llu new shaders, %llu texture uploads\n",
                dt, (idle - idle0) / 1e6, (draw - draw0) / 1e6, (copy - copy0) / 1e6, (hand - hand0) / 1e6, (poll - poll0) / 1e6,
                (aud - aud0) / 1e6, (sw - sw0) / 1e6, (unsigned long long)(vi - vi0),
                (unsigned long long)(g_n_progs - np0), (unsigned long long)(g_n_tex_uploads - nt0));
    }
    idle0 = idle; draw0 = draw; copy0 = copy; vi0 = vi; hand0 = hand; poll0 = poll; aud0 = aud; sw0 = sw; np0 = g_n_progs; nt0 = g_n_tex_uploads;
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
