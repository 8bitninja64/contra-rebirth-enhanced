# Known issues

Inherited from upstream v0.2 unless marked otherwise. Update when an issue is confirmed or fixed.

| Area | Issue | Status |
| --- | --- | --- |
| Video | In 16:9 mode the game draws a pillarboxed 4:3 picture; no true widescreen yet. | open |
| Video | Glitch strip at the right edge on the first Stage 1 frame (upstream NOTES). | unverified |
| Video | Only 4:3 and 16:9 presentation; no 16:10 / 21:9; one scaling filter (bilinear). | open |
| Input | Keyboard and pad bindings are hard-coded; only the first controller is used. | open |
| Audio | Not yet confirmed by ear on any platform. | open |
| Linux | Windowed SDL build not yet tested; Steam Deck not tested. | open |
| Build | MSVC is not supported; Windows builds use MSYS2 MinGW-w64. | by design |
| Build | The generated code (`src/gen`) is derived from the game executable and cannot be redistributed, so CI can only build the runtime. | by design |
