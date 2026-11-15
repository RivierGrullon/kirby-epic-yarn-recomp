// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace Kirby {
// Diagnostic DMA source capture. This precedes SDL gain and output buffering.
class PcmTrace {
public:
    explicit PcmTrace(const std::filesystem::path& path);
    ~PcmTrace();
    PcmTrace(const PcmTrace&)=delete;
    PcmTrace& operator=(const PcmTrace&)=delete;
    void Write(std::span<const uint8_t> rightLeftBe,uint64_t hostNs);
    void Finish();
private:
    struct Close { void operator()(FILE* file) const { std::fclose(file); } };
    struct Stamp { uint64_t frame,hostNs; };
    std::array<char,65536> buffer_;
    std::unique_ptr<FILE,Close> file_;
    std::vector<Stamp> stamps_;
    uint64_t frames_=0,lastHostNs_=0;
    void Put(std::span<const uint8_t> bytes);
    void U32(uint32_t value);
    void U64(uint64_t value);
};
// Used only on the guest/device thread; lifetime is owned by the application.
void SetAudioTrace(PcmTrace* trace);
}
