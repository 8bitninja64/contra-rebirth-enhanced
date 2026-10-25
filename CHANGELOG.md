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
- Docs: `ARCHITECTURE.md`, `BUILDING.md`, `DEVELOPMENT_PLAN.md`, `KNOWN_ISSUES.md`.
- `.gitignore`: decrypted WAD contents, `dist/`, AppImages.
