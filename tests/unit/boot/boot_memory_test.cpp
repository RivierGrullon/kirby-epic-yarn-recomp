// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/boot/boot_memory.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
unsigned checks;
void Check(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
void Word(std::vector<uint8_t>& b, size_t p, uint32_t v) {
    for (unsigned i = 0; i != 4; ++i) b.at(p + i) = v >> (24 - 8 * i);
}
uint32_t Word(const std::vector<uint8_t>& b, size_t p) {
    return (uint32_t(b.at(p)) << 24) | (uint32_t(b.at(p+1)) << 16) |
           (uint32_t(b.at(p+2)) << 8) | b.at(p+3);
}
Kirby::BootData Fixture() {
    Kirby::BootData b;
    b.boot.resize(0x440); b.bi2.resize(0x2000); b.fst.resize(32); b.tmd.resize(0x1E4);
    const char id[] = "RK5E01"; std::copy_n(id, 6, b.boot.begin());
    Word(b.boot, 0x18, 0x5D1C9EA3); Word(b.boot, 0x428, 8); Word(b.boot, 0x42C, 8);
    Word(b.tmd, 0, 0x10001); Word(b.tmd, 0x184, 1); Word(b.tmd, 0x188, 56);
    Word(b.tmd, 0x18C, 0x10000); Word(b.tmd, 0x190, 0x524B3545);
    b.apploader.resize(48);
    Word(b.apploader,16,0x81200000);Word(b.apploader,20,16);
    // Synthetic initializer with a deliberately different minimum revision.
    Word(b.apploader,32,0x3C000038);Word(b.apploader,36,0x3C608000);
    Word(b.apploader,40,0x6000007B);Word(b.apploader,44,0x90033188);
    Word(b.fst, 0, 0x1000000); Word(b.fst, 8, 2);
    Word(b.fst, 12, 1); Word(b.fst, 16, 0x100); Word(b.fst, 20, 16);
    b.fst[25] = 'a'; b.bi2[0x27] = 5;
    return b;
}
}
int main() {
    try {
        std::vector<uint8_t> ram(Kirby::Mem1Size, 0xCC);
        unsigned writes = 0;
        const auto write = [&](uint32_t address, std::span<const uint8_t> data) {
            Check(address >= 0x80000000 && uint64_t(address) + data.size() <= 0x81800000, "write outside MEM1");
            std::copy(data.begin(), data.end(), ram.begin() + address - 0x80000000); ++writes;
        };
        auto b = Fixture();
        const auto layout = Kirby::InstallBootData(b, 0x81000000, write);
        Check(layout.titleId == 0x00010000524B3545ull, "IOS title identity comes from validated TMD");
        Check(Word(ram, 0xF8) == 243000000 && Word(ram, 0xFC) == 729000000, "Broadway clock frequencies");
        Check(Word(ram, 0x3118) == 0x4000000 && Word(ram, 0x3124) == 0x90000800, "retail MEM2 size and arena start");
        Check(Word(ram, 0x3128) == 0x935E0000 && Word(ram, 0x3130) == 0x935E0000 &&
              Word(ram, 0x3134) == 0x93600000 && Word(ram, 0x3148) == 0x93600000, "IOS56 IPC arena excluded from game heap");
        Check(Word(ram, 0x3140) == 0x38161E, "explicit IOS56 compatibility version");
        Check(Word(ram,0x3188)==0x0038007B,"minimum IOS version is derived from apploader");
        Check(layout.arenaHi == layout.bi2 && layout.bi2 + b.bi2.size() <= layout.fst &&
              layout.fst + b.fst.size() <= 0x81800000 && !(layout.fst & 31), "aligned metadata placement");
        Check(Word(ram, 0x34) == layout.arenaHi && Word(ram, 0x3110) == layout.arenaHi &&
              Word(ram, 0xF4) == layout.bi2 && Word(ram, 0x38) == layout.fst, "guest boot pointers match allocations");
        Check(std::equal(b.bi2.begin(), b.bi2.end(), ram.begin() + layout.bi2 - 0x80000000) &&
              std::equal(b.fst.begin(), b.fst.end(), ram.begin() + layout.fst - 0x80000000), "metadata copied byte for byte");
        Check(ram[0x4000] == 0xCC && ram[0x1000000] == 0xCC, "loader preserves program, BSS and stack");
        const auto rejected = [&](Kirby::BootData bad, uint32_t end = 0x81000000) {
            const auto before = writes;
            bool caught = false;
            try { Kirby::InstallBootData(bad, end, write); } catch (const std::runtime_error&) { caught = true; }
            Check(caught && writes == before, "invalid image must fail before guest writes");
        };
        auto bad = b; bad.boot[7] = 1; rejected(bad);
        bad = b; bad.bi2.pop_back(); rejected(bad);
        bad = b; Word(bad.tmd, 0x188, 58); rejected(bad);
        bad = b; Word(bad.tmd, 0, 0x10000); rejected(bad);
        bad = b; bad.apploader.clear(); rejected(bad);
        bad = b; Word(bad.apploader,20,0xFFFFFFFF); rejected(bad);
        bad = b; Word(bad.apploader,16,0x81200010); rejected(bad);
        bad = b; Word(bad.apploader,44,0x90033184); rejected(bad);
        bad = b; Word(bad.apploader,32,0x3C000039); rejected(bad);
        bad = b; Word(bad.apploader,40,0x6000161F); rejected(bad);
        bad = b; bad.apploader.insert(bad.apploader.end(),b.apploader.begin()+32,b.apploader.end());
        Word(bad.apploader,20,32); rejected(bad);
        bad = b; Word(bad.boot, 0x428, 9); rejected(bad);
        bad = b; Word(bad.boot, 0x42C, 0xFFFFFFFF); rejected(bad);
        bad = b; Word(bad.fst, 8, 0xFFFFFFFF); rejected(bad);
        bad = b; Word(bad.fst, 12, 99); rejected(bad);
        bad = b; std::fill(bad.fst.begin()+25, bad.fst.end(), 'x'); rejected(bad);
        bad = b; Word(bad.fst, 12, 0x1000001); Word(bad.fst, 20, 99); rejected(bad);
        rejected(b, 0x81800000);
        std::cout << checks << " boot-memory checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
