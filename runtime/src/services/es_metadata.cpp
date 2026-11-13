// SPDX-License-Identifier: GPL-3.0-only
// Serialization references Dolphin IOS/ES/Formats and Views; see THIRD_PARTY.md.
#include "kirby/services/es_metadata.h"
#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace Kirby {
namespace {
uint32_t Be32(const uint8_t* p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
uint64_t Be64(const uint8_t* p) { return uint64_t(Be32(p)) << 32 | Be32(p+4); }
std::vector<uint8_t> Read(const std::filesystem::path& path, size_t limit) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto size = file.tellg();
    if (!file || size < 0 || uint64_t(size) > limit)
        throw std::runtime_error("Cannot read bounded disc title metadata: " + path.string());
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), bytes.size()))
        throw std::runtime_error("Truncated disc title metadata: " + path.string());
    return bytes;
}
}

std::optional<DiscTitleMetadata::View> DiscTitleMetadata::MakeTicketView(std::span<const uint8_t> ticket) {
    if (ticket.size() != 0x2A4 || Be32(ticket.data()) != 0x10001 || ticket[0x1BC] != 0)
        return std::nullopt; // Only the retail RSA-2048 v0 ticket format is supported.
    View result{};
    result[0] = ticket[0x1BC];
    // IOS places the version in byte zero of a four-byte field, followed by
    // ticket ID through time limits. The encrypted title key is not in this range.
    std::copy_n(ticket.begin()+0x1D0, result.size()-4, result.begin()+4);
    return result;
}

DiscTitleMetadata::DiscTitleMetadata(const std::filesystem::path& root, uint64_t titleId)
    : DiscTitleMetadata(Read(root/"ticket.bin", 0x2A4), Read(root/"tmd.bin", 0x49E4), titleId) {}

bool DiscTitleMetadata::HasTimeLimits() const {
    for (unsigned i = 0; i < 8; ++i)
        if (Be32(view_.data()+0x98+i*8)) return true;
    return false;
}

DiscTitleMetadata::DiscTitleMetadata(std::span<const uint8_t> ticket, std::span<const uint8_t> tmd,
                                   uint64_t titleId) : titleId_(titleId) {
    const auto view = MakeTicketView(ticket);
    if (!view || Be64(ticket.data()+0x1DC) != titleId)
        throw std::runtime_error("Disc ticket format or title does not match native boot");
    if (tmd.size() < 0x1E4 || tmd.size() > 0x49E4 || Be32(tmd.data()) != 0x10001 ||
        tmd[0x180] != 0 || Be64(tmd.data()+0x18C) != titleId)
        throw std::runtime_error("Disc TMD format or title does not match native boot");
    const auto contents = uint32_t(tmd[0x1DE]) << 8 | tmd[0x1DF];
    if (tmd.size() < 0x1E4 + contents*36)
        throw std::runtime_error("Disc TMD content table is truncated");
    view_ = *view;
    tmd_.assign(tmd.begin(), tmd.end());
}
} // namespace Kirby
