// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "kirby/app/build_info.h"
#include <filesystem>
#include <optional>
#include <cstdint>

struct SDL_Process;
namespace Launcher {
enum class BuildProbeStatus { Idle, Checking, Ready, Failed };
class BuildProbe {
public:
    explicit BuildProbe(uint64_t timeoutMs=2500) : timeoutMs_(timeoutMs) {}
    ~BuildProbe();
    BuildProbe(const BuildProbe&)=delete;
    BuildProbe& operator=(const BuildProbe&)=delete;
    bool Start(const std::filesystem::path& executable,const std::filesystem::path& libraries={});
    void Poll();
    void Cancel();
    BuildProbeStatus Status() const { return status_; }
    const std::optional<Kirby::BuildInfo>& Information() const { return info_; }
    const std::string& Document() const { return document_; }
    const std::string& Error() const { return error_; }
private:
    void Stop();
    void Fail(std::string error);
    SDL_Process* process_=nullptr;
    uint64_t timeoutMs_,startedMs_=0;
    bool exited_=false;
    int exitCode_=0;
    BuildProbeStatus status_=BuildProbeStatus::Idle;
    std::optional<Kirby::BuildInfo> info_;
    std::string document_,error_;
};
} // namespace Launcher
