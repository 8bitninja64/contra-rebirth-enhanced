// Enhancement manager: optional, individually switchable changes to the game.
//
// An enhancement is a static Enhancement record registered with REGISTER_ENHANCEMENT in its
// own .cpp under runtime/enhancements/. It may override guest functions listed in
// src/hooks.txt: the generator gives each such function a hook slot, and
// enh_hook("Name", fn) puts a host function in it. The hook receives the original guest
// function, so it can run it, skip it, or change arguments/results around it.
//
// Enhancements are only started when the loaded main.dol is the build they were made for
// (DOL sha1), and their on/off state is stored in the .ini as enh_<id> = 0/1.
#pragma once
#include "ppc.h"

typedef void (*HookFn)(Ctx*, GuestFn orig);

struct Enhancement {
    const char* id;            // ini key suffix, stable: enh_<id>
    const char* name;          // short menu label
    const char* description;   // one line for the menu / log
    bool default_on;
    bool (*init)();            // install hooks, read settings; false = could not start (stays off)
    bool* active;              // set by the manager; hooks check it and fall through to orig when false
};

void enh_register(const Enhancement* e);
#define REGISTER_ENHANCEMENT(rec) static const bool rec##_registered = (enh_register(&rec), true)

// Startup: checks the DOL hash, reads enh_* settings, runs init() of the enabled ones.
void enh_init_all(const char* dol_path);
bool enh_hook(const char* guest_name, HookFn fn);   // false if the name is not in hooks.txt

int enh_count();
const Enhancement* enh_get(int i);
bool enh_enabled(const char* id);
void enh_set_enabled(const char* id, bool on);       // persists via config_save()
bool enh_compatible();                               // DOL matches the supported build
