# Changelog

## Unreleased (develop)
- Renamed to **Contra ReBirth Enhanced XD** (window title, F1 menu, About, exe properties).
- Enhancement `skip_konami_logo` (on by default): start-up goes straight to the opening
  cinematic, without the strap screen or the Konami logo.
- 16:9 shelved for now: Aspect ratio 16:9 and True widescreen are hidden from the F1 menu and
  the game stays 4:3 (`kOfferWidescreen` in `config.h`; all of the code stays). New scaling
  mode "Stretch to 16:9" (`scaling = stretch169`) stretches the 4:3 picture to a 16:9 area.
- Forked from darkuni/contra-rebirth-recomp v0.2 (`9e2763f`).
- Windows: native build with MSYS2 MinGW-w64 instead of a Linux cross-compiler; `tools/build.ps1`.
- Linux: the windowed SDL build no longer exits after 15 s (the headless test timer now applies
  only to the EGL build, or when `TIMEOUT` is set).
- Linux: the windowed SDL build compiles (it never had before); `tools/contrarebirth.sh` launcher.
- Enhancement system: `src/hooks.txt` + generator hook slots + `runtime/enhancements/` manager;
  `frame_stats` diagnostic enhancement.
- Perf log (`PERF=1`): `[vi] retraces dropped` counter.
- Display: `scaling` = fit / integer (multiples of 240 lines) / stretch; `filter` = smooth / sharp
  (sharp bilinear) / nearest; `fullscreen_mode` = borderless / exclusive; `resolution` = auto /
  native / WxH (window size, exclusive display mode). All in the F1 menu and the ini.
- Debug: `PRESENT_SHOT=<file> PRESENT_SHOT_AT=<n>` saves the presented window image.
- Controllers: every connected pad is used; `gamecontrollerdb.txt` (SDL_GameControllerDB) is
  loaded from next to the exe; unknown pads get a generic layout instead of being ignored
  (fixes the 8BitDo Ultimate 2 over Bluetooth doing nothing).
- Enhancement `skip_strap_screen` (on by default): start-up goes straight to the Konami logo.
- The automatic window size now fits inside the desktop (it was 1708x960 regardless).
- Cheats (F1 menu, off by default): infinite lives, invincibility (the game's own no-death
  flag; pits still kill), always Spread / Laser / Homing.
- RE/test tools: RAM snapshots, POKE, write watchpoint, draw call-stack tracing, NO_HOST_INPUT.
- True widescreen (enhancement `widescreen`, needs 16:9): widened playfield pass, copy and
  composite, plus widened view rectangle for tile culling. Enemies and item carriers spawn at
  the 16:9 edge (object-layer spawn region and the engines' on-screen test count the 16:9
  width); a backmost layer that only covers 4:3 (Stage 2 sky and vortex) is stretched to 16:9.
- Widescreen map edges: the playfield is rendered 53 units wider on each side and the
  presented 16:9 window slides toward the level art where the map ends (Stage 1 boss room,
  Stage 5 start), with spawns and the on-screen test following it; the game's camera is not
  touched. Only an edge that holds still slides the picture. 4:3-only rooms (vertical Stage 4)
  keep black side bars; backgrounds that end inside the view while scrolling (Stage 2 elevator
  skyline) are continued with a copy of themselves; the Stage 3 truck sky (one screen-fixed
  picture repeated side by side) is stretched instead of repeated.
- Test switch `START_ENGINE=1A..5C` (with `cheat_stage`): start at any stage half.
- F1 menu rebuilt on Dear ImGui in the style of the TenchuZ / Blitz ports: Display, Graphics,
  Audio (volume, mute), Controls (key rebinding), Gameplay, Cheats, About; pauses the game.
- Level select cheat (`cheat_stage` + `start_stage`): a new game starts at the chosen stage.
- `spawn_log` diagnostic enhancement: logs game objects as they appear and vanish.
- Docs: `ARCHITECTURE.md`, `BUILDING.md`, `DEVELOPMENT_PLAN.md`, `KNOWN_ISSUES.md`.
- `.gitignore`: decrypted WAD contents, `dist/`, AppImages.
