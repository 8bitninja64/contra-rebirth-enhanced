# Development plan

Contra ReBirth Enhanced is a fork of [darkuni/contra-rebirth-recomp](https://github.com/darkuni/contra-rebirth-recomp)
(MIT). Goal: a polished native Windows and SteamOS/Linux release that keeps the original gameplay
and adds modern display options and a maintainable enhancement layer.

Status legend: **done** = implemented and verified as described; **built** = compiles, not yet
verified in play; **todo**.

## Phase 1 — repository
- done: fork `8bitninja64/contra-rebirth-enhanced`, `origin` = fork, `upstream` = darkuni, branch `develop`.
- done: `.gitignore` covers WADs, DOLs, decrypted contents, generated code, builds, `dist/`, saves.
- done: `ARCHITECTURE.md`, `BUILDING.md`, `CHANGELOG.md`, `KNOWN_ISSUES.md`, this file.

## Phase 2 — audit
- done: `ARCHITECTURE.md` (pipeline, recompiler, HLE, GX→GL, SDL, audio, NAND, timing, extension points).

## Phase 3 — Windows
- done: native MSYS2 MinGW-w64 (GCC, posix threads) build; `tools/build.ps1` one-command build.
- todo: interactive verification — boot, video, audio by ear, keyboard + pad, save/load, stages 1–3+.

## Phase 4 — Linux / SteamOS
- done: interactive SDL builds no longer self-terminate after 15 s (headless-test `alarm`).
- todo: native SDL build (`-DUSE_SDL=ON`), tested on desktop Linux (WSLg first), then Steam Deck
  Gaming Mode. Launch script, then AppImage.

## Phase 5 — graphics
Order (each step verified before the next):
1. Output-side options in the presenter only (no game changes): aspect 4:3/16:9/16:10/21:9
   presentation rectangle, scaling modes (integer, nearest, linear, aspect-fit, stretch),
   display mode (windowed / borderless / exclusive), resolution list + native + custom, vsync.
2. True widescreen investigation: the 2D playfield is 320×240 rendered into the EFB then
   copied to a 512×256 texture. Find the M2 engine camera/scroll width, layer culling and spawn
   bounds; widen the copy/projection. Document every glitch; 4:3 stays the default fallback.
3. Frame pacing: 59.94 Hz retrace-driven logic. 60 FPS is native; >60 FPS would need
   interpolation in game code and is out of scope unless a safe path is found.

## Phase 6 — enhancement system
- `src/hooks.txt` → generator weaves a runtime-switchable hook prologue into listed functions.
- `src/runtime/enhancements/`: manager (id, description, enabled, config, DOL-hash compatibility,
  init with error handling, logging) + one file per patch.

## Phase 7 — settings menu
Extend `overlay.cpp` into Graphics / Controls / Audio / Enhancements / System pages, keyboard and
pad navigable; persisted in `contrarebirth.ini`.

## Phase 8 — quality and releases
GitHub Actions build check (runtime only — the generated code needs the user's DOL, so CI
compiles the runtime against a stub table), release layouts that contain no game data, release
notes.

## Session handoff
Next concrete step is always the first **todo** in the lowest phase above. Update this file and
`CHANGELOG.md` with each commit.
