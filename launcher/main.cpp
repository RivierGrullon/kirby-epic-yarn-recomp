// SPDX-License-Identifier: GPL-3.0-only
// Native desktop frontend using the project's existing SDL3 and Dear ImGui libraries.
#include <SDL3/SDL.h>
#include "kirby/settings.h"
#include "kirby/boot/validate_input.h"
#include "kirby/platform/command_line.h"
#include "kirby/app/build_info.h"
#include "pages/pages.h"
#include "input_focus.h"
#include "build_probe.h"
#include "platform.h"
#include <cryptopp/sha.h>
#include "setup/job.h"
#include "setup/installation.h"
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
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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
#ifdef _WIN32
    try {
        Kirby::ValidateWindowsInput(data);
        status = "RK5E01 Rev 0 (USA) verified"; return true;
    } catch (const std::exception& error) { status = error.what(); return false; }
#else
    std::ifstream input(data/"sys/main.dol",std::ios::binary);
    CryptoPP::SHA1 hash;std::array<char,65536> buffer{};
    while (input) {
        input.read(buffer.data(),buffer.size());
        hash.Update(reinterpret_cast<const CryptoPP::byte*>(buffer.data()),input.gcount());
    }
    std::array<CryptoPP::byte,CryptoPP::SHA1::DIGESTSIZE> bytes{};hash.Final(bytes.data());
    constexpr char hex[]="0123456789abcdef";std::string digest;
    for (const auto byte : bytes) { digest+=hex[byte>>4];digest+=hex[byte&15]; }
    const bool valid=input.eof() && digest==ExpectedSha1;
    status = valid ? "RK5E01 Rev 0 (USA) verified" : "Wrong revision or SHA-1 verification failed";
    return valid;
#endif
}
std::string LocalUrl(const fs::path& path) {
    const auto generic=fs::absolute(path).generic_u8string();
    std::string url="file://";
#ifdef _WIN32
    if (!generic.starts_with(u8"/")) url+='/' ;
#endif
    constexpr char hex[]="0123456789ABCDEF";
    for (const unsigned char character : generic) {
        if ((character>='a' && character<='z') || (character>='A' && character<='Z') ||
            (character>='0' && character<='9') || std::string_view("/-._~:").find(character)!=std::string_view::npos)
            url+=character;
        else { url+='%';url+=hex[character>>4];url+=hex[character&15]; }
    }
    return url;
}
void OpenFolder(const fs::path& path) {
    fs::create_directories(path);SDL_OpenURL(LocalUrl(path).c_str());
}
#ifdef _WIN32
using GameProcess = void;
GameProcess* StartGame(const fs::path& executable,const fs::path& settings,const fs::path& root) {
    fs::create_directories(root / "build-logs");
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE log = CreateFileW((root / "build-logs/launcher-game.log").c_str(), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open game log");
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = startup.hStdError = log;
    startup.hStdInput = nullptr;
    PROCESS_INFORMATION child{};
    std::wstring command = L"\"" + executable.wstring() + L"\" --video --config \"" + settings.wstring() + L"\"";
    const bool started = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, root.c_str(), &startup, &child);
    CloseHandle(log);
    if (!started) { SDL_SetError("Could not start the native executable"); return nullptr; }
    CloseHandle(child.hThread);
    return child.hProcess;
}
bool PollGame(GameProcess* game, int* result) {
    DWORD status = 0;
    if (GetExitCodeProcess(game, &status) && status == STILL_ACTIVE) return false;
    *result = static_cast<int>(status);
    return true;
}
void CloseGame(GameProcess* game) { CloseHandle(game); }
#else
using GameProcess = SDL_Process;
GameProcess* StartGame(const fs::path& executable,const fs::path& settings,const fs::path& root) {
    fs::create_directories(root/"build-logs");
    const auto logPath=Launcher::Utf8(root/"build-logs/launcher-game.log");
    auto* log=SDL_IOFromFile(logPath.c_str(),"ab");
    const auto properties=SDL_CreateProperties();auto* environment=SDL_CreateEnvironment(true);
    const auto exe=Launcher::Utf8(executable),config=Launcher::Utf8(settings),directory=Launcher::Utf8(root);
    const char* args[]{exe.c_str(),"--video","--config",config.c_str(),nullptr};
    bool configured=log && properties && environment;
#ifndef _WIN32
    if (configured) {
        const char* previous=SDL_GetEnvironmentVariable(environment,"LD_LIBRARY_PATH");
        const auto libraries=Launcher::Utf8(root/"local/host-libs")+(previous ? std::string(":")+previous : "");
        configured=SDL_SetEnvironmentVariable(environment,"LD_LIBRARY_PATH",libraries.c_str(),true);
    }
#endif
    configured=configured && SDL_SetPointerProperty(properties,SDL_PROP_PROCESS_CREATE_ARGS_POINTER,args) &&
        SDL_SetPointerProperty(properties,SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER,environment) &&
        SDL_SetStringProperty(properties,SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING,directory.c_str()) &&
        SDL_SetNumberProperty(properties,SDL_PROP_PROCESS_CREATE_STDIN_NUMBER,SDL_PROCESS_STDIO_NULL) &&
        SDL_SetNumberProperty(properties,SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,SDL_PROCESS_STDIO_REDIRECT) &&
        SDL_SetPointerProperty(properties,SDL_PROP_PROCESS_CREATE_STDOUT_POINTER,log) &&
        SDL_SetBooleanProperty(properties,SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN,true);
    auto* process=configured ? SDL_CreateProcessWithProperties(properties) : nullptr;
    if (log) SDL_CloseIO(log);
    if (properties) SDL_DestroyProperties(properties);
    if (environment) SDL_DestroyEnvironment(environment);
    return process;
}
bool PollGame(GameProcess* game, int* result) { return SDL_WaitProcess(game, false, result); }
void CloseGame(GameProcess* game) { SDL_DestroyProcess(game); }
#endif
}

int main(int argc, char** argv) {
    Kirby::CommandLineArguments arguments;
    try { arguments.Normalize(argc,argv); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n';return 2; }
    if (argc==2 && std::string_view(argv[1])=="--build-info") {
        std::cout << Kirby::BuildInformation(Kirby::BuildProgram::Launcher); return 0;
    }
    if (argc==3 && std::string_view(argv[1])=="--check-native-build") {
        try {
            Launcher::BuildProbe probe;
            const auto root=(Launcher::FromUtf8(SDL_GetBasePath())/Launcher::GameExecutable).parent_path().parent_path().parent_path();
            probe.Start(Launcher::FromUtf8(argv[2]),root/"local/host-libs");
            while (probe.Status()==Launcher::BuildProbeStatus::Checking) { probe.Poll();SDL_Delay(1); }
            if (probe.Status()==Launcher::BuildProbeStatus::Ready) { std::cout << probe.Document();return 0; }
            std::cerr << probe.Error() << '\n';return 1;
        } catch (const std::exception& error) { std::cerr << error.what() << '\n';return 1; }
    }
    if (argc == 3 && std::string_view(argv[1]) == "--print-settings")
        return Kirby::InspectSettings(Kirby::CommandLinePath(argv[2]), std::cout, std::cerr);
    if (argc == 3 && std::string_view(argv[1]) == "--check-data") {
        std::string status;
        const bool valid = Verify(Launcher::FromUtf8(argv[2]), status);
        std::puts(status.c_str()); return valid ? 0 : 1;
    }
    if (argc==3 && std::string_view(argv[1])=="--check-source") {
        const auto root=(Launcher::FromUtf8(SDL_GetBasePath())/Launcher::GameExecutable).parent_path().parent_path().parent_path();
        Launcher::Setup::Job job;
        std::signal(SIGINT,[](int) { sourceCancelled=1; });
        std::signal(SIGTERM,[](int) { sourceCancelled=1; });
        const auto worker=Launcher::FindSetupWorker(root);
        job.Start(worker.file,Launcher::FromUtf8(argv[2]),root/"local/setup",worker.interpreter);
        while (job.Current().Busy()) {
            if (sourceCancelled) job.Cancel();
            job.Poll();SDL_Delay(1);
        }
        std::cout << job.Current().message << '\n';
        if (job.Current().result) std::cout << Launcher::Utf8(job.Current().result->dataRoot) << '\n';
        return job.Current().status==Launcher::Setup::Status::Ready ? 0 :
            job.Current().status==Launcher::Setup::Status::Cancelled ? 2 : 1;
    }
    const bool selfTest = argc == 2 && std::string_view(argv[1]) == "--self-test";
    if (argc != 1 && !selfTest) { std::fprintf(stderr,"Usage: kirby-launcher [--self-test | --check-data DATA | --check-source SOURCE | --print-settings TOML | --build-info | --check-native-build EXECUTABLE]\n"); return 2; }
    const fs::path developerExecutable = Launcher::FromUtf8(SDL_GetBasePath()) / Launcher::GameExecutable;
    const fs::path root = developerExecutable.parent_path().parent_path().parent_path();
    if (!fs::exists(root / "tools/kirby.py")) {
        std::fprintf(stderr,"Keep the launcher in this project's build/native or build/windows folder\n"); return 2;
    }
    Launcher::State state{root, developerExecutable, root / "local/user/settings.toml", Kirby::Settings{root / "disc/DATA"}};
    auto& executable=state.executable;
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
        try {
            if (const auto installed=Launcher::Setup::ReadInstallation(root)) {
                executable=installed->executable;state.installationId=installed->identifier;
                if (!fs::exists(settings)) config.gameData=installed->dataRoot;
            }
        } catch (const std::exception& error) {
            state.installationError=error.what();
        }
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
    GameProcess* game=nullptr;
    Launcher::Setup::Job setupJob;
    bool setupApplied=false;
    std::string setupSelectionError;
    const auto startSetup=[&](const fs::path& source,Launcher::Setup::Operation operation=Launcher::Setup::Operation::Inspect) {
        if (game!=nullptr || setupJob.Current().Busy() || state.closing) return;
        setupApplied=false;setupSelectionError.clear();
        const auto worker=Launcher::FindSetupWorker(root);
        setupJob.Start(worker.file,source,root/"local/setup",worker.interpreter,operation);
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
        if (!selectedSource.empty() && !selfTest) {
            try { startSetup(Launcher::FromUtf8(selectedSource),Launcher::Setup::Operation::Install); }
            catch (const std::exception& error) { setupSelectionError=error.what(); }
        }
        if (!selfTest) {
            setupJob.Poll();state.setup=setupJob.Current();
            if (state.setup.status==Launcher::Setup::Status::Ready && !setupApplied && !state.closing) {
                setupApplied=true;
                try {
                std::string selectedStatus;
                const auto& selected=state.setup.installation ? state.setup.installation->dataRoot : state.setup.result->dataRoot;
                if (Verify(selected,selectedStatus)) {
                    if (state.setup.installation) {
                        const auto installed=Launcher::Setup::ReadInstallation(root);
                        if (!installed || installed->identifier!=state.setup.installation->identifier)
                            throw std::runtime_error("Installation selection changed before the launcher verified it");
                        executable=installed->executable;state.installationId=installed->identifier;
                        state.installationError.clear();refreshBuild();
                    }
                    if (data!=selected) { data=selected;settingsDirty=true; }
                    valid=true;status=std::move(selectedStatus);
                    if (state.setup.installation && settingsDirty) saveSettings();
                } else setupSelectionError="Imported data changed before selection. Import again. "+selectedStatus;
                } catch (const std::exception& error) { setupSelectionError=error.what(); }
            }
            if (!setupSelectionError.empty()) {
                state.setup.status=Launcher::Setup::Status::Failed;
                state.setup.message=setupSelectionError;state.setup.result.reset();
            }
            if (state.closing && !state.setup.Busy()) running=false;
        }
        if (game!=nullptr) {
            int result = 0;
            if (PollGame(game,&result)) {
                CloseGame(game);game=nullptr; SDL_ShowWindow(window); SDL_RaiseWindow(window);
                state.gameStatus = result == 0 ? "Game closed" : "Game stopped with an error. Open Logs for details.";
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
        if (!focused || page != Launcher::Page::Controls || state.bindingCapture.Busy())
            state.motionPreview.Close();
        state.bindingCapture.Tick(SDL_GetTicks(), focused);
        io.ConfigFlags &= ~(ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard);
        if (!state.bindingCapture.Blocks(Kirby::BindingDevice::Gamepad)) io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        if (!state.bindingCapture.Blocks(Kirby::BindingDevice::Keyboard)) io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui_ImplSDLRenderer3_NewFrame(); ImGui_ImplSDL3_NewFrame();
        Launcher::SynchronizeInputFocus(focused);
        ImGui::NewFrame();
        state.gameRunning = game!=nullptr;
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
                if (setupJob.Current().Busy() || state.choosingSource || game!=nullptr) break;
                if (!state.installationError.empty()) { state.gameStatus="Repair the installed build before playing. "+state.installationError;break; }
                if (!state.installationId.empty()) {
                    const auto installed=Launcher::Setup::ReadInstallation(root);
                    if (!installed || installed->identifier!=state.installationId) {
                        state.gameStatus="Installation selection changed. Restart the launcher before playing.";break;
                    }
                }
                valid = Verify(data, status);
                if (valid && fs::is_regular_file(executable) && saveSettings()) {
                    if (state.checkingBuild) {
                        buildProbe.Cancel();state.checkingBuild=false;
                        state.buildStatus="Refresh build information after the game closes";
                    }
                    game=StartGame(executable,settings,root);
                    if (!game) state.gameStatus="Could not start the native executable: "+std::string(SDL_GetError());
                    else { state.gameStatus = "Game running"; SDL_MinimizeWindow(window); }
                } else if (valid) state.gameStatus = fs::exists(executable)
                    ? "Could not save settings" : "Native executable missing. See the build guide.";
                break;
            case Launcher::Action::ChooseData:
            case Launcher::Action::ChooseImage:
                if (setupJob.Current().Busy() || state.choosingSource || game!=nullptr) break;
                { std::lock_guard lock(selection->mutex);selection->pending=true;state.choosingSource=true; }
                if (action==Launcher::Action::ChooseData)
                    SDL_ShowOpenFolderDialog(SourceChosen,new std::shared_ptr<Selection>(selection),window,Launcher::Utf8(data).c_str(),false);
                else {
                    static constexpr SDL_DialogFileFilter filters[]{{"Wii disc images and archives","wbfs;iso;rvz;7z"},{"All files","*"}};
                    SDL_ShowOpenFileDialog(SourceChosen,new std::shared_ptr<Selection>(selection),window,filters,2,nullptr,false);
                }
                break;
            case Launcher::Action::VerifyData: if (!state.choosingSource) startSetup(data);break;
            case Launcher::Action::BuildGame: if (!state.choosingSource) startSetup(data,Launcher::Setup::Operation::Install);break;
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
            case Launcher::Action::BuildGuide: SDL_OpenURL(LocalUrl(root/"docs/building.md").c_str()); break;
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
    if (game) CloseGame(game);
    state.motionPreview.Close();
    ImGui_ImplSDLRenderer3_Shutdown(); ImGui_ImplSDL3_Shutdown(); ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
    return 0;
}
