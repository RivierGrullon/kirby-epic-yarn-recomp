// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
namespace Kirby {
class IplStorage {
public:
    IplStorage();
    void Open(const std::filesystem::path& path);
    void SetRtc(uint32_t seconds);
    bool Supports(uint32_t command, uint32_t cursor, uint32_t length, bool write) const;
    void Read(uint32_t command, uint32_t cursor, std::span<uint8_t> output) const;
    void Write(uint32_t command, uint32_t cursor, std::span<const uint8_t> input);
private:
    void Save(const std::array<uint8_t, 68>& image) const;
    std::array<uint8_t, 68> bytes_{}; // RTC followed by 64 bytes of SRAM.
    uint8_t flags_ = 0;
    std::filesystem::path path_;
};
} // namespace Kirby
