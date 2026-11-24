// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <span>

namespace Kirby {
// Observes original guest bytes before Aurora resolves replacements. Installed
// only while prompts are enabled. The renderer also runs without the game host.
inline void (*observeButtonPromptTexture)(unsigned format, unsigned width, unsigned height,
                                         std::span<const uint8_t> source) = nullptr;
}
