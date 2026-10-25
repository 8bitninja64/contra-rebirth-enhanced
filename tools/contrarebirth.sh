#!/bin/sh
# Launcher for the native Linux build (desktop and Steam Deck Gaming Mode).
# Usage: contrarebirth.sh [path to game folder]
# Without an argument the game looks for a "game" folder next to the executable.
here="$(cd "$(dirname "$0")" && pwd)"
# Bundled libraries (SDL2) next to the executable win over the system ones
export LD_LIBRARY_PATH="$here/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# Steam Input presents the Deck controls as a standard gamepad; let SDL use it in the background too
export SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1
cd "$here" || exit 1
exec "$here/contrarebirth" "$@"
