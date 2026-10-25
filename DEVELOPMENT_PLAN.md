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
- done (scripted: AUTOPRESS + screenshots + WAV capture): boot, health/Konami screens, save creation
  (`nand/.../bak000`, `banner.bin`), save read on the next launch, title, story, character select,
  Stage 1 movement/jump/shoot; audio continuous in real time; ~58–60 fps on an RTX 3070 laptop.
- todo (needs a person): audio by ear, real keyboard and controller play, Stages 2+.

## Phase 4 — Linux / SteamOS
- done: interactive SDL builds no longer self-terminate after 15 s (headless-test `alarm`).
- done: native SDL build (`-DUSE_SDL=ON`) compiles and runs windowed under WSLg (Ubuntu 26.04):
  boots to the intro, reads the save, game logic at 60 VI/s, audio without underruns.
- done: `tools/contrarebirth.sh` launcher.
- todo: real Linux desktop and Steam Deck Gaming Mode (WSLg presents through D3D12 at ~22 fps,
  which says nothing about real hardware); bundle SDL2; AppImage.

## Phase 5 — graphics
Order (each step verified before the next):
1. Output-side options in the presenter only (no game changes).
   - done (verified by window captures at 1000×700): scaling fit / integer / stretch, filters
     smooth / sharp / nearest, resolution auto / native / WxH for the window.
   - built, untested: exclusive fullscreen at a chosen resolution (needs a person at the screen).
   - 16:10 / 21:9 only make sense after step 2; the game's picture is 4:3 or pillarboxed 16:9.
2. True widescreen investigation: the 2D playfield is 320×240 rendered into the EFB then
   copied to a 512×256 texture. Find the M2 engine camera/scroll width, layer culling and spawn
   bounds; widen the copy/projection. Document every glitch; 4:3 stays the default fallback.
3. Frame pacing: 59.94 Hz retrace-driven logic. 60 FPS is native; >60 FPS would need
   interpolation in game code and is out of scope unless a safe path is found.

## Phase 6 — enhancement system
- built: `src/hooks.txt` → the generator keeps the original as `o_X` and routes `f_X` through a hook slot.
- built: `src/runtime/enhancements/` manager (id, description, default, init, active flag, DOL sha1
  check, `enh_<id>` ini keys, logging) + `frame_stats` diagnostic enhancement as the example.
- todo: settings-menu page for enhancements (Phase 7).

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
