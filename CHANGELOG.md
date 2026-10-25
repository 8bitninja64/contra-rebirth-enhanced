# Changelog

## Unreleased (develop)
- Forked from darkuni/contra-rebirth-recomp v0.2 (`9e2763f`).
- Windows: native build with MSYS2 MinGW-w64 instead of a Linux cross-compiler; `tools/build.ps1`.
- Linux: the windowed SDL build no longer exits after 15 s (the headless test timer now applies
  only to the EGL build, or when `TIMEOUT` is set).
- Docs: `ARCHITECTURE.md`, `BUILDING.md`, `DEVELOPMENT_PLAN.md`, `KNOWN_ISSUES.md`.
- `.gitignore`: decrypted WAD contents, `dist/`, AppImages.
