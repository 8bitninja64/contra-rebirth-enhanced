// The runtime's HLE functions by hle.txt name, for the game module (module_abi.h:
// RuntimeApi::hle). hle_registry.inc is made from hle.txt by CMake (one HLE(Name) per
// replacement that is not a "Name = value" stub), so the runtime builds without any game code.
#include "ppc.h"
#include <cstring>

#define HLE(n) void hle_##n(Ctx*);
#include "hle_registry.inc"
#undef HLE

static const struct { const char* name; GuestFn fn; } kHle[] = {
#define HLE(n) {#n, hle_##n},
#include "hle_registry.inc"
#undef HLE
    {nullptr, nullptr}};

GuestFn hle_lookup(const char* name) {
    for (const auto& e : kHle)
        if (e.name && !strcmp(e.name, name)) return e.fn;
    return nullptr;
}
