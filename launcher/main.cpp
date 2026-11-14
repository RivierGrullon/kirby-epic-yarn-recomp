// SPDX-License-Identifier: GPL-3.0-only
// Linux desktop frontend using the project's existing SDL3 and Dear ImGui libraries.
#include <SDL3/SDL.h>
#include "kirby/settings.h"
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <spawn.h>
#include <string>
#include <string_view>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
namespace fs = std::filesystem;
namespace {
constexpr char ExpectedSha1[] = "f4280a2b0709bfde4132be22e9f7fa8ecfa2a130";
struct Selection { std::mutex mutex; std::string path; bool ready = false; };
void FolderChosen(void* userdata, const char* const* files, int) {
    std::unique_ptr<std::shared_ptr<Selection>> owner(static_cast<std::shared_ptr<Selection>*>(userdata));
    if (files && files[0]) {
        std::lock_guard lock((*owner)->mutex);
        (*owner)->path = files[0]; (*owner)->ready = true;
    }
}
bool Verify(const fs::path& data, std::string& status) {
    for (const char* entry : {"sys/main.dol", "sys/boot.bin", "sys/fst.bin", "ticket.bin", "tmd.bin", "files"}) {
        if (!fs::exists(data / entry)) { status = std::string("Missing ") + entry; return false; }
    }
    int pipefd[2];
    if (pipe(pipefd)) { status = "Could not open verification pipe"; return false; }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipefd[0]);
    posix_spawn_file_actions_addclose(&actions, pipefd[1]);
    std::string dol = (data / "sys/main.dol").string();
    char* args[]{const_cast<char*>("sha1sum"), dol.data(), nullptr};
    pid_t child = -1;
    const int error = posix_spawnp(&child, "sha1sum", &actions, nullptr, args, environ);
    posix_spawn_file_actions_destroy(&actions); close(pipefd[1]);
    std::string digest; std::array<char, 1024> buffer{};
    if (!error) for (;;) {
        const auto count = read(pipefd[0], buffer.data(), buffer.size());
        if (count > 0) digest.append(buffer.data(), count);
        else if (count < 0 && errno == EINTR) continue;
        else break;
    }
    close(pipefd[0]);
    int result = -1;
    if (!error) while (waitpid(child, &result, 0) < 0 && errno == EINTR) {}
    const bool valid = !error && WIFEXITED(result) && WEXITSTATUS(result) == 0 &&
        digest.starts_with(std::string(ExpectedSha1) + " ");
    status = valid ? "RK5E01 Rev 0 (USA) verified" : "Wrong revision or SHA-1 verification failed";
    return valid;
}
void OpenFolder(const fs::path& path) {
    fs::create_directories(path);
    SDL_OpenURL(("file://" + path.string()).c_str());
}
}

int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "--print-settings")
        return Kirby::InspectSettings(argv[2], std::cout, std::cerr);
    if (argc == 3 && std::string_view(argv[1]) == "--check-data") {
        std::string status;
        const bool valid = Verify(argv[2], status);
        std::puts(status.c_str()); return valid ? 0 : 1;
    }
    const bool selfTest = argc == 2 && std::string_view(argv[1]) == "--self-test";
    if (argc != 1 && !selfTest) { std::fprintf(stderr,"Usage: kirby-launcher [--self-test | --check-data DATA | --print-settings TOML]\n"); return 2; }
    const fs::path executable = fs::path(SDL_GetBasePath()) / "WiiCompiled";
    const fs::path root = executable.parent_path().parent_path().parent_path();
    if (!fs::exists(root / "tools/kirby.py")) {
        std::fprintf(stderr,"Keep the launcher in this project's build/native folder\n"); return 2;
    }
    const auto settings = root / "local/user/settings.toml";
    Kirby::Settings config{root / "disc/DATA"};
    std::string settingsError;
    bool settingsDirty = false;
    if (!selfTest) {
        try { config = Kirby::LoadOrMigrateSettings(settings, root / "local/user/launcher.cfg", root, config); }
        catch (const std::exception& error) { settingsError = error.what(); }
    }
    auto& data = config.gameData;
    auto& volume = config.masterVolume;
    auto& vsync = config.vsync;
    auto& performanceMode = config.gpuResidentCopies;
    const auto saveSettings = [&] {
        if (selfTest || !settingsError.empty()) return false;
        try { Kirby::SaveSettings(settings, config); settingsDirty = false; return true; }
        catch (const std::exception& error) { settingsError = error.what(); return false; }
    };
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) { std::fprintf(stderr,"%s\n",SDL_GetError()); return 1; }
    SDL_Window* window = SDL_CreateWindow("Kirby - native launcher", 960, 680,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | (selfTest ? SDL_WINDOW_HIDDEN : 0));
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
    if (!renderer) { std::fprintf(stderr,"%s\n",SDL_GetError()); SDL_Quit(); return 1; }
    SDL_SetRenderVSync(renderer, 1);
    IMGUI_CHECKVERSION(); ImGui::CreateContext();
    auto& io = ImGui::GetIO(); io.IniFilename = nullptr;
    io.FontGlobalScale = 1.15f;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowRounding = 10; style.FrameRounding = 7; style.ChildRounding = 10;
    style.FramePadding = {12, 9}; style.ItemSpacing = {12, 12}; style.WindowPadding = {20,20};
    style.Colors[ImGuiCol_WindowBg] = {0.055f,0.06f,0.09f,1};
    style.Colors[ImGuiCol_ChildBg] = {0.085f,0.09f,0.13f,1};
    style.Colors[ImGuiCol_Button] = {0.52f,0.20f,0.38f,1};
    style.Colors[ImGuiCol_ButtonHovered] = {0.68f,0.28f,0.48f,1};
    style.Colors[ImGuiCol_ButtonActive] = {0.78f,0.35f,0.55f,1};
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer); ImGui_ImplSDLRenderer3_Init(renderer);
    auto selection = std::make_shared<Selection>();
    bool running = true, valid = false; unsigned frames = 0;
    std::string status = "Select your extracted DATA folder";
    if (!selfTest) valid = Verify(data, status);
    pid_t game = -1;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))) running = false;
        }
        { std::lock_guard lock(selection->mutex);
          if (selection->ready) { data = selection->path; selection->ready = false; settingsDirty = true; valid = Verify(data,status); } }
        if (game > 0) {
            int result = 0;
            if (waitpid(game, &result, WNOHANG) == game) {
                game = -1; SDL_ShowWindow(window); SDL_RaiseWindow(window);
                status = WIFEXITED(result) && WEXITSTATUS(result) == 0 ? "Game closed" : "Game stopped with an error. Open Logs for details.";
            }
        }
        ImGui_ImplSDLRenderer3_NewFrame(); ImGui_ImplSDL3_NewFrame(); ImGui::NewFrame();
        ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Launcher", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextColored({1,0.66f,0.80f,1}, "KIRBY"); ImGui::SameLine(); ImGui::TextDisabled("NATIVE / LINUX");
        ImGui::TextUnformatted("Kirby's Epic Yarn"); ImGui::Separator();
        ImGui::BeginChild("play", {0,155}, ImGuiChildFlags_Borders);
        ImGui::TextUnformatted("Play your own copy"); ImGui::TextWrapped("%s",status.c_str());
        ImGui::BeginDisabled(!valid || game > 0 || selfTest || !settingsError.empty());
        if (ImGui::Button("Play", {170,42})) {
            valid = Verify(data,status);
            if (valid && fs::exists(executable) && saveSettings()) {
                const auto libs = (root / "local/host-libs").string();
                const char* previous = std::getenv("LD_LIBRARY_PATH");
                const std::string libraryPath = libs + (previous ? std::string(":") + previous : "");
                setenv("LD_LIBRARY_PATH", libraryPath.c_str(), 1);
                std::string exe = executable.string(), configArg = settings.string();
                std::vector<char*> args{exe.data(),const_cast<char*>("--video"),const_cast<char*>("--config"),configArg.data()};
                args.push_back(nullptr);
                fs::create_directories(root / "build-logs");
                posix_spawn_file_actions_t actions; posix_spawn_file_actions_init(&actions);
                posix_spawn_file_actions_addopen(&actions,STDOUT_FILENO,(root/"build-logs/launcher-game.log").c_str(),O_WRONLY|O_CREAT|O_APPEND,0600);
                posix_spawn_file_actions_adddup2(&actions,STDOUT_FILENO,STDERR_FILENO);
                // The development build keeps its established save paths relative to the repository.
                fs::current_path(root);
                const int error = posix_spawn(&game,exe.c_str(),&actions,nullptr,args.data(),environ);
                posix_spawn_file_actions_destroy(&actions);
                if (error) { game = -1; status = "Could not start the native executable"; }
                else { status = "Game running"; SDL_MinimizeWindow(window); }
            } else if (valid) status = fs::exists(executable) ? "Could not save settings" : "Native executable missing. See the build guide.";
        }
        ImGui::EndDisabled(); ImGui::SameLine();
        if (ImGui::Button("Choose DATA folder", {200,42}))
            SDL_ShowOpenFolderDialog(FolderChosen,new std::shared_ptr<Selection>(selection),window,data.c_str(),false);
        ImGui::EndChild();
        ImGui::TextDisabled("Game data"); ImGui::TextWrapped("%s",data.c_str());
        ImGui::Separator();
        ImGui::BeginDisabled(!settingsError.empty() || selfTest);
        settingsDirty |= ImGui::SliderFloat("Master volume", &volume, 0, 100, "%.0f%%");
        ImGui::TextDisabled("Applied next launch. In-game audio is still under investigation.");
        settingsDirty |= ImGui::Checkbox("VSync", &vsync);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Off can reduce presentation stalls; driver behavior varies.");
        settingsDirty |= ImGui::Checkbox("Experimental performance mode", &performanceMode);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reduces GPU copy waits and repeated texture processing. Compatibility testing is ongoing. Applied next launch.");
        if (ImGui::Button("Save settings")) saveSettings();
        ImGui::SameLine(); ImGui::TextDisabled("%s",settingsDirty ? "Unsaved changes" : "Settings saved on Play or close");
        ImGui::EndDisabled();
        if (!settingsError.empty()) {
            ImGui::TextWrapped("Settings error: %s. Original file preserved.",settingsError.c_str());
            if (ImGui::Button("Open Settings Folder")) OpenFolder(settings.parent_path());
            ImGui::SameLine();
            if (ImGui::Button("Reload settings")) {
                try {
                    config = Kirby::LoadOrMigrateSettings(settings, root / "local/user/launcher.cfg", root,
                                                        Kirby::Settings{root / "disc/DATA"});
                    settingsError.clear(); settingsDirty = false; valid = Verify(data,status);
                } catch (const std::exception& error) { settingsError = error.what(); }
            }
        }
        ImGui::TextWrapped("Display: native resolution. Stable 30 / 60 FPS modes are not validated yet.");
        ImGui::TextWrapped("Controls: D-pad to move, south / cross to jump, east / circle for yarn, Start for +.");
        int count = 0; SDL_JoystickID* controllers = SDL_GetGamepads(&count);
        ImGui::Text("Controllers detected: %d",count);
        for (int i=0;i<count;++i) ImGui::TextDisabled("%s",SDL_GetGamepadNameForID(controllers[i]));
        SDL_free(controllers);
        if (ImGui::Button("Open Logs")) OpenFolder(root / "build-logs");
        ImGui::SameLine(); if (ImGui::Button("Open Saves")) OpenFolder(root / "local/user/nand/title/00010000/524b3545/data");
        ImGui::SameLine(); if (ImGui::Button("Build Guide")) SDL_OpenURL(("file://"+(root/"docs/building.md").string()).c_str());
        ImGui::TextDisabled("Development build | WiiCompiled + Aurora | GPL-3.0-only / third-party licenses preserved");
        ImGui::End(); ImGui::Render();
        SDL_SetRenderScale(renderer,io.DisplayFramebufferScale.x,io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(renderer,14,15,23,255); SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),renderer); SDL_RenderPresent(renderer);
        if (selfTest && ++frames == 5) running = false;
        SDL_Delay(5);
    }
    if (settingsDirty && !saveSettings()) std::fprintf(stderr,"Settings: %s\n",settingsError.c_str());
    ImGui_ImplSDLRenderer3_Shutdown(); ImGui_ImplSDL3_Shutdown(); ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
    return 0;
}
