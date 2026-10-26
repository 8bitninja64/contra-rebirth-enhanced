// Skip the Wii Remote wrist-strap screen shown before the Konami logo.
//
// fn_80013F00 is the boot sequence (IPL) state machine, state in this+0x74:
//   2: starts the strap_en / strap_fr / strap_sp motion (by console language)
//   4: waits until that motion ends, or until a button is newly pressed
//      (input object at this+0x8C: hold at +4, previous hold at +8, mask 0x0C0B)
//   5: starts konami_logo
// Two steps, both undone right after the original call:
//   state 2: the motion names live in 12-byte slots at 0x8020C5C8/D4/E0 ("strap_en",
//            "strap_fr", "strap_sp") and 0x8020C5EC ("konami_logo"); the strap slots are
//            overwritten with "konami_logo" so the strap image is never drawn
//   state 4: the game lets a button press end the strap wait, so A is presented as
//            newly pressed and it moves straight on to its own Konami logo state
#include "enhancements.h"
#include "hle.h"
#include <cstring>

static bool g_active = false;
static const uint32_t kStrapSlots[3] = {0x8020C5C8u, 0x8020C5D4u, 0x8020C5E0u};
static const uint32_t kKonamiSlot = 0x8020C5ECu;

static bool slot_is(uint32_t a, const char* s) {
    for (size_t i = 0; i <= strlen(s); i++)
        if (R8(a + (uint32_t)i) != (uint8_t)s[i]) return false;
    return true;
}

// skip_konami_logo: the sequence ends in state 6 once the logo has faded out, by setting the
// fade value (this+0x70) to its end constant and this+0x08 = 2 (finished), after which the
// game moves on to the opening cinematic. Doing that as soon as state 2 would start the first
// motion leaves out the strap screen and the Konami logo alike.
static bool g_skip_logo = false;
static const uint32_t kFadeEnd = 0x802E8BA0u - 0x7F2Cu;   // r2-0x7F2C, the f32 state 6 stores at the end

static void hook_boot_sequence(Ctx* c, GuestFn orig) {
    const uint32_t self = c->r[3];
    if (g_skip_logo && R32(self + 0x74) == 2) {
        WF32(self + 0x70, RF32(kFadeEnd));
        W32(self + 0x08, 2);
        return;
    }
    const uint32_t state = g_active ? R32(self + 0x74) : 0xFFFFFFFFu;
    if (state == 2 && slot_is(kKonamiSlot, "konami_logo")) {
        uint8_t saved[3][12];
        for (int s = 0; s < 3; s++)
            for (int i = 0; i < 12; i++) { saved[s][i] = R8(kStrapSlots[s] + i); W8(kStrapSlots[s] + i, R8(kKonamiSlot + i)); }
        orig(c);
        for (int s = 0; s < 3; s++)
            for (int i = 0; i < 12; i++) W8(kStrapSlots[s] + i, saved[s][i]);
        return;
    }
    if (state != 4) { orig(c); return; }
    const uint32_t in = R32(self + 0x8C);
    if (!in) { orig(c); return; }
    const uint32_t hold = R32(in + 4), prev = R32(in + 8);
    W32(in + 4, hold | 0x0800u);    // WPAD A held now...
    W32(in + 8, prev & ~0x0800u);   // ...and not before: a new press
    orig(c);
    W32(in + 4, hold);
    W32(in + 8, prev);
}

static bool init() {
    if (!slot_is(kStrapSlots[0], "strap_en") || !slot_is(kKonamiSlot, "konami_logo")) return false;   // not the expected data
    return enh_hook("fn_80013F00", hook_boot_sequence);   // shared by both; a second install is harmless
}

static const Enhancement skip_strap_screen = {
    "skip_strap_screen", "Skip strap screen", "Go straight to the Konami logo at start-up", true, init, &g_active};
REGISTER_ENHANCEMENT(skip_strap_screen);
static const Enhancement skip_konami_logo = {
    "skip_konami_logo", "Skip Konami logo", "Start with the opening cinematic (no strap screen, no Konami logo)", true,
    init, &g_skip_logo};
REGISTER_ENHANCEMENT(skip_konami_logo);
