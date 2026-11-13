// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace Kirby {
// Read-only metadata for the DATA partition selected by native boot.
// Format/title validation is not signature verification or title authorization.
class DiscTitleMetadata {
public:
    using View = std::array<uint8_t, 0xD8>;
    DiscTitleMetadata(const std::filesystem::path& root, uint64_t titleId);
    DiscTitleMetadata(std::span<const uint8_t> ticket, std::span<const uint8_t> tmd, uint64_t titleId);
    static std::optional<View> MakeTicketView(std::span<const uint8_t> ticket);
    const View& TicketView() const { return view_; }
    std::span<const uint8_t> Tmd() const { return tmd_; }
    uint64_t TitleId() const { return titleId_; }
    bool HasTimeLimits() const;
private:
    uint64_t titleId_;
    View view_{};
    std::vector<uint8_t> tmd_;
};
} // namespace Kirby
