// Module side of the game module boundary (module_abi.h): compiled into contrarebirth_game
// together with src/gen, never into the runtime. Every runtime helper the generated code calls
// is defined here as a forwarder through the RuntimeApi the runtime passes to
// contra_module_init() (gen/module.cpp).
#ifndef CONTRA_MODULE   // the module build defines it for every file (gen code included)
#define CONTRA_MODULE 1
#endif
#include "ppc.h"
#include "module_abi.h"

const RuntimeApi* g_api = nullptr;
std::atomic<int>* g_irq_pending_ptr = nullptr;

void module_rt_bind(const RuntimeApi* api) {
    g_api = api;
    g_irq_pending_ptr = api->irq_pending;
}

uint32_t mmio_read(uint32_t a, int size) { return g_api->mmio_read(a, size); }
void mmio_write(uint32_t a, uint32_t v, int size) { g_api->mmio_write(a, v, size); }
uint32_t fctiw(Ctx* c, double v, bool trunc) { return g_api->fctiw(c, v, trunc); }
double fres(double v) { return g_api->fres(v); }
double frsqrte(double v) { return g_api->frsqrte(v); }
void psq_load_slow(Ctx* c, int frd, uint32_t ea, int w, int i) { g_api->psq_load_slow(c, frd, ea, w, i); }
void psq_store_slow(Ctx* c, int frs, uint32_t ea, int w, int i) { g_api->psq_store_slow(c, frs, ea, w, i); }
uint32_t mfspr(Ctx* c, int spr) { return g_api->mfspr(c, spr); }
void mtspr(Ctx* c, int spr, uint32_t v) { g_api->mtspr(c, spr, v); }
uint32_t mftb(Ctx* c, int which) { return g_api->mftb(c, which); }
void dcbz(uint32_t ea) { g_api->dcbz(ea); }
void sc(Ctx* c, uint32_t pc) { g_api->sc(c, pc); }
void unimpl(Ctx* c, uint32_t pc, const char* what) { g_api->unimpl(c, pc, what); }
void dispatch(Ctx* c, uint32_t target) { g_api->dispatch(c, target); }
void trace_call(uint32_t addr) { g_api->trace_call(addr); }
void stub_log(uint32_t addr) { g_api->stub_log(addr); }
void irq_check(Ctx* c) { g_api->irq_check(c); }
void idle_wait() { g_api->idle_wait(); }
