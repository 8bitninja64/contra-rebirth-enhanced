# Building

You need your own copy of **Contra ReBirth (USA)** for WiiWare (title `WKTE`) as a `.wad`. The
build checks that the executable inside is the one this recompilation targets (DOL sha1
`825fb4ec04335500ca9e172d05b2e593c5ab7dc9`; the known-good WAD has sha1
`b203a3997311e215724187931aa6783a1e562905`). No game data is in this repository.

## Windows (MSYS2 MinGW-w64)

Prerequisites:
- Python 3.11+ and `pip install libWiiPy`
- CMake 3.16+
- [MSYS2](https://www.msys2.org/) with, from an MSYS2 shell:
  `pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2 mingw-w64-x86_64-ninja`

One command (first time, with your WAD):

```powershell
powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Wad "C:\path\to\Contra ReBirth (USA) (WiiWare).wad"
```

Later builds: `tools\build.ps1` (reuses `game\`). Options: `-Msys <root>` (default
`$env:MSYS2_ROOT`, `..\..\_Tools\msys64`, then `C:\msys64`), `-Jobs N` (default 6; each generated
file needs about 1.5 GB of RAM while compiling).

Output: `dist\contrarebirth.exe` + `SDL2.dll`. libstdc++, libgcc and winpthread are linked
statically. Run it from the repository (it finds `..\game`) or put a `game` folder next to it.
Saves go to `nand\` next to `game\`; settings to `contrarebirth.ini` next to the exe.

Why MinGW and not MSVC: the runtime uses GCC builtins (`__builtin_bswap*`, `__builtin_clz`),
POSIX-style threading and the CMake file passes GCC flags; it has only ever been built with
GCC/Clang. An MSVC (or clang-cl) port is possible but untested.

## Linux (native, windowed)

Packages (Debian/Ubuntu): `cmake ninja-build g++ libsdl2-dev libgl-dev`; plus Python 3.11+ with
`pip install libWiiPy`.

```sh
python3 tools/prepare_wad.py "Contra ReBirth (USA) (WiiWare).wad" game
cd src
python3 recomp.py ../game/main.dol ../config/symbols.txt gen 300
cmake -S . -B build-linux -G Ninja -DUSE_SDL=ON
ninja -C build-linux -j4
./build-linux/contrarebirth ../game
```

`tools/contrarebirth.sh` is the launcher for an installed copy (puts a bundled `lib/` on
`LD_LIBRARY_PATH`, runs from its own folder). For Steam Deck: copy the exe, the launcher and your
`game` folder into one directory, add the launcher to Steam as a non-Steam game, and pick the
"Gamepad" controller template.

Without `-DUSE_SDL=ON` you get the headless EGL test build from upstream (screenshots via
`SHOT_EVERY`/`SHOT_DIR`, stops after `TIMEOUT` seconds, default 15).

## Releases (no game code)

The recompiled game code builds as its own library, `contrarebirth_game.dll` / `.so`
(`src/runtime/module_abi.h`), so a release ships the runtime alone. On first start the runtime
(`src/runtime/first_start.cpp`) runs `tools/make_module.py` on the player's WAD: libWiiPy from PyPI,
the WAD unpacked into the user directory, `recomp.py`, then the bundled Zig compiles the module.

```sh
python tools/package.py --zip                       # Windows: dist-release/ContraReBirthEnhancedXD-windows-x64.zip
python3 tools/package.py --zip --bin src/build-linux  # Linux: ...-linux-x64.tar.xz
```

`package.py` downloads Zig 0.17.0, the embeddable Python 3.12.10 (Windows) and pip, each pinned by
SHA-256 (`ZIG_DIR=<unpacked zig>` uses a local Zig), trims Zig to the one target, writes
`tools/build_inputs.stamp` (when it changes, the next start rebuilds the module) and
`tools/files.sha256` (the updater installs only changed files), and refuses any game-derived file.

Publishing: push a tag `vX.Y` (or run the Release workflow by hand). `.github/workflows/release.yml`
builds both runtimes with `CONTRA_VERSION` = the tag, packages them and attaches the archives to
the GitHub release; GitHub's asset digests are what the in-game updater checks.

Test switches: `CONTRA_USER_DIR` (user directory), `CONTRA_FIRST_START_WAD` (first start picks
this WAD), `CONTRA_MODULE` (module path), `CONTRA_UPDATE_API=http://127.0.0.1:PORT` (fake GitHub),
`CONTRA_UPDATE_AUTOINSTALL=1`, `START_ENGINE=1A..5C` (with `cheat_stage`).
