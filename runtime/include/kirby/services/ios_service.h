// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "kirby/services/nand.h"
#include "kirby/services/disc.h"
#include "kirby/services/es_metadata.h"
#include <cstdint>
#include <deque>
#include <map>
#include <optional>

namespace Kirby {
// IOS request ABI, independently owned descriptors, and STM event lifetime.
// No host shutdown action or synthetic reset event is generated here.
class IosServices {
public:
    using Resolve = uint8_t* (*)(uint32_t, uint32_t);
    struct Completion { uint32_t request; int32_t result; };
    explicit IosServices(Resolve resolve) : resolve_(resolve) {}
    void ConfigureNand(const std::filesystem::path& root, uint64_t titleId);
    void ConfigureDisc(const std::filesystem::path& root, uint64_t capacity);
    void ConfigureDiscTitle(const std::filesystem::path& root, uint64_t titleId);
    const DiscTitleMetadata* DiscTitle() const { return discTitle_.get(); }
    const ExtractedDisc* Disc() const { return disc_.get(); }
    std::optional<int32_t> Submit(uint32_t request, bool asynchronous);
    bool HasHandle(int32_t handle) const { return handles_.contains(handle); }
    uint32_t PendingEventRequest() const { return eventRequest_; }
    uint32_t IdleLedMode() const { return idleLedMode_; }
    bool VideoDimmingDisabled() const { return videoDimmingDisabled_; }
    std::optional<uint32_t> NetworkRtcCounter() const { return networkRtcCounter_; }
    void TriggerEvent(uint32_t event);
    std::optional<Completion> TakeCompletion();
private:
    enum class Device { StmImmediate, StmEventHook, Fs, Es, File, KdRequest, KdTime, Di };
    struct Handle { Device device; std::shared_ptr<NandFile> file; };
    Resolve resolve_;
    std::unique_ptr<NandStore> nand_;
    std::unique_ptr<ExtractedDisc> disc_;
    std::unique_ptr<DiscTitleMetadata> discTitle_;
    std::map<int32_t, Handle> handles_;
    std::deque<Completion> completions_;
    uint32_t eventRequest_ = 0, eventOutput_ = 0;
    int32_t eventHandle_ = -1;
    uint32_t idleLedMode_ = 0;
    bool videoDimmingDisabled_ = false;
    std::optional<uint32_t> networkRtcCounter_;
};
} // namespace Kirby
