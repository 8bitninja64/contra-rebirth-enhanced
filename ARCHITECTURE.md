# Architecture

How Contra ReBirth Enhanced XD runs, verified against the source at upstream commit `9e2763f`
(darkuni/contra-rebirth-recomp v0.2). Line references are to that tree.

## 1. Pipeline overview

```
your WAD ──tools/prepare_wad.py──▶ game/ (tmd.bin, ticket.bin, content/*.app, main.dol)
                                          │
main.dol + config/symbols.txt + src/hle.txt ──src/recomp.py──▶ src/gen/*.cpp  (generated, never committed)
                                          │
src/runtime/*.cpp + src/gen/*.cpp ──CMake──▶ contrarebirth(.exe)
```

- `prepare_wad.py` decrypts the five WAD contents with libWiiPy and LZ11-decompresses content 1
  into `main.dol`. It checks the DOL against sha1 `825fb4ec04335500ca9e172d05b2e593c5ab7dc9`.
- `recomp.py` turns every function in `config/symbols.txt` (6,957 from dtk) into one C++ function
  `void f_XXXXXXXX(Ctx*)`, 300 per `gen/code_NNN.cpp` (24 files, ~25 MB). It also writes
  `funcs.h` (prototypes) and `table.cpp` (address → function table for indirect calls).

## 2. How PowerPC becomes C++

- Each guest instruction becomes one or more C++ statements over `Ctx` (`runtime/ppc.h`):
  GPRs, FPRs/paired singles, CR, LR, CTR, XER, MSR. Guest memory is a 4 GiB host reservation
  (`reserve_guest()` in `runtime.cpp`), so `R32/W32` are a byte swap plus an offset.
- Branches inside a function are `goto L_<pc>`; jump tables are recovered by scanning for the
  `lis/addi` table base (`recomp.py` ~line 425). CodeWarrior `_savegpr/_restgpr` are inlined.
- Direct calls (`bl`) are emitted as direct C++ calls `f_XXXXXXXX(c)`. Indirect calls (`bctrl`,
  `blrl`) go through `dispatch(c, target)`, which looks the address up in `FN_TABLE`.

**Consequence for enhancements:** a direct call is resolved at C++ compile time, so a host-side
hook on a guest function has to be woven in by the generator (see §10).

## 3. Generated code ↔ runtime

- `src/hle.txt` lists guest functions to replace. `Name` makes the generated body call
  `hle_Name(Ctx*)` (implemented in `runtime/hle_*.cpp`); `Name = value` makes it a stub that sets
  `r3` and returns. This is decided at generation time (`recomp.py` lines 446–453, 474–487).
- `runtime/game.h` is the only Contra-specific runtime file: callback addresses (AX resume,
  decrementer, iosFree, OSExitThread) and r13 (small-data) offsets.
- `guest_call()` runs a guest function from host code on a scratch copy of the CPU context;
  `post_irq()` queues work to run on a guest thread at the next point where interrupts are
  enabled — this is how VI, decrementer and audio interrupts are delivered.
- Hardware registers (`0xCC00xxxx`/`0xCD00xxxx`) are emulated in `runtime.cpp` (MMIO), GX FIFO
  writes are fed to `gx_fifo.cpp`.

## 4. Rendering (GX → OpenGL 3.3)

- `gx_fifo.cpp` decodes the command stream (BP/CP/XF loads, draws, display lists).
- `gx_render.cpp`: vertices are transformed on the CPU (XF matrices, lighting, texgen), TEV stage
  setups become generated GLSL (`gx_shader.cpp`), textures are decoded in `gx_tex.cpp` and cached.
- The EFB is an FBO of logical size 640×528, physically `640·RS × 528·RS` with
  `RS = resolution_scale` (1–4 in the menu, 1–8 in the ini) plus optional MSAA.
- Contra's 2D engine renders the playfield at 320×240 into the EFB, copies it to a 512×256
  texture, then draws that texture onto the 640×480 frame. In 16:9 mode the game itself draws a
  **pillarboxed** 4:3 picture (it asks `SCGetAspectRatio`, which the runtime answers from
  `widescreen`).
- An EFB→XFB copy is a finished frame; `gl_platform_present()` hands it to the platform layer.

## 5. Resolution and aspect ratio (current state)

- Internal resolution: `resolution_scale` multiplies the EFB (§4). Output resolution is just the
  window drawable size; there is no list of output resolutions.
- Aspect: `widescreen=1` → `SCGetAspectRatio` returns 1 (the game lays out 16:9 menus) and the
  presenter letterboxes the frame into a 16:9 rectangle (`gl_platform_sdl.cpp` `present_thread`).
  `widescreen=0` → 4:3. There is no 16:10/21:9 and no true extended playfield.
- Scaling: a single filter — bilinear upscale, box-filter downscale (4×4 taps max).

## 6. Platform layer (SDL2)

- `gl_platform_sdl.cpp` is compiled on Windows and on Linux with `-DUSE_SDL`; Windows loads GL
  through the bundled glad, Linux uses `GL_GLEXT_PROTOTYPES`. `gl_platform_egl.cpp` is the
  headless Linux build (pbuffer, screenshots via `SHOT_EVERY`).
- Two shared GL contexts: the emulation thread renders and blits the XFB into one of three slots
  with a fence; a **present thread** waits on the fence, scales to the window and swaps, so vsync
  never stalls the emulated CPU.
- Input: keyboard state and the first `SDL_GameController` are folded into `HostInput`
  (`hostinput.h`); `hle_input.cpp` turns that into KPAD/WPAD data for the chosen grip (sideways
  remote, remote + nunchuk, classic). Hot-plug is handled (first pad only). Keys are hard-coded.

## 7. Audio

- AX (the game's mixer library) runs recompiled. `hle_audio.cpp` runs an AI thread that fires the
  guest AI-DMA callback every 3 ms; the DSP microcode is replaced by `ax_dsp.cpp`, which walks
  the AX command list and mixes voices on the host, then posts the resume callback with
  `post_irq_front`.
- Mixed 32 kHz stereo goes through `audio_ring.cpp` (60 ms prefill, 200 ms cap) pulled by the
  SDL audio callback. Audio is paced by its own 3 ms clock, not by video.
- Music is BRSTM streamed from content 2 (copied to NAND by the game at boot).

## 8. Saves (NAND)

- `hle_ios.cpp` emulates IOS devices; `/dev/fs` in `hle_fs.cpp` maps NAND paths onto a host folder
  `nand/` **next to the `game` folder** (`nand_init`). The game creates `title/00010001/574b5445/data/`
  (`bak000`, `banner.bin`). `GetUsage` reports real usage so the game's NAND check passes.

## 9. Timing and frame pacing

- `hle_dev.cpp` `vi_thread`: vertical retrace at 59.94 Hz × `speed` (1–8; Tab / R3 for turbo),
  paced with high-resolution waitable timers on Windows (`hostclock.h`). One retrace in flight at
  a time.
- The game logic is driven by retrace, so game speed = retrace rate. Rendering faster than the
  retrace would need interpolation in the game itself; presenting is decoupled (present thread).
- Decrementer (OSAlarm) runs on its own host thread against a 60.75 MHz timebase.
- On non-Windows builds `main()` arms `alarm(TIMEOUT or 15)` — a headless-test guard that kills an
  interactive Linux build after 15 s. Must be limited to the headless build.

## 10. Extension points for enhancements

| Mechanism | Where | Survives regeneration? | Good for |
| --- | --- | --- | --- |
| `hle.txt` full replacement | generator + `runtime/hle_*.cpp` | yes (list is in repo) | SDK functions with known semantics |
| Config + platform layer | `config.*`, `gl_platform_sdl.cpp` | yes | display, scaling, input, vsync |
| GX state interception | `gx_render.cpp` (projection/viewport/scissor) | yes | widescreen, resolution |
| Guest memory patches at boot | after `load_dol()` in `main()` | yes, if DOL hash checked | constant tweaks |
| Generator-woven hooks | `src/hooks.txt` + `runtime/enhancements/` | yes | per-function host overrides switchable at runtime |

### Hooks and the enhancement manager

`src/hooks.txt` lists guest functions (by symbol name). For each one the generator emits the
original body as `o_XXXXXXXX` and a wrapper `f_XXXXXXXX` that calls `g_hook_fn[slot](c, o_X)` when
a hook is installed, else `o_X(c)`. It also writes `gen/hooks.cpp` (slot table: name, address,
original). Functions not listed are generated exactly as before.

`runtime/enhancements/enhancements.{h,cpp}` is the manager. An enhancement is a static
`Enhancement` record (id, name, description, default, `init()`, `active` flag) registered with
`REGISTER_ENHANCEMENT`. At startup `enh_init_all()` hashes `game/main.dol`; only if it is the
supported build (sha1 `825fb4ec…`) are enabled enhancements started. `init()` installs hooks with
`enh_hook("GuestName", fn)`; hooks check their `active` flag and fall through to the original when
off, so toggling at run time is safe. State persists in the ini as `enh_<id> = 0/1`.

Adding one: list the guest function in `src/hooks.txt`, regenerate (`tools/build.ps1` does it),
add `runtime/enhancements/<id>.cpp` modeled on `frame_stats.cpp`. Per-frame work goes through
`enh_on_frame()` (the manager owns the `VIWaitForRetrace` hook). Every enhancement gets an
On/Off row in the F1 menu automatically.

## 10b. Game module, first start and updates

- `src/gen` (generated from the player's `main.dol`) builds as `contrarebirth_game.dll/.so`. Its one
  export, `contra_module_init()`, receives the runtime helpers the code calls (`RuntimeApi`:
  memory-mapped I/O, CPU helpers, dispatch, interrupts, HLE functions by name) and returns the
  function table, hook slots and the DOL's SHA-1 (`GameModule`). Plain C types only: the release
  builds the module with Zig on the player's machine while the runtime is built with MinGW gcc.
  `runtime/module_rt.cpp` is the module side, `module_loader.cpp` the runtime side,
  `hle_registry.cpp` (from `hle.txt` via CMake) maps HLE names to runtime functions.
- First start (`first_start.cpp`): when the game folder or module is missing, or the module's
  stamp differs from `tools/build_inputs.stamp`, a small SDL/ImGui window asks for the WAD and runs
  `tools/make_module.py`, then restarts. Game, module and NAND live in the user directory.
- Updates (`update_check.cpp`, F1 > Update): GitHub's latest release, SHA-256-checked download,
  only changed files unpacked (by `tools/files.sha256`) and swapped with `.old-update` backups,
  restart. The executable has a UTF-8 code page manifest (`app.manifest`) for non-ASCII paths.

## 11. Known game structures (found with the debug tools)

| Address / path | Meaning |
| --- | --- |
| `[802E0138]` (`r13-0x5228`) | game state object, same object across stages |
| `+0x0C/+0x10` f32 | camera / scroll position |
| `+0x24` u32 | area within the stage (map sections) |
| `+0x2C` / `+0x30` / `+0x3C` | P1 score / weapon (0 N, 1 S, 2 L, 3 H) / lives |
| `+0x110` u8 | "no death": player hit test `fn_800492A0` only kills when 0 |
| `+0x04` → player object | vtable `80213B88`; `+0x88` current state handler (member-function triple, `8004A030` on foot, `8004B360` dying); `+0xA8 & 1` killed; `+0xB8` respawn invulnerability timer |
| `fn_80013F00` | boot sequence (strap screen, Konami logo) |
| `fn_80111A70` | tile layer draw; culls against the view rectangle from `fn_800DFC30` |
| `fn_8010DF80` / `fn_8010E260` | CTPObjLayer / MObjLayer: region from the view rectangle; placements entering it are created through the stage's popper (vtable slot 0x1C), leaving it slot 0x18 |
| `[802E0120]` (`r13-0x5240`) | current engine; vtable slot 0x18 camera position, 0x20 `fn_8000A9B0` "is on screen" (pos, margin; 160×120 half size), 0x24 `fn_8000AA80` clamp x to the screen (player) |
| `[802E0178]` (`r13-0x51E8`) | list of all game objects (CTBehave base, `fn_80047210`), next at `+0x0C` |
| Engines | `Engine1A … Engine5C` are stages/halves, plus `EngineDemo/Title/Select/Start/Over/Staff/Debug/Test` |

Render pipeline in gameplay: playfield pass (viewport 320×240, ortho 2/320) → EFB copy 320×240
to a texture → composite pass (640×480 viewport; ortho 854 wide in 16:9 mode, 640 in 4:3).
With true widescreen the playfield pass is 533.33 wide (16:9 plus a 53.33 pad each side), the
copy (534×240) is larger than the game's 512×256 texture and is bound as is, and the composite
samples a 426.67-wide window of it, offset by `g_ws_view` at map edges (`gx_render.cpp`).

## 12. Debug tools (`runtime/debug_tools.cpp`, environment variables)

`MEMDUMP_EVERY/FROM/TO/AT`, `MEMDUMP_MEM2` (RAM snapshots, logged with VI and frame),
`POKE=size:addr=val@from-to`, `WATCH=addr[+off...]` + `WATCH_FROM` (write watchpoint; logs host
stacks, map with `nm`), `DRAWTRACE_TEX` / `DRAWTRACE_FRAME` (call stacks of draws),
`NO_HOST_INPUT`, `AUTOPRESS`, `SHOT_EVERY`, `PRESENT_SHOT`, `FRAMELOG`, `ENH=id=1,...`.
