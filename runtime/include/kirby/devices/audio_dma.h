// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
namespace Kirby {
// Wii AID at the verified 32 kHz rate: eight stereo samples per 32-byte fetch.
class AudioDma {
public:
    using Resolver = uint8_t* (*)(uint32_t, uint32_t);
    void Configure(Resolver memory, uint64_t (*clock)(), bool (*sink)(const uint8_t*, uint32_t));
    bool Read16(uint32_t address, uint16_t& value) const;
    bool Write16(uint32_t address, uint16_t value);
    bool Poll(); // True when the initial or auto-reload AID interrupt is due.
    void Reset();
    uint64_t Blocks() const { return blocks_; }
    uint64_t DroppedBlocks() const { return dropped_; }
private:
    bool Valid(uint32_t address, uint32_t count) const;
    Resolver memory_ = nullptr;
    uint64_t (*clock_)() = nullptr;
    bool (*sink_)(const uint8_t*, uint32_t) = nullptr;
    uint32_t source_ = 0, cursor_ = 0;
    uint16_t control_ = 0, remaining_ = 0;
    uint64_t epoch_ = 0, consumed_ = 0, blocks_ = 0, dropped_ = 0;
    bool startIrq_ = false;
};
}
