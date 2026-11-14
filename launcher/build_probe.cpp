// SPDX-License-Identifier: GPL-3.0-only
#include "build_probe.h"
#include <SDL3/SDL.h>
#include <array>
#include <stdexcept>
namespace Launcher {
BuildProbe::~BuildProbe() { Stop(); }
bool BuildProbe::Start(const std::filesystem::path& executable,const std::filesystem::path& libraries) {
    if (status_==BuildProbeStatus::Checking) return false;
    Cancel();
    const auto path=std::filesystem::absolute(executable).string();
    const char* arguments[]{path.c_str(),"--build-info",nullptr};
    const auto properties=SDL_CreateProperties();
    auto* environment=SDL_CreateEnvironment(true);
    bool configured=properties && environment;
    if (configured) {
        configured=SDL_SetEnvironmentVariable(environment,"SDL_AUDIO_DRIVER","dummy",true) &&
            SDL_SetEnvironmentVariable(environment,"SDL_VIDEO_DRIVER","offscreen",true);
        if (!libraries.empty()) {
            const char* previous=SDL_GetEnvironmentVariable(environment,"LD_LIBRARY_PATH");
            const auto libraryPath=libraries.string()+(previous ? std::string(":")+previous : "");
            configured=configured && SDL_SetEnvironmentVariable(environment,"LD_LIBRARY_PATH",libraryPath.c_str(),true);
        }
        configured=configured && SDL_SetPointerProperty(properties,SDL_PROP_PROCESS_CREATE_ARGS_POINTER,arguments) &&
            SDL_SetPointerProperty(properties,SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER,environment) &&
            SDL_SetNumberProperty(properties,SDL_PROP_PROCESS_CREATE_STDIN_NUMBER,SDL_PROCESS_STDIO_NULL) &&
            SDL_SetNumberProperty(properties,SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,SDL_PROCESS_STDIO_APP) &&
            SDL_SetBooleanProperty(properties,SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN,true);
    }
    if (configured) process_=SDL_CreateProcessWithProperties(properties);
    const std::string error=SDL_GetError();
    if (properties) SDL_DestroyProperties(properties);
    if (environment) SDL_DestroyEnvironment(environment);
    if (!process_) { Fail("Cannot query native build: "+error);return false; }
    startedMs_=SDL_GetTicks();status_=BuildProbeStatus::Checking;return true;
}
void BuildProbe::Poll() {
    if (status_!=BuildProbeStatus::Checking) return;
    if (SDL_GetTicks()-startedMs_>=timeoutMs_) { Fail("Native build query timed out");return; }
    if (!exited_) {
        SDL_ClearError();exited_=SDL_WaitProcess(process_,false,&exitCode_);
        if (!exited_ && *SDL_GetError()) { Fail("Cannot collect native build query: "+std::string(SDL_GetError()));return; }
    }
    auto* output=SDL_GetProcessOutput(process_);
    if (!output) { Fail("Native build query has no output stream");return; }
    std::array<char,4096> buffer;
    // Bound both storage and per-frame work, even for a continuously writing child.
    for (unsigned reads=0;reads<4;++reads) {
        const auto size=SDL_ReadIO(output,buffer.data(),buffer.size());
        if (size) {
            if (document_.size()+size>16384) { Fail("Native build information exceeds 16 KiB");return; }
            document_.append(buffer.data(),size);continue;
        }
        const auto status=SDL_GetIOStatus(output);
        if (status==SDL_IO_STATUS_ERROR) { Fail("Cannot read native build information");return; }
        if (!exited_ || status!=SDL_IO_STATUS_EOF) return;
        if (exitCode_!=0) {
            Fail("Native build query exited with code "+std::to_string(exitCode_)+
                 ". Rebuild the native executable if it does not support --build-info.");return;
        }
        try {
            auto info=Kirby::ParseBuildInformation(document_);
            if (info.program!="runtime") throw std::runtime_error("The queried executable is not a native game build");
            info_=std::move(info);Stop();status_=BuildProbeStatus::Ready;
        } catch (const std::exception& error) { Fail("Native build information unavailable: "+std::string(error.what())); }
        return;
    }
}
void BuildProbe::Cancel() {
    Stop();info_.reset();document_.clear();error_.clear();status_=BuildProbeStatus::Idle;
}
void BuildProbe::Stop() {
    if (process_) {
        if (!exited_) { SDL_KillProcess(process_,true);SDL_WaitProcess(process_,true,&exitCode_); }
        SDL_DestroyProcess(process_);process_=nullptr;
    }
    exited_=false;
}
void BuildProbe::Fail(std::string error) {
    Stop();info_.reset();error_=error.substr(0,400);status_=BuildProbeStatus::Failed;
}
} // namespace Launcher
