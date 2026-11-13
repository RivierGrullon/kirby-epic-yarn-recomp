// SPDX-License-Identifier: GPL-3.0-only
// Register/latch behavior: Dolphin Emulator Project DSP.cpp (GPL-2.0-or-later).
#include "kirby/devices/audio_dma.h"
#include <stdexcept>
namespace Kirby {
void AudioDma::Configure(Resolver memory, uint64_t (*clock)(), bool (*sink)(const uint8_t*, uint32_t)) {
    memory_ = memory; clock_ = clock; sink_ = sink;
}
void AudioDma::Reset() {
    source_ = cursor_ = control_ = remaining_ = 0;
    epoch_ = consumed_ = blocks_ = dropped_ = 0; startIrq_ = false;
}
bool AudioDma::Valid(uint32_t address, uint32_t count) const {
    return count && memory_ && clock_ && sink_ && memory_(address, count*32);
}
bool AudioDma::Read16(uint32_t address, uint16_t& value) const {
    switch (address) {
    case 0xCC005030: value = source_ >> 16; break;
    case 0xCC005032: value = source_; break;
    case 0xCC005036: value = control_; break;
    case 0xCC00503A: value = remaining_ ? remaining_-1 : 0; break;
    default: return false;
    }
    return true;
}
bool AudioDma::Write16(uint32_t address, uint16_t value) {
    switch (address) {
    case 0xCC005030: source_ = (source_ & 0xFFFF) | (uint32_t(value & 0x1FFF)<<16); return true;
    case 0xCC005032: source_ = (source_ & 0xFFFF0000) | (value & 0xFFE0); return true;
    case 0xCC005036:
        if ((value & 0x8000) && !(control_ & 0x8000)) {
            if (!Valid(source_, value & 0x7FFF)) return false;
            cursor_ = source_; remaining_ = value & 0x7FFF;
            epoch_ = clock_(); consumed_ = 0; startIrq_ = true;
        }
        control_ = value;
        if (!(value & 0x8000)) startIrq_ = false;
        return true;
    default: return false;
    }
}
bool AudioDma::Poll() {
    if (!(control_ & 0x8000)) return false;
    const uint64_t elapsed = clock_()-epoch_;
    // 200 Broadway cycles (rounded to 17 TB ticks), matching the reference's
    // delayed first IRQ. Exact hardware scheduling is not claimed.
    bool irq = startIrq_ && elapsed >= 17;
    if (irq) startIrq_ = false;
    const uint64_t due = elapsed/60750000*4000 + elapsed%60750000*4000/60750000;
    // DMA keeps cycling while guest execution is paused or a shader stalls.
    // Advance expired fetches without flooding the host sink with old audio.
    // Preserve the latched tail, reload cursor and level-triggered IRQ exactly.
    constexpr uint64_t MaxCatchUpBlocks = 256; // 64 ms at 32 kHz stereo.
    if (due-consumed_ > MaxCatchUpBlocks) {
        const auto skipped = due-consumed_-MaxCatchUpBlocks;
        if (skipped < remaining_) {
            cursor_ += uint32_t(skipped)*32;
            remaining_ -= skipped;
        } else {
            const auto count = control_ & 0x7FFF;
            if (!Valid(source_, count))
                throw std::runtime_error("Audio DMA skipped reload exceeds guest RAM or has zero length");
            const auto intoBuffer = (skipped-remaining_) % count;
            cursor_ = source_ + uint32_t(intoBuffer)*32;
            remaining_ = count-intoBuffer;
            irq = true;
        }
        consumed_ += skipped;
        dropped_ += skipped;
    }
    while (consumed_ < due) {
        const auto data = memory_(cursor_, 32);
        if (!data) throw std::runtime_error("Audio DMA source exceeds guest RAM");
        if (!sink_(data, 32)) throw std::runtime_error("Native audio sink rejected a DMA block");
        ++consumed_; ++blocks_; cursor_ += 32;
        if (--remaining_ == 0) {
            const auto count = control_ & 0x7FFF;
            if (!Valid(source_, count)) throw std::runtime_error("Audio DMA reload exceeds guest RAM or has zero length");
            cursor_ = source_; remaining_ = count; irq = true;
        }
    }
    return irq;
}
}
