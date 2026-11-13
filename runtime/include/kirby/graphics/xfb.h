// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Kirby {
struct XfbCopy {
    uint32_t address, width, height, stride, bytes;
    static XfbCopy Decode(const std::array<uint32_t, 256>& bp, uint32_t control);
};
struct TextureCopy {
    uint32_t address, format, x, y, sourceWidth, sourceHeight, width, height;
    uint32_t rowBytes, stride, rows, bytes;
    static TextureCopy Decode(const std::array<uint32_t,256>& bp, uint32_t control, bool depth);
};
// Converts Aurora's native GX RGBA8 readback (4x4 AR/GB tiles) to linear YUYV.
// Coefficients and normalization follow Dolphin's hardware-tested XFB encoder.
std::vector<uint8_t> EncodeXfb(const XfbCopy& copy, std::span<const uint8_t> tiledRgba8);
}
