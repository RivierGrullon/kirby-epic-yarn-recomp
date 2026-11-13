// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/graphics/xfb.h"
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks;
void Check(bool value, const char* why) { ++checks; if (!value) throw std::runtime_error(why); }
template<class F> void Throws(F fn) {
    bool threw = false; try { fn(); } catch (const std::runtime_error&) { threw = true; }
    Check(threw, "invalid copy/conversion must fail");
}
}
int main() {
    try {
        std::array<uint32_t, 256> bp{};
        bp[0x4A] = (479 << 10) | 639; bp[0x4B] = 0x10096860 >> 5;
        bp[0x4D] = 40; bp[0x4E] = 256;
        auto copy = Kirby::XfbCopy::Decode(bp, 0x4803);
        Check(copy.address == 0x10096860 && copy.width == 640 && copy.height == 480 &&
              copy.stride == 1280 && copy.bytes == 614400, "RK5E01 copy register layout");
        Check(Kirby::XfbCopy::Decode(bp,0x4003).bytes == copy.bytes, "display copy without clear uses the same XFB layout");
        for (const auto control : {0x803u, 0x4883u, 0x4A03u, 0x5803u})
            Throws([&] { Kirby::XfbCopy::Decode(bp, control); });
        bp[0x4E] = 128; Throws([&] { Kirby::XfbCopy::Decode(bp, 0x4803); });
        bp[0x4E] = 256; bp[0x4D] = 41; Throws([&] { Kirby::XfbCopy::Decode(bp, 0x4803); });
        bp[0x4D] = 40; bp[0x4B] = 0xFFFFFF; Throws([&] { Kirby::XfbCopy::Decode(bp, 0x4803); });
        bp[0x4B]=0x100000>>5; bp[0x4D]=82;
        auto texture=Kirby::TextureCopy::Decode(bp,0x63,false);
        Check(texture.format==6 && texture.width==640 && texture.height==480 &&
              texture.rowBytes==10240 && texture.stride==2624 && texture.rows==120,
              "texture layout decodes rotated target format and tile-row stride");
        texture=Kirby::TextureCopy::Decode(bp,0x263,false);
        Check(texture.width==320 && texture.height==240 && texture.rowBytes==5120 && texture.rows==60,
              "half-scale texture copy halves dimensions before tiling");
        texture=Kirby::TextureCopy::Decode(bp,0x8003,false);
        Check(texture.format==0 && texture.rowBytes==2560 && texture.rows==60,
              "intensity copy uses I4 tiling");
        texture=Kirby::TextureCopy::Decode(bp,0x63,true);
        Check(texture.format==0x16,"depth source selects Z24X8 copy format");
        Throws([&] { Kirby::TextureCopy::Decode(bp,0x4003,false); });
        Throws([&] { Kirby::TextureCopy::Decode(bp,0xE3,false); });
        bp[0x4B]=0xFFFFFF;
        Throws([&] { Kirby::TextureCopy::Decode(bp,0x63,false); });
        // Cross both tile and row boundaries; padding pixels must not escape.
        copy = {0, 6, 5, 12, 60};
        std::vector<uint8_t> tiled(256, 0xFF);
        const std::array<std::array<uint8_t, 3>, 5> colors{{{0,0,0},{255,255,255},{255,0,0},{0,255,0},{0,0,255}}};
        const std::array<std::array<uint8_t, 4>, 5> expected{{{16,128,16,128},{235,128,235,128},
            {82,90,82,239},{144,54,144,34},{41,239,41,110}}};
        for (unsigned y = 0; y < 5; ++y) for (unsigned x = 0; x < 6; ++x) {
            const auto at = ((y / 4) * 2 + x / 4) * 64 + ((y % 4) * 4 + x % 4) * 2;
            tiled[at + 1] = colors[y][0]; tiled[at + 32] = colors[y][1]; tiled[at + 33] = colors[y][2];
        }
        const auto encoded = Kirby::EncodeXfb(copy, tiled);
        Check(encoded.size() == 60, "XFB extent excludes tiled padding");
        for (unsigned y = 0; y < 5; ++y) for (unsigned x = 0; x < 6; x += 2)
            Check(std::equal(expected[y].begin(), expected[y].end(), encoded.begin() + y*12 + x*2),
                  "RGBA8 tiled samples become Y0 U Y1 V with Dolphin conversion coefficients");
        Kirby::XfbCopy pair{0,2,1,4,4};
        std::vector<uint8_t> mixed(64); mixed[1] = 255; mixed[35] = 255;
        Check(Kirby::EncodeXfb(pair, mixed) == std::vector<uint8_t>({82,164,41,174}),
              "chroma averages adjacent red and blue pixels");
        Throws([&] { Kirby::EncodeXfb(copy, std::span(tiled).first(255)); });
        copy.width = 5; Throws([&] { Kirby::EncodeXfb(copy, tiled); });
        std::cout << checks << " XFB layout/conversion checks passed\n";
    } catch (const std::exception& e) { std::cerr << "check " << checks << ": " << e.what() << '\n'; return 1; }
}
