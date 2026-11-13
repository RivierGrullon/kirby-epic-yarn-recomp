// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/devices/interrupts.h"
#include <array>
#include <iostream>
#include <stdexcept>
namespace {
std::array<uint8_t,128> ram;
uint8_t* Resolve(uint32_t address, uint32_t size) {
    if (address < 0x80000000 || uint64_t(address - 0x80000000) + size > ram.size())
        throw std::runtime_error("DMA outside test RAM");
    return ram.data() + address - 0x80000000;
}
uint32_t Clock() { return 0x12345678; }
unsigned checks;
void Check(bool b, const char* why) { ++checks; if (!b) throw std::runtime_error(why); }
}
int main() {
    try {
        Kirby::InterruptController exi;
        exi.ConfigureIpl({}, Resolve, Clock);
        auto write = [&](uint32_t off,uint32_t v) { Check(exi.Write32(0xCD006800 + off, v), "EXI write decode"); };
        auto read = [&](uint32_t off) { uint32_t v; Check(exi.Read32(0xCD006800 + off,v), "EXI read decode"); return v; };
        auto command = [&](uint32_t v) { write(0,0);write(0,0x100);write(16,v);write(12,0x35);write(0,0x108); };
        command(0x20000100);write(4,0x80000020);write(8,64);write(12,3);
        Check(ram[0x21] == 0x2C && ram[0x22] == 0xFF && ram[0x33] == 0x2C, "DMA copies SRAM at the requested offset");
        Check(ram[0x1F] == 0 && ram[0x60] == 0 && read(12) == 2 && (read(0) & 8), "DMA boundaries and completion");
        command(0x20000000);write(12,0x31);
        Check(read(16) == 0x12345678, "RTC uses the supplied clock and is latched per command");
        command(0xA0000100);ram[0x30] = 0xAB;write(4,0x80000020);write(8,64);write(12,7);
        command(0x20000100);write(4,0x80000000);write(8,64);write(12,3);
        Check(ram[0x10] == 0xAB, "DMA write is visible on subsequent read");
        command(0x20000100);write(4,0x80000060);write(8,64);
        const auto old = ram; const auto count = exi.ExiTransferCount();
        bool failed=false;
        try { exi.Write32(0xCD00680C,3); } catch(const std::runtime_error&) { failed=true; }
        Check(failed && ram == old && exi.ExiTransferCount() == count && !(read(0)&8), "invalid DMA must not mutate RAM or complete");
        std::cout << checks << " EXI DMA checks passed\n";
    } catch(const std::exception& e) { std::cerr << "check " << checks << ": " << e.what() << '\n';return 1; }
}
