// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
namespace Kirby {
// Latched configuration for the SDK's Hollywood audio clock sequence. This
// models its selected mode, not an analog PLL or DSP/audio sample execution.
class HollywoodClock {
public:
    bool Read32(uint32_t address, uint32_t& value) const;
    bool Write32(uint32_t address, uint32_t value);
    void Reset();
    bool AudioClockReady() const;
    bool Mode32Khz() const;
private:
    uint32_t control_ = 0, divider_ = 0, reset_ = 0;
};
HollywoodClock& Clock();
}
