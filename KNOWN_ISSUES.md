# Known issues

Inherited from upstream v0.2 unless marked otherwise. Update when an issue is confirmed or fixed.

| Area | Issue | Status |
| --- | --- | --- |
| Video | True widescreen (F1 → "True widescreen", needs Aspect 16:9; off by default): checked on all five stages by autoplay. Scrolling sections are correct and enemies spawn at the 16:9 edge (same spawn counts as 4:3). At map edges (Stage 1 boss room, Stage 5 start, area starts) the picture slides toward the level art (view clamp, as in the Hangtime port); rooms that are only 4:3 wide (Stage 4 skull room and descent shaft) get their sides filled from the nearest edge column, softened and darkened, since no art exists there. The player is still clamped to the 4:3 area (fn_8000AA80). | in progress |
| Video | Glitch strip at the right edge on the first Stage 1 frame (upstream NOTES). | unverified |
| Video | Only the game's own 4:3 / 16:9 (pillarboxed) picture; 16:10 and 21:9 need true widescreen first (stretching is available but is not widescreen). | open |
| Input | Keyboard and pad bindings are hard-coded; only the first controller is used. | open |
| Audio | Not yet confirmed by ear on any platform. | open |
| Linux | Windowed SDL build verified only under WSLg (presents ~22 fps there; game logic and audio run at full speed). Real desktop Linux and Steam Deck untested. | open |
| Timing | Fixed most of it: game ran on the integrated GPU, the SDL event pump on the emulation thread stalled 18–35 ms, and the FIFO re-parsed draws per byte. Now 59.6 game frames/s average in Stage 1 (RTX 3070 laptop), retraces at exactly 59.94; about 1–2 frames per 5 s still miss a retrace because of GL driver time inside draws. | open (minor) |
| Timing | Higher than 60 fps is not offered: game logic, animation and audio advance once per retrace; more frames would only repeat images unless the game's own movement were interpolated. | by design |
| Video | The game itself scales its 320×240 playfield to 640×480 with bilinear filtering, so the frame we receive is already soft; "Sharp"/"Nearest" output filters cannot undo that. Candidate enhancement: force point sampling on that texture draw. | open |
| Video | Exclusive fullscreen and display-mode switching are implemented but untested (not exercised automatically, it would change the desktop mode). | open |
| Build | MSVC is not supported; Windows builds use MSYS2 MinGW-w64. | by design |
| Build | The generated code (`src/gen`) is derived from the game executable and cannot be redistributed, so CI can only build the runtime. | by design |
