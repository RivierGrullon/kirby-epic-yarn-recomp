// SPDX-License-Identifier: GPL-3.0-only
#include "build_probe.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>

namespace fs=std::filesystem;
namespace {
unsigned checks;
void Check(bool value,const char* reason) { ++checks;if (!value) throw std::runtime_error(reason); }
void Finish(Launcher::BuildProbe& probe) {
    const auto began=std::chrono::steady_clock::now();
    while (probe.Status()==Launcher::BuildProbeStatus::Checking) {
        probe.Poll();SDL_Delay(1);
        Check(std::chrono::steady_clock::now()-began<std::chrono::seconds(3),"Build query blocked the application");
    }
}
int Child(const std::string& mode) {
    const char* audio=std::getenv("SDL_AUDIO_DRIVER");
    const char* video=std::getenv("SDL_VIDEO_DRIVER");
    if (!audio || std::string(audio)!="dummy" || !video || std::string(video)!="offscreen") return 17;
    if (mode=="timeout") { SDL_Delay(10000);return 0; }
    if (mode=="overflow") { std::cout << std::string(50000,'x');return 0; }
    if (mode=="legacy") { std::cerr << "Unsupported --build-info\n";return 2; }
    if (mode=="malformed") { std::cout << "not a build record";return 0; }
    auto text=Kirby::BuildInformation(mode=="launcher" ? Kirby::BuildProgram::Launcher : Kirby::BuildProgram::Runtime);
    if (mode=="changed") {
        const auto start=text.find("source_sha256 = \"")+17;
        text[start]=text[start]=='a' ? 'b' : 'a';
    }
    std::cout << text;
    return mode=="nonzero" ? 7 : 0;
}
}
int main(int argc,char** argv) {
    if (argc==2 && std::string(argv[1])=="--build-info") return Child(fs::path(argv[0]).filename().string());
    using namespace Launcher;
    std::string pattern=(fs::temp_directory_path()/"kirby's build query.XXXXXX").string();
    Check(mkdtemp(pattern.data())!=nullptr,"Could not create query fixture directory");
    const fs::path directory=pattern;
    struct Cleanup { fs::path path;~Cleanup() { fs::remove_all(path); } } cleanup{directory};
    const auto fixture=[&](const std::string& name) {
        const auto path=directory/name;fs::create_symlink(fs::canonical(argv[0]),path);return path;
    };
    const auto valid=fixture("valid build's executable");
    const char* previousAudio=std::getenv("SDL_AUDIO_DRIVER");
    const std::string previous=previousAudio ? previousAudio : "";
    BuildProbe probe;
    Check(probe.Start(valid),"Could not start valid native build query");
    Check(!probe.Start(valid),"Duplicate query was started");
    Finish(probe);
    Check(probe.Status()==BuildProbeStatus::Ready && probe.Information().has_value(),"Valid build was not reported");
    Check(probe.Information()->program=="runtime","Native query returned the wrong program");
    const auto local=Kirby::ParseBuildInformation(Kirby::BuildInformation(Kirby::BuildProgram::Runtime));
    Check(Kirby::SameBuildInputs(local,*probe.Information()),"Native record changed in transit");
    Check(probe.Start(fixture("changed")),"Refresh could not start");Finish(probe);
    Check(probe.Status()==BuildProbeStatus::Ready && !Kirby::SameBuildInputs(local,*probe.Information()),
          "Probe displayed its own build instead of the queried executable's record");
    for (const auto mode : {"legacy","malformed","launcher","nonzero","overflow"}) {
        Check(probe.Start(fixture(mode)),"Failed-case fixture did not start");Finish(probe);
        Check(probe.Status()==BuildProbeStatus::Failed && !probe.Information() && !probe.Error().empty(),
              "Invalid query was presented as a valid runtime build");
    }
    Check(!probe.Start(directory/"missing") && probe.Status()==BuildProbeStatus::Failed,"Missing executable was accepted");
    const auto timeout=fixture("timeout");BuildProbe timed(100);
    const auto started=std::chrono::steady_clock::now();
    Check(timed.Start(timeout),"Timeout fixture did not start");Finish(timed);
    Check(timed.Status()==BuildProbeStatus::Failed && timed.Error().find("timed out")!=std::string::npos,
          "Stalled build query did not time out");
    Check(std::chrono::steady_clock::now()-started<std::chrono::seconds(1),"Timeout blocked the UI");
    Check(timed.Start(timeout),"Cancelled fixture did not start");timed.Cancel();
    Check(timed.Status()==BuildProbeStatus::Idle && !timed.Information(),"Cancellation retained a pending query");
    Check((std::getenv("SDL_AUDIO_DRIVER") ? std::getenv("SDL_AUDIO_DRIVER") : "")==previous,
          "Child diagnostic changed the parent's audio environment");
    int status=0;errno=0;
    Check(waitpid(-1,&status,WNOHANG)==-1 && errno==ECHILD,"Build query leaked a child or zombie");
    std::cout << "Build query: actual child identity, timeout, output bounds, failures and cleanup verified\n";
}
