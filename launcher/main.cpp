// SPDX-License-Identifier: GPL-3.0-only
// Linux desktop frontend using the project's existing SDL3 and Dear ImGui libraries.
#include <SDL3/SDL.h>
#include "kirby/settings.h"
#include "kirby/app/build_info.h"
#include "pages/pages.h"
#include "input_focus.h"
#include "build_probe.h"
#include "setup/job.h"
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
struct Selection { std::mutex mutex; std::string path,error; bool ready=false,pending=false; };
void SourceChosen(void* userdata, const char* const* files, int) {
    std::unique_ptr<std::shared_ptr<Selection>> owner(static_cast<std::shared_ptr<Selection>*>(userdata));
    std::lock_guard lock((*owner)->mutex);
    (*owner)->path=files && files[0] ? files[0] : "";
    (*owner)->error=files ? "" : SDL_GetError();
    (*owner)->ready=true;(*owner)->pending=false;
}
volatile sig_atomic_t sourceCancelled=0;
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
    if (argc==2 && std::string_view(argv[1])=="--build-info") {
        std::cout << Kirby::BuildInformation(Kirby::BuildProgram::Launcher); return 0;
    }
    if (argc==3 && std::string_view(argv[1])=="--check-native-build") {
        try {
            Launcher::BuildProbe probe;
            const auto root=(fs::path(SDL_GetBasePath())/"WiiCompiled").parent_path().parent_path().parent_path();
            probe.Start(argv[2],root/"local/host-libs");
            while (probe.Status()==Launcher::BuildProbeStatus::Checking) { probe.Poll();SDL_Delay(1); }
            if (probe.Status()==Launcher::BuildProbeStatus::Ready) { std::cout << probe.Document();return 0; }
            std::cerr << probe.Error() << '\n';return 1;
        } catch (const std::exception& error) { std::cerr << error.what() << '\n';return 1; }
    }
    if (argc == 3 && std::string_view(argv[1]) == "--print-settings")
        return Kirby::InspectSettings(argv[2], std::cout, std::cerr);
    if (argc == 3 && std::string_view(argv[1]) == "--check-data") {
        std::string status;
        const bool valid = Verify(argv[2], status);
        std::puts(status.c_str()); return valid ? 0 : 1;
    }
    if (argc==3 && std::string_view(argv[1])=="--check-source") {
        const auto root=(fs::path(SDL_GetBasePath())/"WiiCompiled").parent_path().parent_path().parent_path();
        Launcher::Setup::Job job;
        std::signal(SIGINT,[](int) { sourceCancelled=1; });
        std::signal(SIGTERM,[](int) { sourceCancelled=1; });
        job.Start(root/"tools/install.py",argv[2],root/"local/setup");
        while (job.Current().Busy()) {
            if (sourceCancelled) job.Cancel();
            job.Poll();SDL_Delay(1);
        }
        std::cout << job.Current().message << '\n';
        if (job.Current().result) std::cout << job.Current().result->dataRoot.string() << '\n';
        return job.Current().status==Launcher::Setup::Status::Ready ? 0 :
            job.Current().status==Launcher::Setup::Status::Cancelled ? 2 : 1;
    }
    const bool selfTest = argc == 2 && std::string_view(argv[1]) == "--self-test";
    if (argc != 1 && !selfTest) { std::fprintf(stderr,"Usage: kirby-launcher [--self-test | --check-data DATA | --check-source SOURCE | --print-settings TOML | --build-info | --check-native-build EXECUTABLE]\n"); return 2; }
    const fs::path executable = fs::path(SDL_GetBasePath()) / "WiiCompiled";
    const fs::path root = executable.parent_path().parent_path().parent_path();
    if (!fs::exists(root / "tools/kirby.py")) {
        std::fprintf(stderr,"Keep the launcher in this project's build/native folder\n"); return 2;
    }
    Launcher::State state{root, executable, root / "local/user/settings.toml", Kirby::Settings{root / "disc/DATA"}};
    state.selfTest = selfTest;
    state.launcherBuild=Kirby::ParseBuildInformation(Kirby::BuildInformation(Kirby::BuildProgram::Launcher));
    if (selfTest) {
        // Render a fixture record without launching a query or touching user data.
        state.runtimeBuild=state.launcherBuild;state.runtimeBuild->program="runtime";
        state.buildStatus.clear();
    }
    const auto& settings = state.settingsFile;
    auto& config = state.config;
    auto& settingsError = state.settingsError;
    auto& settingsDirty = state.settingsDirty;
    if (!selfTest) {
        try { config = Kirby::LoadOrMigrateSettings(settings, root / "local/user/launcher.cfg", root, config); }
        catch (const std::exception& error) { settingsError = error.what(); }
    }
    auto& data = config.gameData;
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
    SDL_SetWindowMinimumSize(window, 640, 520);
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
    style.Colors[ImGuiCol_Header] = {0.36f,0.15f,0.28f,1};
    style.Colors[ImGuiCol_HeaderHovered] = style.Colors[ImGuiCol_ButtonHovered];
    style.Colors[ImGuiCol_HeaderActive] = style.Colors[ImGuiCol_ButtonActive];
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer); ImGui_ImplSDLRenderer3_Init(renderer);
    auto selection = std::make_shared<Selection>();
    bool running = true; unsigned frames = 0;
    auto& valid = state.dataVerified;
    auto& status = state.dataStatus;
    if (!selfTest) valid = Verify(data, status);
    pid_t game = -1;
    Launcher::Setup::Job setupJob;
    bool setupApplied=false;
    std::string setupSelectionError;
    const auto startInspection=[&](const fs::path& source) {
        if (game>0 || setupJob.Current().Busy() || state.closing) return;
        setupApplied=false;setupSelectionError.clear();
        setupJob.Start(root/"tools/install.py",source,root/"local/setup");
        state.setup=setupJob.Current();
    };
    Launcher::BuildProbe buildProbe;
    const auto refreshBuild=[&] {
        state.runtimeBuild.reset();
        state.checkingBuild=buildProbe.Start(executable,root/"local/host-libs");
        state.buildStatus=state.checkingBuild ? "Checking native executable..." : buildProbe.Error();
    };
    if (!selfTest) refreshBuild();
    auto page = Launcher::Page::Play;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            const bool ownsInput = SDL_GetKeyboardFocus() == window && !(SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED);
            const bool consumed = !selfTest && ownsInput && state.bindingCapture.Handle(event, config.controls, settingsDirty);
            if (!consumed) ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))) {
                state.closing=true;
                if (setupJob.Current().Busy()) setupJob.Cancel();
                else running=false;
            }
        }
        std::string selectedSource,selectionError;
        { std::lock_guard lock(selection->mutex);
          state.choosingSource=selection->pending;
          if (selection->ready) { selectedSource=selection->path;selectionError=selection->error;selection->ready=false; } }
        if (!selectionError.empty()) setupSelectionError="Could not open source chooser: "+selectionError;
        if (!selectedSource.empty() && !selfTest) startInspection(selectedSource);
        if (!selfTest) {
            setupJob.Poll();state.setup=setupJob.Current();
            if (state.setup.status==Launcher::Setup::Status::Ready && !setupApplied && !state.closing) {
                setupApplied=true;
                std::string selectedStatus;
                const auto& selected=state.setup.result->dataRoot;
                if (Verify(selected,selectedStatus)) {
                    if (data!=selected) { data=selected;settingsDirty=true; }
                    valid=true;status=std::move(selectedStatus);
                } else setupSelectionError="Imported data changed before selection. Import again. "+selectedStatus;
            }
            if (!setupSelectionError.empty()) {
                state.setup.status=Launcher::Setup::Status::Failed;
                state.setup.message=setupSelectionError;state.setup.result.reset();
            }
            if (state.closing && !state.setup.Busy()) running=false;
        }
        if (game > 0) {
            int result = 0;
            if (waitpid(game, &result, WNOHANG) == game) {
                game = -1; SDL_ShowWindow(window); SDL_RaiseWindow(window);
                state.gameStatus = WIFEXITED(result) && WEXITSTATUS(result) == 0 ? "Game closed" : "Game stopped with an error. Open Logs for details.";
                refreshBuild();
            }
        }
        if (!selfTest) {
            buildProbe.Poll();
            state.checkingBuild=buildProbe.Status()==Launcher::BuildProbeStatus::Checking;
            if (buildProbe.Status()==Launcher::BuildProbeStatus::Ready && !state.runtimeBuild) {
                state.runtimeBuild=buildProbe.Information();state.buildStatus.clear();
            } else if (buildProbe.Status()==Launcher::BuildProbeStatus::Failed) {
                state.runtimeBuild.reset();state.buildStatus=buildProbe.Error();
            }
        }
        const bool focused = SDL_GetKeyboardFocus() == window &&
            !(SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED);
        state.bindingCapture.Tick(SDL_GetTicks(), focused);
        io.ConfigFlags &= ~(ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard);
        if (!state.bindingCapture.Blocks(Kirby::BindingDevice::Gamepad)) io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        if (!state.bindingCapture.Blocks(Kirby::BindingDevice::Keyboard)) io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui_ImplSDLRenderer3_NewFrame(); ImGui_ImplSDL3_NewFrame();
        Launcher::SynchronizeInputFocus(focused);
        ImGui::NewFrame();
        state.gameRunning = game > 0;
        if (selfTest) {
            // Exercise every page at both layout sizes without file/process actions.
            page = static_cast<Launcher::Page>((frames / 3) % Launcher::PageNames.size());
            if (frames == 0) SDL_SetWindowSize(window, 960, 680);
            if (frames == Launcher::PageNames.size() * 3) SDL_SetWindowSize(window, 640, 520);
        }
        ImGui::BeginDisabled(state.closing);
        const auto action = Launcher::DrawLauncher(state, page);
        ImGui::EndDisabled();
        if (!selfTest && focused && !state.closing) try {
            switch (action) {
            case Launcher::Action::Play:
                if (setupJob.Current().Busy() || state.choosingSource || game>0) break;
                valid = Verify(data, status);
                if (valid && fs::is_regular_file(executable) && saveSettings()) {
                    if (state.checkingBuild) {
                        buildProbe.Cancel();state.checkingBuild=false;
                        state.buildStatus="Refresh build information after the game closes";
                    }
                    const auto libs = (root / "local/host-libs").string();
                    const char* previous = std::getenv("LD_LIBRARY_PATH");
                    const std::string libraryPath = libs + (previous ? std::string(":") + previous : "");
                    setenv("LD_LIBRARY_PATH", libraryPath.c_str(), 1);
                    std::string exe = executable.string(), configArg = settings.string();
                    char* args[]{exe.data(), const_cast<char*>("--video"), const_cast<char*>("--config"), configArg.data(), nullptr};
                    fs::create_directories(root / "build-logs");
                    posix_spawn_file_actions_t actions; posix_spawn_file_actions_init(&actions);
                    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, (root/"build-logs/launcher-game.log").c_str(), O_WRONLY|O_CREAT|O_APPEND, 0600);
                    posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
                    // Existing save paths stay relative to the repository.
                    fs::current_path(root);
                    const int error = posix_spawn(&game, exe.c_str(), &actions, nullptr, args, environ);
                    posix_spawn_file_actions_destroy(&actions);
                    if (error) { game = -1; state.gameStatus = "Could not start the native executable"; }
                    else { state.gameStatus = "Game running"; SDL_MinimizeWindow(window); }
                } else if (valid) state.gameStatus = fs::exists(executable)
                    ? "Could not save settings" : "Native executable missing. See the build guide.";
                break;
            case Launcher::Action::ChooseData:
            case Launcher::Action::ChooseImage:
                if (setupJob.Current().Busy() || state.choosingSource || game>0) break;
                { std::lock_guard lock(selection->mutex);selection->pending=true;state.choosingSource=true; }
                if (action==Launcher::Action::ChooseData)
                    SDL_ShowOpenFolderDialog(SourceChosen,new std::shared_ptr<Selection>(selection),window,data.c_str(),false);
                else {
                    static constexpr SDL_DialogFileFilter filters[]{{"Wii disc images and archives","wbfs;iso;rvz;7z"},{"All files","*"}};
                    SDL_ShowOpenFileDialog(SourceChosen,new std::shared_ptr<Selection>(selection),window,filters,2,nullptr,false);
                }
                break;
            case Launcher::Action::VerifyData: if (!state.choosingSource) startInspection(data);break;
            case Launcher::Action::CancelSetup: setupJob.Cancel();break;
            case Launcher::Action::OpenSetupLogs:
                OpenFolder(state.setup.logPath.empty() ? root/"local/setup/logs" : state.setup.logPath.parent_path());break;
            case Launcher::Action::SaveSettings: saveSettings(); break;
            case Launcher::Action::ReloadSettings:
                try {
                    config = Kirby::LoadOrMigrateSettings(settings, root / "local/user/launcher.cfg", root,
                                                        Kirby::Settings{root / "disc/DATA"});
                    settingsError.clear(); settingsDirty = false; valid = Verify(data, status);
                } catch (const std::exception& error) { settingsError = error.what(); }
                break;
            case Launcher::Action::OpenLogs: OpenFolder(root / "build-logs"); break;
            case Launcher::Action::OpenSaves: OpenFolder(root / "local/user/nand/title/00010000/524b3545/data"); break;
            case Launcher::Action::OpenSettings: OpenFolder(settings.parent_path()); break;
            case Launcher::Action::BuildGuide: SDL_OpenURL(("file://"+(root/"docs/building.md").string()).c_str()); break;
            case Launcher::Action::ShowInstall: page = Launcher::Page::Install; break;
            case Launcher::Action::RefreshBuildInfo: refreshBuild(); break;
            case Launcher::Action::None: break;
            }
        } catch (const std::exception& error) { state.gameStatus = error.what(); }
        ImGui::Render();
        SDL_SetRenderScale(renderer,io.DisplayFramebufferScale.x,io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(renderer,14,15,23,255); SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),renderer); SDL_RenderPresent(renderer);
        if (selfTest && ++frames == Launcher::PageNames.size() * 6) running = false;
        SDL_Delay(5);
    }
    if (settingsDirty && !saveSettings()) std::fprintf(stderr,"Settings: %s\n",settingsError.c_str());
    buildProbe.Cancel();
    ImGui_ImplSDLRenderer3_Shutdown(); ImGui_ImplSDL3_Shutdown(); ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
    return 0;
}
