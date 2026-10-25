// Test/RE helpers driven by environment variables, run once per retrace on the guest thread.
//
//   MEMDUMP_AT=1200,1500,...   VI counts at which MEM1 (24 MiB at 0x80000000) is written as
//   MEMDUMP_DIR=<dir>          mem1_<vi>.bin (plus MEM2 at 0x90000000 as mem2_<vi>.bin with MEMDUMP_MEM2=1)
//   MEMDUMP_EVERY=<n>          ...or every n retraces between MEMDUMP_FROM and MEMDUMP_TO
//   POKE=8:8030A1B3=09@1000-99999,32:80300000=1@500
//                              write a byte/half/word every retrace inside the VI range
//                              (size 8/16/32, hex address, hex value; "@a" alone = from a on)
//   WATCH=802E0138+4+90        (Windows) write watchpoint on a 4-byte guest word, given as an
//   WATCH_FROM=<vi>            address or pointer chain (R32(A)+o1, R32(..)+o2); armed at that VI. Every write
//                              logs the host instruction address ([watch] rip=...), which
//                              tools map to f_XXXXXXXX with nm on the exe.
#include "hle.h"
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#endif

struct Poke { int size; uint32_t addr, val; uint64_t from, to; };
static std::vector<Poke> g_pokes;
static std::vector<uint64_t> g_dump_at;
static uint64_t g_dump_every = 0, g_dump_from = 0, g_dump_to = ~0ull;
static std::string g_dump_dir = ".";
static bool g_loaded = false;

static void load() {
    g_loaded = true;
    if (const char* d = getenv("MEMDUMP_DIR")) g_dump_dir = d;
    if (const char* e = getenv("MEMDUMP_EVERY")) g_dump_every = strtoull(e, nullptr, 10);
    if (const char* e = getenv("MEMDUMP_FROM")) g_dump_from = strtoull(e, nullptr, 10);
    if (const char* e = getenv("MEMDUMP_TO")) g_dump_to = strtoull(e, nullptr, 10);
    if (const char* a = getenv("MEMDUMP_AT")) {
        for (const char* p = a; *p;) {
            char* end; uint64_t v = strtoull(p, &end, 10);
            if (end == p) break;
            g_dump_at.push_back(v);
            p = *end ? end + 1 : end;
        }
    }
    if (const char* s = getenv("POKE")) {
        std::string all = s;
        size_t pos = 0;
        while (pos < all.size()) {
            size_t comma = all.find(',', pos);
            std::string it = all.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
            pos = comma == std::string::npos ? all.size() : comma + 1;
            Poke p = {32, 0, 0, 0, ~0ull};
            unsigned long long from = 0, to = ~0ull;
            unsigned a = 0, v = 0;
            int n = sscanf(it.c_str(), "%d:%x=%x@%llu-%llu", &p.size, &a, &v, &from, &to);
            if (n < 3) { fprintf(g_log, "[poke] cannot parse '%s'\n", it.c_str()); continue; }
            p.addr = a; p.val = v; p.from = n >= 4 ? from : 0; p.to = n >= 5 ? to : ~0ull;
            g_pokes.push_back(p);
            fprintf(g_log, "[poke] %d-bit %08X = %X from VI %llu\n", p.size, p.addr, p.val, (unsigned long long)p.from);
        }
    }
}

static void dump(uint64_t vi) {
    struct { uint32_t base, size; const char* name; } regions[] = {
        {0x80000000u, 0x01800000u, "mem1"}, {0x90000000u, 0x03400000u, "mem2"}};
    static const bool with_mem2 = getenv("MEMDUMP_MEM2") != nullptr;   // MEM2 is 52 MiB: opt-in
    for (auto& r : regions) {
        if (r.base == 0x90000000u && !with_mem2) continue;
        char path[1024];
        snprintf(path, sizeof path, "%s/%s_%06llu.bin", g_dump_dir.c_str(), r.name, (unsigned long long)vi);
        if (FILE* f = fopen(path, "wb")) { fwrite(MEM + r.base, 1, r.size, f); fclose(f); }
    }
    extern uint64_t g_frame;
    fprintf(g_log, "[memdump] VI %llu frame %llu\n", (unsigned long long)vi, (unsigned long long)g_frame);
}

#ifdef _WIN32
// ---------------------------------------------------------------- write watchpoint
static uint8_t* g_watch = nullptr;          // host address of the watched word
static uint8_t* g_watch_page = nullptr;
static thread_local bool t_stepping = false;
static uint32_t g_watch_old = 0;
static LONG CALLBACK watch_handler(EXCEPTION_POINTERS* ep) {
    auto* er = ep->ExceptionRecord;
    DWORD old;
    if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2 && er->ExceptionInformation[0] == 1) {
        uint8_t* a = (uint8_t*)er->ExceptionInformation[1];
        if (a < g_watch_page || a >= g_watch_page + 4096) return EXCEPTION_CONTINUE_SEARCH;
        if (a >= g_watch && a < g_watch + 4) {
            uint32_t v; memcpy(&v, g_watch, 4);
            g_watch_old = __builtin_bswap32(v);
            extern uint64_t g_frame, g_vi_count;
            fprintf(g_log, "[watch] write by rip=%p (word was %u) at VI %llu frame %llu\n", (void*)ep->ContextRecord->Rip,
                    g_watch_old, (unsigned long long)g_vi_count, (unsigned long long)g_frame);
            // the store is often inside an out-of-line W32(): the caller's return address is near the stack top
            const uint64_t* sp = (const uint64_t*)ep->ContextRecord->Rsp;
            for (int k = 0; k < 160; k++) fprintf(g_log, "[watch]   stack rip=%p\n", (void*)sp[k]);
        }
        VirtualProtect(g_watch_page, 4096, PAGE_READWRITE, &old);   // let this one store through...
        ep->ContextRecord->EFlags |= 0x100;                        // ...then trap right after it
        t_stepping = true;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (er->ExceptionCode == EXCEPTION_SINGLE_STEP && t_stepping) {
        t_stepping = false;
        uint32_t v; memcpy(&v, g_watch, 4); v = __builtin_bswap32(v);
        if (v != g_watch_old) { fprintf(g_log, "[watch] now %u\n", v); g_watch_old = v; }
        VirtualProtect(g_watch_page, 4096, PAGE_READONLY, &old);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
static void watch_arm() {
    const char* w = getenv("WATCH");
    // "A" = address A; "A+o1+o2..." = pointer chain: R32(A)+o1, then R32(that)+o2, ...
    char* p;
    uint32_t addr = (uint32_t)strtoul(w, &p, 16);
    while (*p == '+') addr = R32(addr) + (uint32_t)strtoul(p + 1, &p, 16);
    g_watch = MEM + xl(addr);
    g_watch_page = (uint8_t*)((uintptr_t)g_watch & ~(uintptr_t)4095);
    memcpy(&g_watch_old, g_watch, 4); g_watch_old = __builtin_bswap32(g_watch_old);
    AddVectoredExceptionHandler(1, watch_handler);
    DWORD old;
    VirtualProtect(g_watch_page, 4096, PAGE_READONLY, &old);
    fprintf(g_log, "[watch] armed on %08X (value %u), exe at %p\n", addr, g_watch_old, (void*)GetModuleHandleA(nullptr));
}
#endif

// ---------------------------------------------------------------- sampling profiler
// SAMPLE_PROFILE=1: about 1000 times a second every registered guest thread is suspended and
// its instruction pointer counted; every 10 s the hottest addresses are logged as
// "[prof] rip=<addr> n=<count>" (map them to f_XXXXXXXX with nm on the exe).
#ifdef _WIN32
#include <algorithm>
#include <map>
#include <mutex>
#include <thread>
#include <chrono>
static std::mutex g_prof_mx;
static std::vector<HANDLE> g_prof_threads;
void debug_register_thread() {
    static const bool on = getenv("SAMPLE_PROFILE") != nullptr;
    if (!on) return;
    HANDLE h;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &h, 0, FALSE, DUPLICATE_SAME_ACCESS);
    std::lock_guard<std::mutex> g(g_prof_mx);
    g_prof_threads.push_back(h);
    static bool started = false;
    if (started) return;
    started = true;
    std::thread([] {
        std::map<uint64_t, unsigned> hits;
        auto last = std::chrono::steady_clock::now();
        fprintf(g_log, "[prof] exe at %p\n", (void*)GetModuleHandleA(nullptr));
        for (;;) {
            Sleep(1);
            std::vector<HANDLE> ths;
            { std::lock_guard<std::mutex> g(g_prof_mx); ths = g_prof_threads; }
            for (HANDLE h : ths) {
                if (SuspendThread(h) == (DWORD)-1) continue;
                CONTEXT ctx = {};
                ctx.ContextFlags = CONTEXT_CONTROL;
                if (GetThreadContext(h, &ctx)) hits[ctx.Rip]++;
                ResumeThread(h);
            }
            if (std::chrono::steady_clock::now() - last > std::chrono::seconds(10)) {
                last = std::chrono::steady_clock::now();
                std::vector<std::pair<unsigned, uint64_t>> v;
                for (auto& [rip, n] : hits) v.push_back({n, rip});
                std::sort(v.rbegin(), v.rend());
                for (size_t i = 0; i < v.size() && i < 400; i++) fprintf(g_log, "[prof] rip=%llx n=%u\n", (unsigned long long)v[i].second, v[i].first);
                // totals per module: separates driver time from threads waiting in the OS
                std::map<std::string, unsigned> mods;
                for (auto& [n, rip] : v) {
                    HMODULE m = nullptr;
                    char name[MAX_PATH] = "?";
                    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                           (LPCSTR)rip, &m) && m) {
                        GetModuleFileNameA(m, name, sizeof name);
                        const char* b = strrchr(name, '\\');
                        if (b) memmove(name, b + 1, strlen(b));
                    }
                    mods[name] += n;
                }
                for (auto& [name, n] : mods) fprintf(g_log, "[prof] module %s n=%u\n", name.c_str(), n);
                fprintf(g_log, "[prof] ---\n");
                fflush(g_log);
                hits.clear();
            }
        }
    }).detach();
}
#else
void debug_register_thread() {}
#endif

void debug_on_retrace(uint64_t vi) {
    if (!g_loaded) load();
#ifdef _WIN32
    static const uint64_t watch_from = getenv("WATCH") ? strtoull(getenv("WATCH_FROM") ? getenv("WATCH_FROM") : "1", nullptr, 10) : 0;
    if (watch_from && vi == watch_from) watch_arm();
#endif
    for (auto& p : g_pokes) {
        if (vi < p.from || vi > p.to) continue;
        if (p.size == 8) W8(p.addr, (uint8_t)p.val);
        else if (p.size == 16) W16(p.addr, (uint16_t)p.val);
        else W32(p.addr, p.val);
    }
    bool d = false;
    for (uint64_t a : g_dump_at) d |= a == vi;
    if (g_dump_every && vi >= g_dump_from && vi <= g_dump_to && (vi - g_dump_from) % g_dump_every == 0) d = true;
    if (d) dump(vi);
}
