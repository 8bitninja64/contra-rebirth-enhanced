// Cheats (all off by default). Each one is an enhancement, so it is switched in the F1 menu
// and stored in the .ini as enh_cheat_<name>. They are applied once per game frame
// (enh_on_frame) through the game's own state pointer, never a fixed heap address.
//
// Found by RAM scanning (debug_tools.cpp MEMDUMP/POKE) and confirmed in the generated code:
//   [0x802E0138] (r13-0x5228)  game state object, loaded by ~870 places in the game code
//     +0x2C  u32  player 1 score
//     +0x30  u32  player 1 weapon: 0 N (normal), 1 S (spread), 2 L (laser), 3 H (homing)
//     +0x3C  u32  player 1 lives ("REST"; the HUD shows lives-1). Lowered by fn_80009B80
//                 (found with the WATCH write watchpoint).
//     +0x110 u8   "no death" flag: the player hitbox test fn_800492A0 only sets the killed
//                 bit (player +0xA8 & 1) when this is 0; read in 4 more places. Falling into
//                 a pit sets the killed bit elsewhere (fn_80048840) and still kills.
//     +0x04  ptr  player 1 object (vtable 80213B88): +0xB8 respawn invulnerability timer,
//                 +0x88 current state handler (8004A030 on foot, 8004B360 dying)
#include "enhancements.h"
#include "hle.h"

static const uint32_t kGameState = 0x802E0138u;

static bool valid(uint32_t p) { return p >= 0x80000000u && p < 0x81800000u; }

// apply() runs every frame while on; undo() once when the cheat is switched off.
struct Cheat { bool active; void (*apply)(uint32_t state); void (*undo)(uint32_t state); bool was_on; };

static void lives(uint32_t st) {
    if (R32(st + 0x3C) < 3) W32(st + 0x3C, 3);
}
static void invincible(uint32_t st) { W8(st + 0x110, 1); }
static void spread(uint32_t st) { W32(st + 0x30, 1); }
static void laser(uint32_t st) { W32(st + 0x30, 2); }
static void homing(uint32_t st) { W32(st + 0x30, 3); }
static void invincible_off(uint32_t st) { W8(st + 0x110, 0); }

static Cheat g_lives = {false, lives, nullptr, false}, g_inv = {false, invincible, invincible_off, false};
static Cheat g_spread = {false, spread, nullptr, false}, g_laser = {false, laser, nullptr, false},
             g_homing = {false, homing, nullptr, false};
static Cheat* const g_all[] = {&g_lives, &g_inv, &g_spread, &g_laser, &g_homing};

void cheats_apply() {
    uint32_t st = R32(kGameState);
    if (!valid(st)) return;
    for (Cheat* ch : g_all) {
        if (ch->active) ch->apply(st);
        else if (ch->was_on && ch->undo) ch->undo(st);
        ch->was_on = ch->active;
    }
}

static bool init_any() {
    static bool registered = false;
    if (!registered) { enh_on_frame(cheats_apply); registered = true; }
    return true;
}

static const Enhancement cheat_lives = {
    "cheat_lives", "Cheat: infinite lives", "Lives never drop below 3", false, init_any, &g_lives.active};
static const Enhancement cheat_invincible = {
    "cheat_invincible", "Cheat: invincibility", "Enemies and shots cannot kill you (pits still can)", false,
    init_any, &g_inv.active};
static const Enhancement cheat_spread = {
    "cheat_spread", "Cheat: always Spread gun", "Weapon S, whatever you pick up", false, init_any, &g_spread.active};
static const Enhancement cheat_laser = {
    "cheat_laser", "Cheat: always Laser", "Weapon L, whatever you pick up", false, init_any, &g_laser.active};
static const Enhancement cheat_homing = {
    "cheat_homing", "Cheat: always Homing", "Weapon H, whatever you pick up", false, init_any, &g_homing.active};
REGISTER_ENHANCEMENT(cheat_lives);
REGISTER_ENHANCEMENT(cheat_invincible);
REGISTER_ENHANCEMENT(cheat_spread);
REGISTER_ENHANCEMENT(cheat_laser);
REGISTER_ENHANCEMENT(cheat_homing);
