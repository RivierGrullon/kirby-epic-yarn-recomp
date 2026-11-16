// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <algorithm>

namespace Kirby {
inline constexpr std::array ResolutionScales{1, 2, 3, 4, 5, 6, 8};
inline constexpr bool ValidResolutionScale(int scale) {
    return std::ranges::find(ResolutionScales, scale) != ResolutionScales.end();
}
inline constexpr const char* AntiAliasingName(int samples, bool fxaa) {
    return samples == 4 ? (fxaa ? "4x MSAA + FXAA" : "4x MSAA") : (fxaa ? "FXAA" : "Off");
}
} // namespace Kirby
