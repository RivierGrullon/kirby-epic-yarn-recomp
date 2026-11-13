// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/graphics/xfb.h"
#include <algorithm>
#include <stdexcept>

namespace Kirby {
XfbCopy XfbCopy::Decode(const std::array<uint32_t, 256>& bp, uint32_t control) {
    if (!(control & 0x4000)) throw std::runtime_error("GX texture-copy destination adapter is not implemented");
    if (control & ((3u << 7) | (3u << 12) | (1u << 9)))
        throw std::runtime_error("GX display-copy gamma, field or half-scale mode is not implemented");
    const auto width = (bp[0x4A] & 0x3FF) + 1, height = ((bp[0x4A] >> 10) & 0x3FF) + 1;
    const auto stride = (bp[0x4D] & 0x3FF) * 32;
    if ((width & 1) || stride != width * 2)
        throw std::runtime_error("GX display-copy width/stride needs a padding adapter");
    if ((bp[0x4E] & 0x1FF) != 256)
        throw std::runtime_error("GX display-copy vertical scaling is not implemented");
    const auto address = (bp[0x4B] & 0xFFFFFF) << 5;
    const auto bytes = stride * height;
    if (uint64_t(address) + bytes > 0x20000000ull)
        throw std::runtime_error("GX display-copy destination overflows physical memory space");
    return {address, width, height, stride, bytes};
}
TextureCopy TextureCopy::Decode(const std::array<uint32_t,256>& bp, uint32_t control, bool depth) {
    if (control & (0x4000 | (3u<<7) | (3u<<12)))
        throw std::runtime_error("GX texture copy has unsupported display/gamma/field mode");
    const auto encoded=(control>>3)&15;
    const auto target=(encoded>>1)|((encoded&1)<<3);
    const bool intensity=control&0x8000;
    uint32_t format;
    if (depth) {
        switch(target) {
        case 0: format=0x30; break;
        case 1: case 8: format=0x11; break;
        case 3: format=0x13; break;
        case 6: format=0x16; break;
        case 9: format=0x39; break;
        case 10: format=0x3A; break;
        case 12: format=0x3C; break;
        default: throw std::runtime_error("GX depth-copy format is unsupported");
        }
    } else if (intensity) {
        if (target>3) throw std::runtime_error("GX intensity-copy format is invalid");
        format=target;
    } else {
        constexpr uint32_t formats[]{0x20,0x28,0x22,0x23,4,5,6,0x27,0x28,0x29,0x2A,0x2B,0x2C};
        if (target>=std::size(formats)) throw std::runtime_error("GX color-copy format is unsupported");
        format=formats[target];
    }
    const auto x=bp[0x49]&1023, y=(bp[0x49]>>10)&1023;
    const auto sw=(bp[0x4A]&1023)+1, sh=((bp[0x4A]>>10)&1023)+1;
    const auto width=sw >> ((control>>9)&1), height=sh >> ((control>>9)&1);
    if (!width || !height || x+sw>1024 || y+sh>1024)
        throw std::runtime_error("GX texture-copy extent is invalid");
    const auto low=format&15;
    const uint32_t tileWidth=low==0 || low==1 || low==2 || low==7 || (low>=8 && low<=10) ? 8 : 4;
    const uint32_t tileHeight=low==0 ? 8 : 4;
    const uint32_t rowBytes=((width+tileWidth-1)/tileWidth)*(low==6 ? 64 : 32);
    const auto rows=(height+tileHeight-1)/tileHeight;
    const auto stride=(bp[0x4D]&1023)*32;
    const auto address=(bp[0x4B]&0xFFFFFF)<<5;
    const uint64_t bytes=uint64_t(rows-1)*stride+rowBytes;
    if (uint64_t(address)+bytes>0x20000000ull)
        throw std::runtime_error("GX texture-copy destination overflows physical memory");
    return {address,format,x,y,sw,sh,width,height,rowBytes,stride,rows,uint32_t(bytes)};
}
std::vector<uint8_t> EncodeXfb(const XfbCopy& c, std::span<const uint8_t> input) {
    if (!c.width || !c.height || c.width > 1024 || c.height > 1024 || (c.width & 1) ||
        c.stride != c.width * 2 || c.bytes != uint64_t(c.stride) * c.height)
        throw std::runtime_error("Invalid XFB conversion dimensions");
    const size_t tilesX = (c.width + 3) / 4;
    if (input.size() != tilesX * ((c.height + 3) / 4) * 64)
        throw std::runtime_error("GX RGBA8 readback size differs from the XFB extent");
    auto color = [&](uint32_t x, uint32_t y) {
        const auto at = ((y / 4) * tilesX + x / 4) * 64 + ((y % 4) * 4 + x % 4) * 2;
        return std::array<int, 3>{input[at + 1], input[at + 32], input[at + 33]};
    };
    auto channel = [](int numerator, int denominator) -> uint8_t {
        // Dolphin rounds the normalized RGB dot product before writing UNORM8.
        // Keep those two quantization steps rather than substituting a generic
        // BT.601 library conversion with different rounding/offsets.
        const int value = (numerator + denominator / 2) / denominator;
        return std::clamp((value * 255 + 128) / 256, 0, 255);
    };
    auto luma = [&](const auto& rgb) { return channel(66*rgb[0] + 129*rgb[1] + 25*rgb[2] + 16*255, 255); };
    std::vector<uint8_t> result(c.bytes);
    for (uint32_t y = 0; y < c.height; ++y) for (uint32_t x = 0; x < c.width; x += 2) {
        const auto a = color(x, y), b = color(x + 1, y);
        const int r = a[0] + b[0], g = a[1] + b[1], blue = a[2] + b[2];
        const auto at = size_t(y) * c.stride + x * 2;
        result[at] = luma(a);
        result[at + 1] = channel(-38*r - 74*g + 112*blue + 128*510, 510);
        result[at + 2] = luma(b);
        result[at + 3] = channel(112*r - 94*g - 18*blue + 128*510, 510);
    }
    return result;
}
}
