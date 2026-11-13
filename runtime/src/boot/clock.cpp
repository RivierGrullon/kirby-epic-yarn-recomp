// SPDX-License-Identifier: GPL-3.0-only
// Addresses: Dolphin WII_IPC.cpp UNK_180/1CC/1D0; configuration sequences:
// Swiftshine/key OSAudioSystem.c, checked against RK5E01 __AIClockInit.
#include "kirby/boot/clock.h"
namespace Kirby {
bool HollywoodClock::Read32(uint32_t address, uint32_t& value) const {
    switch (address) {
    case 0xCD800180: value = control_; return true;
    case 0xCD8001CC: value = divider_; return true;
    case 0xCD8001D0: value = reset_; return true;
    default: return false;
    }
}
bool HollywoodClock::Write32(uint32_t address, uint32_t value) {
    // Accept only fields actually programmed by the reviewed SDK sequence.
    // Unreviewed register/bit accesses remain explicit runtime failures.
    switch (address) {
    case 0xCD800180:
        if (value & ~0x180u) return false;
        control_ = value; return true;
    case 0xCD8001CC:
        if (value & ~0x07FFFFFFu) return false;
        divider_ = value; return true;
    case 0xCD8001D0:
        if (value & ~0xD0000000u) return false;
        reset_ = value; return true;
    default: return false;
    }
}
void HollywoodClock::Reset() { control_ = divider_ = reset_ = 0; }
bool HollywoodClock::Mode32Khz() const { return (control_ & 0x100) != 0; }
bool HollywoodClock::AudioClockReady() const {
    return !(control_ & 0x80) && reset_ == 0xC0000000 &&
        divider_ == (Mode32Khz() ? 0x04B0FFCEu : 0x04640FC0u);
}
HollywoodClock& Clock() { static HollywoodClock clock; return clock; }
}
