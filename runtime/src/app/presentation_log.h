// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>

namespace Kirby {
enum class PresentationEvent { Clock,Presented,Discarded,Pending };
struct PresentationRecord {
    PresentationEvent event{};
    uint64_t request=0,requestSdlNs=0,callbackSdlNs=0,presentNs=0;
    uint32_t refreshNs=0;
    uint64_t sequence=0;
    uint32_t flags=0;
    int32_t clockId=0;
    uint64_t clockNs=0,sdlBeforeNs=0,sdlAfterNs=0;
};
class PresentationLog {
public:
    explicit PresentationLog(const std::filesystem::path& path);
    ~PresentationLog();
    PresentationLog(const PresentationLog&)=delete;
    PresentationLog& operator=(const PresentationLog&)=delete;
    void Write(const PresentationRecord& record);
    void Finish();
private:
    struct Close { void operator()(FILE* file) const { std::fclose(file); } };
    std::array<char,65536> buffer_;
    std::unique_ptr<FILE,Close> file_;
    unsigned rows_=0;
};
}
