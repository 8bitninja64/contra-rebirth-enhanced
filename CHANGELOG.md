# Changelog

## Unreleased (develop)
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
- Docs: `ARCHITECTURE.md`, `BUILDING.md`, `DEVELOPMENT_PLAN.md`, `KNOWN_ISSUES.md`.
- `.gitignore`: decrypted WAD contents, `dist/`, AppImages.
