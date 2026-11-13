// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/devices/ipl.h"
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
unsigned checks;
void Check(bool b, const char* why) { ++checks; if (!b) throw std::runtime_error(why); }
}
int main() {
    const auto dir = std::filesystem::temp_directory_path() /
        ("kirby-sram-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        Kirby::IplStorage s;
        std::array<uint8_t, 64> data{};
        s.Read(0x20000100, 0, data);
        Check(data[0] == 0 && data[1] == 0x2C && data[2] == 0xFF && data[3] == 0xD0, "valid default SRAM checksums");
        Check(data[18] == 0 && data[19] == 0x2C, "English stereo defaults");
        s.SetRtc(0x12345678);
        std::array<uint8_t, 4> rtc{};
        s.Read(0x20000000, 0, rtc);
        Check(rtc == std::array<uint8_t, 4>{0x12,0x34,0x56,0x78}, "RTC wire order");
        Check(!s.Supports(0x20000100, 63, 2, false), "out-of-range SRAM transfer rejected");
        Check(!s.Supports(0x00000000, 0, 4, false), "ROM reads are not replaced by SRAM");
        Check(!s.Supports(0x20000100, 0, 4, true), "read command cannot perform a write");
        std::filesystem::create_directory(dir);
        s.Open(dir / "sram.bin");
        Check(std::filesystem::file_size(dir / "sram.bin") == 64, "persistence excludes volatile RTC");
        const std::array<uint8_t,2> id{0x12,0x34};
        s.Write(0xA0000100, 0x30, id);
        Kirby::IplStorage restored;
        restored.Open(dir / "sram.bin");
        restored.Read(0x20000100, 0, data);
        Check(data[0x30] == 0x12 && data[0x31] == 0x34 && data[19] == 0x2C, "saved settings survive reload and preserve adjacent fields");
        const std::array<uint8_t,4> flags{0xA5,0xA5,0xA5,0xA5};
        s.Write(0xA1000800, 0, flags);
        s.Read(0x21000800, 0, rtc);
        Check(rtc == flags, "RTC flags follow reviewed byte protocol");
        std::ofstream(dir / "invalid.bin") << "bad";
        bool failed = false;
        try { restored.Open(dir / "invalid.bin"); } catch (const std::runtime_error&) { failed = true; }
        Check(failed && std::filesystem::file_size(dir / "invalid.bin") == 3, "invalid persistence is rejected without overwrite");
        std::filesystem::remove_all(dir);
        std::cout << checks << " IPL storage checks passed\n";
    } catch (const std::exception& e) {
        std::filesystem::remove_all(dir);
        std::cerr << "check " << checks << ": " << e.what() << '\n'; return 1;
    }
}
