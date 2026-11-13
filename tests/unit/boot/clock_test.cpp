// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/boot/clock.h"
#include <iostream>
#include <stdexcept>
int main() {
    try {
        Kirby::HollywoodClock c;
        uint32_t value = 7;
        const auto check = [](bool ok) { if (!ok) throw std::runtime_error("clock register check failed"); };
        check(c.Read32(0xCD800180, value) && value == 0);
        check(c.Write32(0xCD800180, 0x100) && c.Read32(0xCD800180, value) && value == 0x100);
        check(c.Write32(0xCD8001CC, 0x4B0FFCE) && c.Write32(0xCD8001D0, 0xC0000000));
        check(c.AudioClockReady() && c.Mode32Khz());
        check(!c.Write32(0xCD8001D0, 0xC0000001) && c.AudioClockReady());
        check(c.Write32(0xCD800180, 0) && !c.AudioClockReady());
        check(c.Write32(0xCD8001CC, 0x4640FC0) && c.AudioClockReady() && !c.Mode32Khz());
        check(c.Write32(0xCD8001D0, 0) && !c.AudioClockReady());
        check(!c.Read32(0xCD800184, value) && !c.Write32(0xCD800181, 0));
        c.Reset(); check(c.Read32(0xCD8001CC, value) && value == 0 && !c.AudioClockReady());
        std::cout << "10 Hollywood clock checks passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
