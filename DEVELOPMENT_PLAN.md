# Development plan

Contra ReBirth Enhanced XD is a fork of [darkuni/contra-rebirth-recomp](https://github.com/darkuni/contra-rebirth-recomp)
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
2. True widescreen: done (enhancement `widescreen`; renderer widens the 320×240 playfield pass,
   copy and composite; tile culling, spawn region and on-screen test widened; 4:3 background
   stretch). Checked on Stages 1–5, including map edges (view clamp), 4:3-only rooms (black bars), the
   Stage 2 elevator ride (played by the owner, recorded) and the Stage 3 sky.
   Todo: bosses of Stages 2–5 by hand, then decide whether it becomes the default.
3. Frame pacing: done to 59.6 game frames/s (see KNOWN_ISSUES). >60 fps would need
   interpolation of the game's own movement; out of scope.

## Phase 6b — cheats (done)
Infinite lives, invincibility (game's no-death flag), always Spread/Laser/Homing; F1 rows.
Level select done (`cheat_stage`); todo: the "AREA n" label still shows the old stage.

## Phase 6 — enhancement system
- built: `src/hooks.txt` → the generator keeps the original as `o_X` and routes `f_X` through a hook slot.
- built: `src/runtime/enhancements/` manager (id, description, default, init, active flag, DOL sha1
  check, `enh_<id>` ini keys, logging) + `frame_stats` diagnostic enhancement as the example.
- todo: settings-menu page for enhancements (Phase 7).

## Phase 7 — settings menu
Done: Dear ImGui menu (Display, Graphics, Audio, Controls, Gameplay, Cheats, About), pauses the
game, pad and keyboard navigation, key rebinding; persisted in `contrarebirth.ini`. Todo: verify
pad rebinding and the Linux build of the menu.

## Phase 8b — releases and updates (done, first tag pending)
Game module split, first-start build from the player's WAD (Windows verified from an unzipped
release in a non-ASCII path; Linux under WSLg), `tools/package.py`, `.github/workflows/release.yml`,
in-game updater (verified v0.3 -> v0.4 against a fake API; bad digest refused). Todo: push the
first tag, check the CI run and the real GitHub update path, Steam Deck test.

## Phase 8 — quality and releases
GitHub Actions build check (runtime only — the generated code needs the user's DOL, so CI
compiles the runtime against a stub table), release layouts that contain no game data, release
notes.

## Session handoff
Next concrete step is always the first **todo** in the lowest phase above. Update this file and
`CHANGELOG.md` with each commit.
