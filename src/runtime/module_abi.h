// The game module boundary. The recompiled game code (src/gen, derived from the player's
// main.dol) builds as its own library, contrarebirth_game.dll / .so, so a release can ship the
// runtime alone: the module is generated and compiled from the player's WAD on first start.
//
// The runtime loads the module (module_loader.cpp) and calls its one export,
// contra_module_init(), with a RuntimeApi: the helpers the generated code calls (module side:
// module_rt.cpp forwards each one), the interrupt flag and an HLE name lookup. The module answers
// with a GameModule: its function table, hook slots and the SHA-1 of the main.dol it was made
// from. Only plain C types cross the boundary, so the module may come from another compiler
// than the runtime (the release builds it with Zig on the player's machine).
#pragma once
#include <atomic>
#include <cstdint>

struct Ctx;
typedef void (*GuestFn)(Ctx*);
typedef void (*HookFn)(Ctx*, GuestFn orig);

struct FnEntry { uint32_t addr, size; GuestFn fn; const char* name; };
struct HookDef { const char* name; uint32_t addr; GuestFn orig; };

// Bump when RuntimeApi or GameModule change: a module made for another runtime is rebuilt.
#define CONTRA_MODULE_ABI 1

struct RuntimeApi {
    uint32_t abi;
    uint32_t (*mmio_read)(uint32_t a, int size);
    void (*mmio_write)(uint32_t a, uint32_t v, int size);
    uint32_t (*fctiw)(Ctx* c, double v, bool trunc);
    double (*fres)(double v);
    double (*frsqrte)(double v);
    void (*psq_load_slow)(Ctx* c, int frd, uint32_t ea, int w, int i);
    void (*psq_store_slow)(Ctx* c, int frs, uint32_t ea, int w, int i);
    uint32_t (*mfspr)(Ctx* c, int spr);
    void (*mtspr)(Ctx* c, int spr, uint32_t v);
    uint32_t (*mftb)(Ctx* c, int which);
    void (*dcbz)(uint32_t ea);
    void (*sc)(Ctx* c, uint32_t pc);
    void (*unimpl)(Ctx* c, uint32_t pc, const char* what);
    void (*dispatch)(Ctx* c, uint32_t target);
    void (*trace_call)(uint32_t addr);
    void (*stub_log)(uint32_t addr);
    void (*irq_check)(Ctx* c);
    void (*idle_wait)();
    std::atomic<int>* irq_pending;
    GuestFn (*hle)(const char* name);   // runtime HLE function by hle.txt name (nullptr: none)
};

struct GameModule {
    uint32_t abi;
    const FnEntry* table; unsigned count;           // sorted by address
    const HookDef* hooks; unsigned hook_count;       // hooks.txt slots
    HookFn* hook_fn;                                 // the slots' current host functions
    char dol_sha1[41];                               // main.dol the code was generated from
    const char* missing_hle;                         // set when init failed: an HLE the runtime lacks
};

#if defined(_WIN32)
#define CONTRA_MODULE_EXPORT extern "C" __declspec(dllexport)
#else
#define CONTRA_MODULE_EXPORT extern "C" __attribute__((visibility("default")))
#endif
typedef bool (*ContraModuleInit)(const RuntimeApi* api, GameModule* out);
