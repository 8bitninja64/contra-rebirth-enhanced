#include "enhancements.h"
#include "hooks_table.h"
#include "config.h"
#include "hle.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>
#include <string>

extern HookFn g_hook_fn[];

// The DOL this recompilation (symbols, hooks, game.h) was made for.
static const char* kSupportedDolSha1 = "825fb4ec04335500ca9e172d05b2e593c5ab7dc9";

struct Slot { const Enhancement* e; bool initialized; bool failed; };
static std::vector<Slot>& slots() { static std::vector<Slot> v; return v; }
static bool g_compatible = false;

void enh_register(const Enhancement* e) { slots().push_back({e, false, false}); }
int enh_count() { return (int)slots().size(); }
const Enhancement* enh_get(int i) { return i >= 0 && i < enh_count() ? slots()[i].e : nullptr; }
bool enh_compatible() { return g_compatible; }

bool enh_hook(const char* guest_name, HookFn fn) {
    for (unsigned i = 0; i < HOOK_COUNT; i++)
        if (HOOK_DEFS[i].name && !strcmp(HOOK_DEFS[i].name, guest_name)) { g_hook_fn[i] = fn; return true; }
    fprintf(g_log, "[enh] hook target %s is not in hooks.txt (regenerate src/gen after editing it)\n", guest_name);
    return false;
}

// ---------------------------------------------------------------- per-frame callbacks
static std::vector<void (*)()>& frame_fns() { static std::vector<void (*)()> v; return v; }
void enh_on_frame(void (*fn)()) { frame_fns().push_back(fn); }
static void hook_VIWaitForRetrace(Ctx* c, GuestFn orig) {
    orig(c);
    for (auto fn : frame_fns()) fn();
}

// ---------------------------------------------------------------- SHA-1 (file check only)
static std::string sha1_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return "";
    std::vector<uint8_t> d;
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) d.insert(d.end(), buf, buf + n);
    fclose(f);
    uint64_t bits = (uint64_t)d.size() * 8;
    d.push_back(0x80);
    while (d.size() % 64 != 56) d.push_back(0);
    for (int i = 7; i >= 0; i--) d.push_back((uint8_t)(bits >> (i * 8)));
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    auto rol = [](uint32_t x, int s) { return (x << s) | (x >> (32 - s)); };
    for (size_t o = 0; o < d.size(); o += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++) w[i] = (uint32_t)d[o + 4 * i] << 24 | d[o + 4 * i + 1] << 16 | d[o + 4 * i + 2] << 8 | d[o + 4 * i + 3];
        for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], e4 = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f2, k;
            if (i < 20) { f2 = (b & c) | (~b & e4); k = 0x5A827999; }
            else if (i < 40) { f2 = b ^ c ^ e4; k = 0x6ED9EBA1; }
            else if (i < 60) { f2 = (b & c) | (b & e4) | (c & e4); k = 0x8F1BBCDC; }
            else { f2 = b ^ c ^ e4; k = 0xCA62C1D6; }
            uint32_t t = rol(a, 5) + f2 + e + k + w[i];
            e = e4; e4 = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += e4; h[4] += e;
    }
    char hex[41];
    for (int i = 0; i < 5; i++) snprintf(hex + 8 * i, 9, "%08x", h[i]);
    return hex;
}

// ---------------------------------------------------------------- state
bool enh_enabled(const char* id) {
    for (auto& s : slots())
        if (!strcmp(s.e->id, id)) return s.e->active && *s.e->active;
    return false;
}

static void start(Slot& s) {
    if (!s.initialized && !s.failed) {
        s.initialized = true;
        if (s.e->init && !s.e->init()) {
            s.failed = true;
            fprintf(g_log, "[enh] %s failed to start and stays off\n", s.e->id);
        }
    }
    if (s.e->active) *s.e->active = !s.failed;
}

void enh_set_enabled(const char* id, bool on) {
    for (auto& s : slots()) {
        if (strcmp(s.e->id, id)) continue;
        g_cfg.enh[id] = on;
        if (on && g_compatible) start(s);
        else if (s.e->active) *s.e->active = false;
        fprintf(g_log, "[enh] %s %s\n", id, on && s.e->active && *s.e->active ? "on" : "off");
        config_save();
        return;
    }
}

void enh_init_all(const char* dol_path) {
    std::string h = sha1_file(dol_path);
    g_compatible = h == kSupportedDolSha1;
    if (!g_compatible)
        fprintf(g_log, "[enh] main.dol sha1 %s is not the supported build %s: all enhancements disabled\n",
                h.empty() ? "(unreadable)" : h.c_str(), kSupportedDolSha1);
    if (g_compatible) enh_hook("VIWaitForRetrace", hook_VIWaitForRetrace);
    // ENH="id=1,id2=0": per-run overrides for tests, not saved
    std::string env = getenv("ENH") ? getenv("ENH") : "";
    auto env_value = [&](const char* id, bool* out) {
        std::string key = std::string(id) + "=";
        size_t p = env.find(key);
        if (p == std::string::npos || (p && env[p - 1] != ',')) return false;
        *out = env[p + key.size()] == '1';
        return true;
    };
    bool new_keys = false;
    for (auto& s : slots()) {
        auto it = g_cfg.enh.find(s.e->id);
        if (it == g_cfg.enh.end()) { g_cfg.enh[s.e->id] = s.e->default_on; new_keys = true; }   // list it in the .ini
        bool on = g_cfg.enh[s.e->id];
        env_value(s.e->id, &on);
        if (on && g_compatible) start(s);
        fprintf(g_log, "[enh] %-20s %s  %s\n", s.e->id, s.e->active && *s.e->active ? "on " : "off", s.e->description);
    }
    if (new_keys) config_save();
}
