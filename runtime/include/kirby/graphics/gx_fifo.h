// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <span>

namespace Kirby {
// PE CPU access configuration and token/finish interrupt handshakes. EFB reads,
// bounding boxes and performance counters require a renderer and are unsupported.
class PixelEngine {
public:
    bool Read16(uint32_t address, uint16_t& value) const;
    bool Write16(uint32_t address, uint16_t value);
    void SetToken(uint16_t token, bool interrupt);
    void Finish() { finishPending_ = true; }
    bool TokenInterrupt() const { return tokenPending_ && (control_ & 1); }
    bool FinishInterrupt() const { return finishPending_ && (control_ & 2); }
private:
    std::array<uint16_t, 5> config_{};
    uint16_t control_ = 0, token_ = 0;
    bool tokenPending_ = false, finishPending_ = false;
};
PixelEngine& Pixels();
// CP/PI FIFO transport. The consumer receives ordered, complete 32-byte bursts;
// it must retain partial GX commands across bursts. No implicit renderer exists.
class GxFifo {
public:
    using Resolver = uint8_t* (*)(uint32_t, uint32_t);
    using Consumer = std::function<void(std::span<const uint8_t>)>;
    void Reset();
    void Configure(Resolver resolve, Consumer consume = {});
    bool Read16(uint32_t address, uint16_t& value) const;
    bool Write16(uint32_t address, uint16_t value);
    bool Read32(uint32_t address, uint32_t& value) const;
    bool Write32(uint32_t address, uint32_t value);
    void SetWritePipeAddress(uint32_t value);
    // Transfers to RAM are synchronous. BNE describes outstanding bus transfers,
    // not a partially filled gather buffer; see the pinned Dolphin GPFifo notes.
    uint32_t WritePipeAddress() const { return writePipeAddress_; }
    void Gather(std::span<const uint8_t> bytes);
    bool Interrupt() const;
    uint16_t Status() const;
    uint16_t Control() const { return control_; }
    uint32_t Base() const { return regs_[0]; }
    uint32_t End() const { return regs_[1]; }
    uint32_t Distance() const { return regs_[4]; }
    uint64_t Bursts() const { return bursts_; }
    uint64_t ConsumedBursts() const { return consumed_; }
private:
    bool AtBreakpoint() const;
    void Drain();
    static void CheckRing(uint32_t base, uint32_t end, uint32_t pointer);
    std::array<uint32_t, 8> regs_{};
    std::array<uint32_t, 3> pi_{};
    uint16_t control_ = 0, perf_ = 0, token_ = 0;
    uint32_t writePipeAddress_ = 0;
    std::array<uint8_t, 32> gather_{};
    unsigned used_ = 0;
    uint64_t bursts_ = 0, consumed_ = 0;
    Resolver resolve_ = nullptr;
    Consumer consume_;
};
GxFifo& GraphicsFifo();
}
