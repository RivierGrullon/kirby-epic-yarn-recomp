// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "kirby/input/bindings.h"
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Kirby::PromptArt {
struct Image {
    unsigned width{}, height{};
    std::vector<uint8_t> pixels; // RGBA, row-major
};

using Glyphs = std::array<Image, GameActions.size()>;
Image Load(std::string_view name, std::string_view fallback);
void Start();
void Apply(const Glyphs& glyphs, const Image& dpad);
void Stop();
}
