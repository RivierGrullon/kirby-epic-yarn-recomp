// SPDX-License-Identifier: GPL-3.0-only
// Native host for the Qt launcher and its command-line tools.
#include <SDL3/SDL.h>
#include "kirby/settings.h"
#include "kirby/boot/validate_input.h"
#include "kirby/platform/command_line.h"
#include "kirby/app/build_info.h"
#include "desktop_service.h"
#include "build_probe.h"
#include "platform.h"
#include <cryptopp/sha.h>
#include "setup/job.h"
#include "setup/installation.h"
#include <array>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fs = std::filesystem;
namespace {
constexpr char ExpectedSha1[] = "f4280a2b0709bfde4132be22e9f7fa8ecfa2a130";
volatile sig_atomic_t sourceCancelled=0;
bool Verify(const fs::path& data, std::string& status) {
    for (const char* entry : {"sys/main.dol", "sys/boot.bin", "sys/fst.bin", "ticket.bin", "tmd.bin", "files"}) {
        if (!fs::exists(data / entry)) { status = std::string("Missing ") + entry; return false; }
    }
#ifdef _WIN32
    try {
        Kirby::ValidateWindowsInput(data);
        status = "RK5E01 Rev 0 (USA) verified"; return true;
    } catch (const std::exception& error) {
        status = std::string("Data verification failed: ") + error.what(); return false;
    }
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
    const bool service = argc == 2 && std::string_view(argv[1]) == "--ui-service";
    if (argc != 1 && !selfTest && !service) { std::fprintf(stderr,"Usage: kirby-launcher [--self-test | --check-data DATA | --check-source SOURCE | --print-settings TOML | --build-info | --check-native-build EXECUTABLE]\n"); return 2; }
    const fs::path developerExecutable = Launcher::FromUtf8(SDL_GetBasePath()) / Launcher::GameExecutable;
    const bool portable = fs::is_regular_file(developerExecutable.parent_path() / "kirby-portable.txt");
    const fs::path root = portable ? developerExecutable.parent_path() : developerExecutable.parent_path().parent_path().parent_path();
    if (!portable && !fs::exists(root / "tools/kirby.py")) {
        std::fprintf(stderr,"Keep the launcher in this project's build/native or build/windows folder\n"); return 2;
    }
    if (!service) {
#ifdef _WIN32
        const auto frontend = developerExecutable.parent_path()/"qt-launcher/kirby-ui.exe";
#else
        const auto frontend = developerExecutable.parent_path()/"qt-launcher/kirby-ui";
#endif
        if (!fs::is_regular_file(frontend)) {
            std::cerr << "Qt launcher missing: " << Launcher::Utf8(frontend)
                      << ". Restore the qt-launcher folder from Kirby Setup or build the Qt frontend.\n";
            return 1;
        }
        const auto program=Launcher::Utf8(frontend);
        const char* args[]{program.c_str(),selfTest?"--self-test":nullptr,nullptr};
        auto* ui=SDL_CreateProcess(args,false);
        if (!ui) { std::cerr<<SDL_GetError()<<'\n'; return 1; }
        int result=0;
        if (selfTest && !SDL_WaitProcess(ui,true,&result)) result=1;
        SDL_DestroyProcess(ui);
        return result;
    }
    Launcher::State state{root, developerExecutable, root / "local/user/settings.toml", Kirby::Settings{root / "disc/DATA"}};
    state.launcherBuild=Kirby::ParseBuildInformation(Kirby::BuildInformation(Kirby::BuildProgram::Launcher));
    try {
        state.config=Kirby::LoadOrMigrateSettings(state.settingsFile,root/"local/user/launcher.cfg",root,state.config);
    } catch (const std::exception& error) {
        state.settingsError=error.what();
    }
    try {
        if (const auto installed=Launcher::Setup::ReadInstallation(root)) {
            state.executable=installed->executable;
            state.installationId=installed->identifier;
            if (!fs::exists(state.settingsFile)) state.config.gameData=installed->dataRoot;
        }
    } catch (const std::exception& error) {
        state.installationError=error.what();
    }

    GameProcess* child=nullptr;
    const int result=Launcher::DesktopService(state,Verify,
        [&] { child=StartGame(state.executable,state.settingsFile,root); return child!=nullptr; },
        [&]() -> std::optional<int> {
            int code=0;
            if (!child || !PollGame(child,&code)) return {};
            CloseGame(child); child=nullptr; return code;
        });
    if (child) CloseGame(child);
    SDL_Quit();
    return result;
}
