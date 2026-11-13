// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include "kirby/devices/ipl.h"
#include "kirby/devices/dsp.h"

namespace Kirby {
// Single-threaded interrupt registers and EXI boot transport. DMA is limited
// to reviewed SRAM/RTC operations in validated MEM1. DSP boot uses a verified
// initialization microcode HLE. Sample playback remains unsupported;
// CPU exception delivery is provided separately at audited scheduler boundaries.
class InterruptController {
public:
    InterruptController();
    void Reset();
    bool Read16(uint32_t address, uint16_t& value);
    bool Read32(uint32_t address, uint32_t& value);
    bool Write16(uint32_t address, uint16_t value);
    bool Write32(uint32_t address, uint32_t value);
    void SetPiLine(uint32_t source, bool asserted);
    void RaiseDsp(uint16_t sources);
    DspBoot& Dsp() { return dsp_; }
    void PollDsp();
    void RaiseAi();
    void RaiseExi(unsigned channel, uint32_t sources);
    void RaiseIpc(uint32_t sources);
    bool Pending() const;
    uint32_t PiMask() const;
    uint32_t IpcMask() const;
    uint32_t IpcMessage() const { return ipcMessage_; }
    bool DeviceSourcesMasked() const;
    uint64_t ExiTransferCount() const;
    bool EuartReady() const;
    uint64_t EuartBytesWritten() const;
    void ConfigureIpl(const std::filesystem::path& path,
                      uint8_t* (*dmaResolver)(uint32_t, uint32_t), uint32_t (*rtcClock)());

private:
    void UpdateDeviceLines();
    bool WriteExiControl(unsigned channel, uint32_t value);
    uint32_t piCause_, piMask_, ipcFlags_, ipcMask_, aiControl_;
    uint32_t aiVolume_, aiThreshold_;
    uint32_t ipcControl_, ipcMessage_;
    uint16_t miMask_, miFlags_, miProtection_;
    DspBoot dsp_;
    std::array<uint32_t, 3> exiStatus_;
    struct ExiTransfer {
        uint32_t address = 0, length = 0, control = 0, data = 0;
    };
    std::array<ExiTransfer, 3> exiTransfer_;
    uint32_t iplCommand_;
    unsigned iplCommandBytes_;
    uint64_t exiTransferCount_;
    uint8_t euartControl_;
    uint64_t euartBytesWritten_;
    std::string euartLine_;
    IplStorage storage_;
    uint32_t iplCursor_;
    uint8_t* (*dmaResolver_)(uint32_t, uint32_t);
    uint32_t (*rtcClock_)();
};

InterruptController& Interrupts();
} // namespace Kirby

// Used only from the runtime's checked slow paths. False leaves the existing
// unsupported-MMIO error intact, without fabricating a value or success.
bool KirbyMmioRead16(uint32_t address, uint16_t& value);
bool KirbyMmioRead32(uint32_t address, uint32_t& value);
bool KirbyMmioWrite16(uint32_t address, uint16_t value);
bool KirbyMmioWrite32(uint32_t address, uint32_t value);
