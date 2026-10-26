// Runtime side of the game module boundary (module_abi.h): finds contrarebirth_game.dll / .so,
// hands it the RuntimeApi and keeps what it returns (function table, hook slots).
#include "module_loader.h"
#include "ppc.h"
#include "hle.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

const FnEntry* g_fn_table = nullptr;
unsigned g_fn_count = 0;
const HookDef* g_hook_defs = nullptr;
unsigned g_hook_count = 0;
HookFn* g_hook_slots = nullptr;
std::string g_module_sha1, g_module_path;

GuestFn hle_lookup(const char* name);   // hle_registry.cpp

fs::path module_exe_dir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(std::wstring(buf, n)).parent_path();
#else
    std::error_code ec;
    return fs::read_symlink("/proc/self/exe", ec).parent_path();
#endif
}

static bool try_load(const fs::path& p, std::string& error) {
    std::error_code ec;
    if (!fs::exists(p, ec)) return false;
#ifdef _WIN32
    HMODULE h = LoadLibraryW(p.wstring().c_str());
    if (!h) { error = "could not load " + p.u8string() + " (error " + std::to_string(GetLastError()) + ")"; return false; }
    auto init = (ContraModuleInit)(void*)GetProcAddress(h, "contra_module_init");
#else
    void* h = dlopen(p.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h) { error = std::string("could not load ") + p.string() + " (" + dlerror() + ")"; return false; }
    auto init = (ContraModuleInit)dlsym(h, "contra_module_init");
#endif
    if (!init) { error = p.u8string() + " is not a game module"; return false; }
    static RuntimeApi api;
    api.abi = CONTRA_MODULE_ABI;
    api.mmio_read = mmio_read; api.mmio_write = mmio_write;
    api.fctiw = fctiw; api.fres = fres; api.frsqrte = frsqrte;
    api.psq_load_slow = psq_load_slow; api.psq_store_slow = psq_store_slow;
    api.mfspr = mfspr; api.mtspr = mtspr; api.mftb = mftb;
    api.dcbz = dcbz; api.sc = sc; api.unimpl = unimpl;
    api.dispatch = dispatch; api.trace_call = trace_call; api.stub_log = stub_log;
    api.irq_check = irq_check; api.idle_wait = idle_wait;
    api.irq_pending = &g_irq_pending;
    api.hle = hle_lookup;
    GameModule m{};
    if (!init(&api, &m)) {
        error = p.u8string() + (m.missing_hle ? std::string(" needs HLE function ") + m.missing_hle + ", which this runtime lacks"
                                              : std::string(" was made for another runtime version"));
        return false;
    }
    g_fn_table = m.table; g_fn_count = m.count;
    g_hook_defs = m.hooks; g_hook_count = m.hook_count; g_hook_slots = m.hook_fn;
    g_module_sha1 = m.dol_sha1;
    g_module_path = p.u8string();
    return true;
}

static std::string read_first_line(const fs::path& p) {
    FILE* f = fopen(p.u8string().c_str(), "rb");
    if (!f) return {};
    char buf[256] = {};
    if (!fgets(buf, sizeof buf, f)) buf[0] = 0;
    fclose(f);
    std::string s = buf;
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

bool module_is_release() {
    std::error_code ec;
    return fs::exists(module_exe_dir() / "tools" / "make_module.py", ec);
}

bool module_user_outdated() {
    if (!module_is_release()) return false;   // development builds have no stamp
    const std::string want = read_first_line(module_exe_dir() / "tools" / "build_inputs.stamp");
    const std::string have = read_first_line(module_user_dir() / "contrarebirth_game.stamp");
    return want.empty() || have != want;
}

bool module_load(std::string& error) {
    std::vector<fs::path> where;
    if (const char* e = getenv("CONTRA_MODULE")) where.push_back(fs::u8path(e));
    where.push_back(module_exe_dir() / CONTRA_MODULE_FILE);
    // a module built for another release's recompiler/tables is rebuilt, not loaded
    if (!module_user_outdated()) where.push_back(module_user_dir() / CONTRA_MODULE_FILE);
    for (const fs::path& p : where) {
        std::string err;
        if (try_load(p, err)) { fprintf(g_log, "[module] %s (main.dol sha1 %s)\n", g_module_path.c_str(), g_module_sha1.c_str()); return true; }
        if (!err.empty()) { error = err; fprintf(g_log, "[module] %s\n", err.c_str()); }
    }
    if (error.empty()) error = "no game module (" CONTRA_MODULE_FILE ") was found";
    return false;
}

fs::path module_user_dir() {
    if (const char* t = getenv("CONTRA_USER_DIR"); t && *t) return fs::u8path(t);   // tests
#ifdef _WIN32
    const char* base = getenv("APPDATA");
    fs::path p = base ? fs::u8path(base) : module_exe_dir();
#else
    const char* xdg = getenv("XDG_DATA_HOME");
    const char* home = getenv("HOME");
    fs::path p = xdg && *xdg ? fs::path(xdg) : home ? fs::path(home) / ".local" / "share" : module_exe_dir();
#endif
    return p / "Contra ReBirth Enhanced XD";
}
