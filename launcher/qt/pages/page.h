// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>

namespace Yarn {
enum Page { Home, Install, Display, Controls, Saves, Diagnostics, Basic, ControllerGuide, Count };
inline constexpr std::array Titles{"Play","Install","Display","Controls","Saves","Diagnostics","Basic settings","Controls"};
} // namespace Yarn
