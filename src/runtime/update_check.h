// Updates from the project's GitHub releases (F1 > Update), ported from the NFL Blitz 2003 port's
// updater. Check: one anonymous HTTPS GET to api.github.com for the latest release; its tag is
// compared with this build's version (CONTRA_VERSION). Install (only when the player chooses it):
// downloads this system's package (ContraReBirthEnhancedXD-windows-x64.zip / -linux-x64.tar.xz),
// checks its SHA-256 against the digest GitHub publishes (no checksum, no install), unpacks it
// with the system's tar and replaces the program files in place. Settings next to the program,
// and the game, module and saves in the user directory, are not touched; when the new version's
// build inputs differ (tools/build_inputs.stamp), the next start rebuilds the game module from
// the unpacked game (first_start.cpp). Replaced files are renamed to *.old-update first (a
// running program's files can be renamed on both systems), restored if anything fails and
// removed at the next start. Then the program restarts.
#pragma once
#include <cstdint>
#include <string>

namespace update {

struct Asset {
    std::string name, url, sha256;
    uint64_t size = 0;
};

struct Release {
    enum State { Idle, Checking, UpToDate, Available, NoReleases, Failed } state = Idle;
    std::string latest;   // the release's tag, e.g. "v0.3"
    std::string error;    // Failed: what went wrong, for the player
    std::string page;     // the release's web page
    std::string notes;    // the release notes (Markdown as written)
    Asset asset;          // this system's package (name empty: the release has none)
};

struct Install {
    enum State { Idle, Downloading, Verifying, Extracting, Installing, Done, Failed } state = Idle;
    float fraction = 0;   // Downloading: 0..1
    std::string error;
};

const char* version();      // this build's version
const char* repository();   // "owner/name" (CONTRA_UPDATE_REPO overrides, for tests)
void check_start();         // background check (ignored while one runs)
Release check_status();
bool version_newer(const std::string& latest, const std::string& current);
// Whether `release` can be installed in place here; `why` says why not (the menu then offers
// the release page).
bool can_install(const Release& release, std::string& why);
void install_start(const Release& release);   // background install
Install install_status();
// Starts the program's executable again with this process's arguments; the caller then quits.
bool restart();
// At startup: removes the files the last install replaced.
void cleanup();
// Opens a web page in the system's browser.
void open_page(const std::string& url);

}  // namespace update
