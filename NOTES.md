# Contra ReBirth (WKTE, WiiWare NTSC-U) - native recomp notes

Static recompilation built on the pipeline first written for Final Fantasy Fables: Chocobo's
Dungeon (Wii): same recompiler, same runtime (threads, IOS, GX->OpenGL, AX mixer, input),
retargeted to this WiiWare title.

## Status
- Boots on the Linux headless build: health screen, Konami logo, NAND check, save creation
  (bak000 + banner.bin), intro demo, title, character select, Stage 1 gameplay renders.
- Windows exe builds (mingw-w64 + SDL2 2.30.9); not yet play-tested on Windows.
- Not done yet: audio check (AX path runs, not listened to), real input testing,
  widescreen presentation, speed check on a real GPU.

## Source title
- WAD: Contra_ReBirth_NTSC-U.wad, sha1 b203a3997311e215724187931aa6783a1e562905
- Title 00010001-574B5445 (WKTE), IOS53, group 0x3434, 5 contents, boot index 4.
- Contents: 0 = banner/meta ("wiicontra.0907222115", build Jul 22 2009),
  1 = main DOL LZ11-compressed (unpacks to 2,410,528 bytes, sha1 825fb4ec04335500ca9e172d05b2e593c5ab7dc9),
  2 = U8 data archive (PSB motion/map/font files, contra_sound.brsar, brstm streams, HomeButton3),
  3 = U8 (HomeButton2 + misc, type 0x8001), 4 = small loader DOL that unpacks content 1.
- tools/prepare_wad.py makes the `game` folder: tmd.bin, ticket.bin, content/<index>.app, main.dol.

## Binary
- Entry 0x80004050, main 0x80007170. .text 0x80006F20-0x801E9F00. dtk 1.8.4: 6,957 functions.
- Game code ~0x80006F20-0x800AB3C0 (M2 engine: Engine1A/Engine5A.., EngineDemo/Title/Select/Start,
  ChipLayer, Motion, BgTask, PSB loader). Libraries above: homebutton (nw4hbm), NW4R SND,
  RVL SDK (Feb 2009, KPAD/WPAD Jun 2009), TMC_JPEG, MSL, TRK.
- No NW4R G3D, no CRI. 2D engine renders at 320x240 into the EFB, copies to a 512x256 texture,
  then draws that onto the 640x480 frame (pillarboxed when SC aspect = 16:9).

## Naming
- tools/match.py (size-sequence match vs ogws/spm/mkw/brawl/ss/xenoblade): 2,280 renames.
  It reads ~/refs/<name>.sym / .spl (symbols.txt / splits.txt copied from those projects).
- tools/xmatch.py (masked-instruction match vs the Chocobo's Dungeon recomp's named DOL, + call propagation):
  1,466 matches; overrides the size-based names where they disagree (it fixed off-by-one names,
  e.g. AIStopDMA was really AIStartDMA). Manual: AIRegisterDMACallback 8017AC80,
  KPADInit 8019E410, KPADInitEx 8019E420.
- config/symbols.dtk.txt = raw dtk output, config/symbols.txt = named.

## Retargeting (runtime/game.h)
- AX resume callback 0x8017CE30 (__AXDSPResumeCallback), DecrementerExceptionCallback 0x80155950,
  iosFree 0x8018B020, OSExitThread 0x8015E5D0, SelectThread idle loop 0x8015E268 (recomp.py),
  interrupt table r13-0x4F70, IOS heap r13-0x54B4. Library boundary for call tracing 0x800AB3C0.

## Runtime changes vs Chocobo
- title.cpp/.h: loads TMD/ticket, content table; TMD view and ticket view builders (Dolphin formats).
- /dev/es: GetTitleID/Dir, TMD/ticket views, stored TMD, content lists, OpenContent (0x09/0x24),
  ReadContent, SeekContent, CloseContent, GetConsumption. ES_LOG=1 logs every call.
- No disc: low memory game ID from title ID + group, IOS version from TMD, dvd_init not called.
- hle_rom.cpp: __OSReadROM serves the IPL font regions (0x1AFF00 SJIS, 0x1FCF00 ANSI) from
  Dolphin's Droid Sans replacement fonts (Apache 2.0, ipl-font-licenses.txt).
- /dev/fs: GetStats reports a half-empty NAND; GetUsage computes real blocks/inodes from the host
  folder (counts the directory itself - NANDCheck subtracts 1 and wrapped to 0 free inodes).
  FS_LOG=1 logs calls.
- KPADReadEx HLE (2009 KPAD signature with error pointer).
- recomp.py: switch-table scan now tracks lis across the whole function (MSL __pformatter keeps
  the table base in r20 from the prologue).
- gx_render.cpp: EFB copies sampled through a larger texture are padded into a texture of the
  sampled size (fixes black screen after the logo). FRAMELOG=1 logs per-frame prims/copies/textures.

## Build / run (Linux headless)
    python3 tools/prepare_wad.py Contra_ReBirth_NTSC-U.wad game
    cd src && python3 recomp.py ../game/main.dol ../config/symbols.txt gen 300
    cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++ && ninja -C build
    SHOT_EVERY=150 SHOT_DIR=shots TIMEOUT=120 VI_HZ=240 ./build/contrarebirth ../game
  Scripted input: AUTOPRESS="A@900-906,PLUS@1300-1306,2@1400-1406" (VI counts).
  Windows: cmake -S . -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=mingw.cmake -DSDL2_DIR=<SDL2>/x86_64-w64-mingw32
  then ninja -C build-win -j1 (see README.md).

## Next
1. Real-time play test of the Windows build (input mapping for sideways remote: 1 = shoot, 2 = jump).
2. Audio: listen to BGM and SFX. The game logs "copy to nand <name>.brstm" for each stream at boot - confirm where those land and that streaming reads them back.
3. Widescreen: present the 4:3 game area without pillarbox or stretch the 16:9 output.
4. Glitch strip at the right edge on the first Stage 1 frame (pad texture edge / filtering).
