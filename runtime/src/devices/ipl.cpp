// SPDX-License-Identifier: GPL-3.0-only
// Settings layout checked against OSRtc.h and Dolphin's Sram.h (the latter
// credits libogc authors Michael Wiedenbauer and Dave Murphy). See THIRD_PARTY.md.
#include "kirby/devices/ipl.h"
#include <algorithm>
#include <fstream>
#include <stdexcept>
namespace Kirby {
IplStorage::IplStorage() {
    // Synthetic console settings: English, stereo, configured; no card IDs.
    // Checksum covers SRAM bytes 12..19, as in the SDK's UnlockSram.
    bytes_[4 + 19] = 0x2C;
    uint16_t sum = 0, inverse = 0;
    for (unsigned i = 4 + 12; i < 4 + 20; i += 2) {
        const uint16_t value = (uint16_t(bytes_[i]) << 8) | bytes_[i + 1];
        sum += value; inverse += static_cast<uint16_t>(~value);
    }
    bytes_[4] = sum >> 8; bytes_[5] = sum;
    bytes_[6] = inverse >> 8; bytes_[7] = inverse;
}
void IplStorage::SetRtc(uint32_t seconds) {
    for (unsigned i = 0; i < 4; ++i) bytes_[i] = seconds >> (24 - 8 * i);
}
void IplStorage::Open(const std::filesystem::path& path) {
    auto next = bytes_;
    const bool exists = std::filesystem::exists(path);
    if (exists) {
        if (std::filesystem::file_size(path) != 64) throw std::runtime_error("SRAM file must contain exactly 64 bytes");
        std::ifstream input(path, std::ios::binary);
        if (!input.read(reinterpret_cast<char*>(next.data() + 4), 64)) throw std::runtime_error("Cannot read SRAM file");
    }
    const auto oldPath = path_;
    path_ = path;
    try { if (!exists) Save(next); } catch (...) { path_ = oldPath; throw; }
    bytes_ = next;
}
void IplStorage::Save(const std::array<uint8_t, 68>& image) const {
    if (path_.empty()) return; // Standalone unit models are deliberately volatile.
    if (!path_.parent_path().empty()) std::filesystem::create_directories(path_.parent_path());
    auto temporary = path_; temporary += ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output.write(reinterpret_cast<const char*>(image.data() + 4), 64)) throw std::runtime_error("Cannot write SRAM temporary file");
    output.close();
    if (!output) throw std::runtime_error("Cannot close SRAM temporary file");
    std::filesystem::rename(temporary, path_);
}
bool IplStorage::Supports(uint32_t command, uint32_t cursor, uint32_t length, bool write) const {
    if (!length || bool(command & 0x80000000) != write) return false;
    const uint32_t address = (command >> 6) & 0x1FFFFFF;
    if (address == 0x840020) return cursor <= 4 && length <= 4 - cursor;
    if (address < 0x800000 || address >= 0x800044) return false;
    const auto offset = address - 0x800000;
    return cursor <= 68 - offset && length <= 68 - offset - cursor;
}
void IplStorage::Read(uint32_t command, uint32_t cursor, std::span<uint8_t> output) const {
    if (output.size() > 68 || !Supports(command, cursor, output.size(), false)) throw std::runtime_error("Unsupported IPL storage read");
    const auto address = (command >> 6) & 0x1FFFFFF;
    if (address == 0x840020) std::fill(output.begin(), output.end(), flags_);
    else std::copy_n(bytes_.begin() + address - 0x800000 + cursor, output.size(), output.begin());
}
void IplStorage::Write(uint32_t command, uint32_t cursor, std::span<const uint8_t> input) {
    if (input.size() > 68 || !Supports(command, cursor, input.size(), true)) throw std::runtime_error("Unsupported IPL storage write");
    const auto address = (command >> 6) & 0x1FFFFFF;
    if (address == 0x840020) flags_ = input.back();
    else {
        auto next = bytes_;
        std::copy(input.begin(), input.end(), next.begin() + address - 0x800000 + cursor);
        Save(next); // Persist before accepting the state change and completion.
        bytes_ = next;
    }
}
} // namespace Kirby
