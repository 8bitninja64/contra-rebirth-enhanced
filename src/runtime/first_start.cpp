// First start (and rebuild after an update): the release ships no game code, so before the
// game can run, tools/make_module.py turns the player's WAD into the game folder and the game
// module (runtime/module_abi.h) in the user directory. This is its own small window, ahead of
// the game's: the player picks the WAD (files found nearby are offered, so a pad is enough on
// a Steam Deck), then the build runs in a child process while its progress is shown.
#include "first_start.h"
#include "module_loader.h"
#include "overlay.h"
#include "game.h"
#include "hle.h"
#include <SDL.h>
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#else
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

fs::path tools_dir() {   // release: tools/ beside the exe; repository: <repo>/tools
    const fs::path exe = module_exe_dir();
    std::error_code ec;
    if (fs::exists(exe / "tools" / "make_module.py", ec)) return exe / "tools";
    return exe.parent_path().parent_path() / "tools";
}

fs::path python_exe() {
#ifdef _WIN32
    const fs::path bundled = module_exe_dir() / "tools" / "python" / "python.exe";
    std::error_code ec;
    return fs::exists(bundled, ec) ? bundled : fs::path(L"python.exe");
#else
    return "python3";
#endif
}

std::string lower(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

std::vector<fs::path> find_wads() {
    std::vector<fs::path> dirs = {module_exe_dir(), module_exe_dir().parent_path(), module_user_dir()};
#ifdef _WIN32
    if (const char* h = getenv("USERPROFILE")) dirs.insert(dirs.end(), {fs::u8path(h) / "Downloads", fs::u8path(h) / "Desktop", fs::u8path(h) / "Documents"});
#else
    if (const char* h = getenv("HOME")) dirs.insert(dirs.end(), {fs::path(h) / "Downloads", fs::path(h) / "Desktop", fs::path(h), fs::path(h) / "Documents"});
#endif
    std::vector<fs::path> out;
    for (const fs::path& d : dirs) {
        std::error_code ec;
        for (auto it = fs::directory_iterator(d, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
            if (!it->is_regular_file(ec) || lower(it->path().extension().u8string()) != ".wad") continue;
            if (std::find(out.begin(), out.end(), it->path()) == out.end()) out.push_back(it->path());
            if (out.size() >= 12) return out;
        }
    }
    return out;
}

fs::path browse_for_wad() {
#ifdef _WIN32
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFilter = L"Wii WAD (*.wad)\0*.wad\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Choose your Contra ReBirth WAD";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&ofn) ? fs::path(file) : fs::path();
#else
    for (const char* cmd : {"zenity --file-selection --title='Choose your Contra ReBirth WAD' --file-filter='*.wad *.WAD' 2>/dev/null",
                            "kdialog --getopenfilename ~ '*.wad *.WAD' 2>/dev/null"}) {
        FILE* p = popen((std::string("env -u LD_PRELOAD ") + cmd).c_str(), "r");
        if (!p) continue;
        char buf[4096] = {};
        const bool got = fgets(buf, sizeof buf, p) != nullptr;
        const int rc = pclose(p);
        if (got && rc == 0) {
            std::string s = buf;
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            if (!s.empty()) return s;
        }
        if (WIFEXITED(rc) && WEXITSTATUS(rc) != 127) break;   // the dialog ran and was cancelled
    }
    return {};
#endif
}

// make_module.py in a child process; its output lines are collected for the screen and the log.
struct Build {
    std::mutex mx;
    std::vector<std::string> lines;
    std::atomic<bool> running{false};
    std::atomic<int> exit_code{-1};
    std::thread reader;
#ifdef _WIN32
    HANDLE process = nullptr;
#else
    pid_t pid = -1;
#endif

    void add(const std::string& line) {
        std::lock_guard<std::mutex> lk(mx);
        lines.push_back(line);
    }

    bool start(const fs::path& wad, std::string& error) {
        const fs::path script = tools_dir() / "make_module.py", user = module_user_dir();
        std::error_code ec;
        fs::create_directories(user, ec);
        { std::lock_guard<std::mutex> lk(mx); lines.clear(); }
        exit_code = -1;
#ifdef _WIN32
        SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
        HANDLE rd = nullptr, wr = nullptr;
        if (!CreatePipe(&rd, &wr, &sa, 0)) { error = "could not create a pipe"; return false; }
        SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
        auto q = [](const std::wstring& s) { return L"\"" + s + L"\""; };
        std::wstring cmd = q(python_exe().wstring()) + L" -I -u " + q(script.wstring()) + L" " +
                           (wad.empty() ? std::wstring(L"-") : q(wad.wstring())) + L" " + q(user.wstring());
        STARTUPINFOW si{};
        si.cb = sizeof si;
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = wr; si.hStdError = wr; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        PROCESS_INFORMATION pi{};
        SetEnvironmentVariableW(L"PYTHONIOENCODING", L"utf-8");
        if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            CloseHandle(rd); CloseHandle(wr);
            error = "could not start Python (" + python_exe().u8string() + ")";
            return false;
        }
        CloseHandle(wr);
        CloseHandle(pi.hThread);
        process = pi.hProcess;
        running = true;
        reader = std::thread([this, rd] {
            std::string part;
            char buf[4096];
            DWORD got = 0;
            while (ReadFile(rd, buf, sizeof buf, &got, nullptr) && got) {
                part.append(buf, got);
                for (size_t nl; (nl = part.find('\n')) != std::string::npos; part.erase(0, nl + 1)) {
                    std::string l = part.substr(0, nl);
                    if (!l.empty() && l.back() == '\r') l.pop_back();
                    add(l);
                }
            }
            if (!part.empty()) add(part);
            CloseHandle(rd);
            WaitForSingleObject(process, INFINITE);
            DWORD code = 1;
            GetExitCodeProcess(process, &code);
            exit_code = (int)code;
            running = false;
        });
        return true;
#else
        int fds[2];
        if (pipe(fds)) { error = "could not create a pipe"; return false; }
        const std::string py = python_exe().string(), sc = script.string(), w = wad.empty() ? "-" : wad.string(), u = user.string();
        std::vector<char*> argv = {(char*)py.c_str(), (char*)"-I", (char*)"-u", (char*)sc.c_str(), (char*)w.c_str(), (char*)u.c_str(), nullptr};
        pid = fork();
        if (pid == 0) {
            setpgid(0, 0);
            dup2(fds[1], 1); dup2(fds[1], 2);
            close(fds[0]); close(fds[1]);
            unsetenv("LD_PRELOAD");   // Steam's overlay
            execvp(argv[0], argv.data());
            fprintf(stderr, "error: could not run %s\n", argv[0]);
            _exit(127);
        }
        close(fds[1]);
        if (pid < 0) { close(fds[0]); error = "could not start Python"; return false; }
        running = true;
        reader = std::thread([this, fd = fds[0]] {
            std::string part;
            char buf[4096];
            for (ssize_t n; (n = read(fd, buf, sizeof buf)) > 0;) {
                part.append(buf, (size_t)n);
                for (size_t nl; (nl = part.find('\n')) != std::string::npos; part.erase(0, nl + 1)) add(part.substr(0, nl));
            }
            if (!part.empty()) add(part);
            close(fd);
            int st = 0;
            waitpid(pid, &st, 0);
            exit_code = WIFEXITED(st) ? WEXITSTATUS(st) : 1;
            running = false;
        });
        return true;
#endif
    }

    void cancel() {
#ifdef _WIN32
        if (running && process) TerminateProcess(process, 1);
#else
        if (running && pid > 0) kill(-pid, SIGTERM);
#endif
    }

    void finish() {
        if (reader.joinable()) reader.join();
#ifdef _WIN32
        if (process) { CloseHandle(process); process = nullptr; }
#endif
        std::lock_guard<std::mutex> lk(mx);
        std::ofstream log(module_user_dir() / "module_build.log", std::ios::trunc);
        for (const std::string& l : lines) log << l << "\n";
    }
};

}  // namespace

bool first_start_run(const std::string& reason, bool can_reuse_game) {
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) return false;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window* win = SDL_CreateWindow(PORT_NAME, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1100, 640,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) return false;
    SDL_GLContext gl = SDL_GL_CreateContext(win);
    SDL_GL_SetSwapInterval(1);
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i)) SDL_GameControllerOpen(i);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    overlay_apply_theme();
    ImGui_ImplSDL2_InitForOpenGL(win, gl);
    ImGui_ImplOpenGL3_Init("#version 330");

    enum class State { Choose, Building, Failed, Done } state = State::Choose;
    Build build;
    std::vector<fs::path> wads = find_wads();
    std::string error;
    fs::path chosen;
    auto start = [&](const fs::path& wad) {
        chosen = wad;
        error.clear();
        state = build.start(wad, error) ? State::Building : State::Failed;
    };
    if (can_reuse_game) start({});   // an update changed the game code: rebuild from the unpacked game
    else if (const char* t = getenv("CONTRA_FIRST_START_WAD"); t && *t) start(fs::u8path(t));   // tests

    bool quit = false;
    while (!quit && state != State::Done) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) quit = true;
            if (e.type == SDL_CONTROLLERDEVICEADDED) SDL_GameControllerOpen(e.cdevice.which);
        }
        if (state == State::Building && !build.running) {
            build.finish();
            if (build.exit_code == 0) state = State::Done;
            else {
                std::lock_guard<std::mutex> lk(build.mx);
                error = "The game code could not be built.";
                for (auto it = build.lines.rbegin(); it != build.lines.rend(); ++it)
                    if (it->rfind("error:", 0) == 0) { error = it->substr(7); break; }
                state = State::Failed;
            }
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        const ImVec2 size = io.DisplaySize;
        ImGui::GetIO().FontGlobalScale = std::clamp(size.y / 1400.f, 0.45f, 1.0f);
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(size);
        ImGui::Begin("##first", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        ImGui::PushFont(overlay_title_font());
        ImGui::TextUnformatted("CONTRA REBIRTH ENHANCED XD");
        ImGui::PopFont();
        ImGui::TextDisabled("%s", reason.c_str());
        ImGui::Separator();
        if (state == State::Choose) {
            ImGui::TextWrapped("This port runs your own copy of Contra ReBirth (WiiWare, USA). Choose its .wad file: the game code "
                               "is then built on this computer, once (a few minutes). Nothing is downloaded but the WAD tool libWiiPy.");
            ImGui::Spacing();
            if (!wads.empty()) {
                ImGui::TextUnformatted("WAD files found:");
                for (size_t i = 0; i < wads.size(); i++) {
                    if (i == 0) ImGui::SetItemDefaultFocus();
                    if (ImGui::Button((wads[i].filename().u8string() + "##" + std::to_string(i)).c_str())) start(wads[i]);
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", wads[i].parent_path().u8string().c_str());
                }
                ImGui::Spacing();
            }
            if (ImGui::Button("Browse for the WAD...")) {
                const fs::path p = browse_for_wad();
                if (!p.empty()) start(p);
            }
            ImGui::SameLine();
            if (ImGui::Button("Quit")) quit = true;
        } else if (state == State::Building) {
            std::string current = "Starting", last;
            float frac = 0.02f;
            {
                std::lock_guard<std::mutex> lk(build.mx);
                int stepn = 0, steps = 5;
                for (const std::string& l : build.lines) {
                    int a = 0, b = 0;
                    if (sscanf(l.c_str(), "# step %d/%d", &a, &b) == 2) { stepn = a; steps = b; current = l.substr(l.find(' ', 7) + 1); }
                    else if (sscanf(l.c_str(), "compiled %d/%d", &a, &b) == 2 && b) frac = (stepn - 1 + (float)a / b) / steps;
                    last = l;
                }
                if (stepn) frac = std::max(frac, (stepn - 1) / (float)steps);
            }
            ImGui::TextUnformatted(current.c_str());
            ImGui::ProgressBar(frac, ImVec2(-1, 0));
            ImGui::TextDisabled("%s", last.c_str());
            ImGui::Spacing();
            if (ImGui::Button("Cancel")) build.cancel();
        } else if (state == State::Failed) {
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1), "%s", error.c_str());
            ImGui::TextDisabled("The full output is in %s", (module_user_dir() / "module_build.log").u8string().c_str());
            {
                std::lock_guard<std::mutex> lk(build.mx);
                ImGui::BeginChild("##log", ImVec2(0, size.y * 0.45f), true);
                const size_t from = build.lines.size() > 40 ? build.lines.size() - 40 : 0;
                for (size_t i = from; i < build.lines.size(); i++) ImGui::TextUnformatted(build.lines[i].c_str());
                ImGui::EndChild();
            }
            if (ImGui::Button("Try again")) { wads = find_wads(); state = State::Choose; }
            ImGui::SameLine();
            if (ImGui::Button("Quit")) quit = true;
        }
        ImGui::End();
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(win);
    }
    if (state == State::Building) { build.cancel(); build.finish(); }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(win);
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
    return state == State::Done;
}

bool first_start_restart(int argc, char** argv) {
#ifdef _WIN32
    (void)argc; (void)argv;
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = GetCommandLineW();
    if (!CreateProcessW(exe, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#else
    const std::string self = (module_exe_dir() / GAME_EXE).string();
    std::vector<char*> args = {(char*)self.c_str()};
    for (int i = 1; i < argc; i++) args.push_back(argv[i]);
    args.push_back(nullptr);
    execv(self.c_str(), args.data());
    return false;
#endif
}
