// First start / rebuild screen (first_start.cpp): builds the game module from the player's WAD.
#pragma once
#include <string>

// Shows the screen until the module is built (true: restart to play) or the player quits.
// can_reuse_game: the unpacked game is already in the user directory (an update changed the
// game code), so the build starts at once without asking for the WAD.
bool first_start_run(const std::string& reason, bool can_reuse_game);
// Starts this program again with the same arguments (Linux: replaces this process).
bool first_start_restart(int argc, char** argv);
