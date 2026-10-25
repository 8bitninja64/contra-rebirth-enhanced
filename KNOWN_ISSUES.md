# Known issues

Inherited from upstream v0.2 unless marked otherwise. Update when an issue is confirmed or fixed.

| Area | Issue | Status |
| --- | --- | --- |
| Video | In 16:9 mode the game draws a pillarboxed 4:3 picture; no true widescreen yet. | open |
| Video | Glitch strip at the right edge on the first Stage 1 frame (upstream NOTES). | unverified |
| Video | Only the game's own 4:3 / 16:9 (pillarboxed) picture; 16:10 and 21:9 need true widescreen first (stretching is available but is not widescreen). | open |
| Input | Keyboard and pad bindings are hard-coded; only the first controller is used. | open |
| Audio | Not yet confirmed by ear on any platform. | open |
| Linux | Windowed SDL build verified only under WSLg (presents ~22 fps there; game logic and audio run at full speed). Real desktop Linux and Steam Deck untested. | open |
| Timing | Windows (RTX 3070 laptop, no capture): retraces 59.0–60.0/s (avg ≈59.45, target 59.94; ~0.6/s dropped while the previous one is still undelivered), and the game completes only ~57–58 frames/s (`frame_stats`), so it runs ~3% slow. Screenshot capture (`SHOT_EVERY`) makes it worse (58.5). | investigating |
| Video | The game itself scales its 320×240 playfield to 640×480 with bilinear filtering, so the frame we receive is already soft; "Sharp"/"Nearest" output filters cannot undo that. Candidate enhancement: force point sampling on that texture draw. | open |
| Video | Exclusive fullscreen and display-mode switching are implemented but untested (not exercised automatically, it would change the desktop mode). | open |
| Build | MSVC is not supported; Windows builds use MSYS2 MinGW-w64. | by design |
| Build | The generated code (`src/gen`) is derived from the game executable and cannot be redistributed, so CI can only build the runtime. | by design |
