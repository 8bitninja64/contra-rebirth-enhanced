// Runtime side of the game module boundary (module_abi.h).
#pragma once
#include "module_abi.h"
#include <filesystem>
#include <string>

#ifdef _WIN32
#define CONTRA_MODULE_FILE "contrarebirth_game.dll"
#else
#define CONTRA_MODULE_FILE "contrarebirth_game.so"
#endif

// Loaded from the module (valid after module_load succeeded).
extern const FnEntry* g_fn_table;
extern unsigned g_fn_count;
extern const HookDef* g_hook_defs;
extern unsigned g_hook_count;
extern HookFn* g_hook_slots;
extern std::string g_module_sha1, g_module_path;

// Looks for the module in CONTRA_MODULE (env), next to the executable, then in the user
// directory; false with `error` saying why when none loads.
bool module_load(std::string& error);
std::filesystem::path module_exe_dir();
std::filesystem::path module_user_dir();   // %APPDATA% / XDG data dir, "Contra ReBirth Enhanced XD"
