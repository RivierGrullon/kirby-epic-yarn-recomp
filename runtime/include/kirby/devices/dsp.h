// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include "kirby/devices/audio_dma.h"
namespace Kirby {
struct DspTaskImage {
    uint32_t address = 0, length = 0;
    uint16_t iram = 0, dramLength = 0, entry = 0;
};
// Native HLE of the SDK's verified initialization microcode, not a DSP interpreter.
// Unknown DSP commands/microcode fail explicitly; verified AXWii uses the task backend.
class DspBoot {
public:
    using Resolver = uint8_t* (*)(uint32_t, uint32_t);
    void Configure(Resolver memory, bool (*verifyInitCode)(), uint64_t (*clock)());
    void ConfigureTaskBackend(bool (*boot)(const DspTaskImage&),
        bool (*readMail)(uint32_t&), void (*sendMail)(uint32_t), void (*stop)(),
        bool (*interruptActive)() = nullptr);
    void Poll();
    bool Read16(uint32_t address, uint16_t& value);
    bool Read32(uint32_t address, uint32_t& value);
    bool Write16(uint32_t address, uint16_t value);
    bool Write32(uint32_t address, uint32_t value);
    void Raise(uint16_t bits);
    bool Interrupt() const;
    uint16_t Control() const { return control_; }
    uint64_t DmaCount() const { return dmaCount_; }
    uint64_t InitCount() const { return initCount_; }
    uint64_t InitMailCount() const { return initMailCount_; }
    uint64_t TaskBootCount() const { return taskBootCount_; }
    AudioDma& Audio() { return audio_; }
    const AudioDma& Audio() const { return audio_; }
private:
    bool SendMail(uint32_t mail);
    bool WriteControl(uint16_t value);
    bool StartDma(uint32_t value);
    uint16_t control_ = 0x804, arInfo_ = 0, arRefresh_ = 156;
    uint32_t mmAddress_ = 0, arAddress_ = 0, dmaCountReg_ = 0, toDsp_ = 0;
    uint32_t pendingMail_ = 0x8071FEED, lastMail_ = 0;
    uint64_t dmaDeadline_ = 0, initDeadline_ = 0;
    uint64_t dmaCount_ = 0, initCount_ = 0, initMailCount_ = 0;
    Resolver memory_ = nullptr;
    bool (*verifyInitCode_)() = nullptr;
    uint64_t (*clock_)() = nullptr;
    DspTaskImage task_{};
    uint32_t romCommand_ = 0, romFields_ = 0;
    uint64_t taskBootCount_ = 0;
    bool taskActive_ = false;
    AudioDma audio_;
    bool (*bootTask_)(const DspTaskImage&) = nullptr;
    bool (*readTaskMail_)(uint32_t&) = nullptr;
    void (*sendTaskMail_)(uint32_t) = nullptr;
    void (*stopTask_)() = nullptr;
    bool (*cpuInterruptActive_)() = nullptr;
};
}
