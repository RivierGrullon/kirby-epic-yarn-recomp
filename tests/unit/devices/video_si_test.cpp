// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/devices/video_si.h"
#include <iostream>
#include <stdexcept>
namespace {
unsigned checks;
void Check(bool value, const char* why) { ++checks; if (!value) throw std::runtime_error(why); }
uint32_t Read(Kirby::VideoSi& d, uint32_t a) {
    uint32_t v = 0xBAD; Check(d.Read32(a, v), "register decode"); return v;
}
uint16_t Half(Kirby::VideoSi& d, uint32_t a) {
    uint16_t v = 0xBAD; Check(d.Read16(a, v), "halfword decode"); return v;
}
}
int main() {
    try {
        Kirby::VideoSi d;
        Check(Half(d, 0xCC00206C) == 1 && Half(d, 0xCC002002) == 1, "NTSC boot clock/display configuration");
        Check(Half(d, 0xCC00202C) == 1 && Half(d, 0xCC00202E) == 1, "initial beam position");
        Check(d.Write32(0xCC002030, 0x10020001), "program display interrupt at line 2");
        d.Advance(858 * 2);
        Check(Half(d, 0xCC00202C) == 2 && d.VideoInterrupt(), "beam time raises programmed VI source");
        Check(d.Write16(0xCC002030, 0x1002) && !d.VideoInterrupt(), "VI status clears on direct write");
        Check(!d.Write16(0xCC00202C, 22), "unsupported beam relocation rejected");
        Check(d.Write16(0xCC002002, 3) && Half(d, 0xCC002002) == 1 && Read(d, 0xCC002030) == 0,
              "VI reset clears interrupt registers and self-clears RST");
        d.Reset();
        d.Advance(858 * 16);
        Check(d.PollCount() == 1 && (Read(d, 0xCD006438) & 0x08080808) == 0x08080808,
              "VI timing polls empty SI ports with NOREP");
        Check(Read(d, 0xCD006404) == 0xC0000000, "empty-port poll latches input error");
        Check(d.Write32(0xCD006438, 0x08000000) && !(Read(d, 0xCD006438) & 0x08000000), "channel-specific NOREP W1C");
        Check(Read(d, 0xCD006438) & 0x00080000, "acknowledging channel zero preserves channel one");
        Check(d.Write32(0xCD006480, 0x12345678), "SI communication RAM write");
        Check(Half(d, 0xCD006480) == 0x1234 && Half(d, 0xCD006482) == 0x5678, "SI buffer is big endian");
        Check(d.Write16(0xCD006482, 0xABCD) && Read(d, 0xCD006480) == 0x1234ABCD, "SI halfword write preserves adjacent bytes");
        for (unsigned channel = 0; channel < 4; ++channel) {
            Check(d.Write32(0xCD006434, 0xC0010301 | (channel << 1)), "start type query with TC enabled");
            Check((Read(d, 0xCD006434) & 0xE0000001) == 0xE0000000 && d.SerialInterrupt(),
                  "empty device completes with COMERR and TC, without active TSTART");
            Check(Read(d, 0xCD006438) & (0x08000000 >> (channel * 8)), "transfer failure marks the correct channel");
            Check(d.Write32(0xCD006434, 0xC0000000) && !d.SerialInterrupt(), "TC acknowledgment drops SI source");
        }
        Check(d.TransferCount() == 4 && Read(d, 0xCD006480) == 0x1234ABCD, "empty devices cannot invent ID bytes");
        uint32_t unchanged = 7;
        Check(!d.Read32(0xCD006440, unchanged) && unchanged == 7, "reserved SI address rejected");
        Check(!d.Write16(0xCD006434, 0) && !d.Write32(0xCD006481, 0), "invalid register widths/alignment rejected");
        d.Reset();
        Check(d.TransferCount() == 0 && d.PollCount() == 0 && Read(d, 0xCD006434) == 0, "reset clears SI state");
        d.Reset();
        Check(d.Output().height == 0, "boot VI is blank despite its enabled timing generator");
        d.Write16(0xCC002000, (240 << 4) | 6);
        d.Write16(0xCC002048, (40 << 8) | 80);
        d.Write32(0xCC00201C, 0x10000000 | (0x10096860 >> 5));
        d.Write32(0xCC002024, (0x10096D60 >> 5));
        const auto top = d.Output();
        Check(top.address == 0x10096860 && top.width == 640 && top.height == 240 &&
              top.stride == 2560 && top.enabled && top.xOffset == 0, "VI decodes paged MEM2 top-field layout");
        unsigned observed = 0;
        Kirby::VideoOutput field;
        d.ObserveVideo([&](const auto& output) { ++observed; field = output; });
        d.Advance(858 * 1005);
        Check(observed == 1 && field.field == 1 && field.address == 0x10096D60,
              "bottom field inherits top-field page-offset mode and notifies scanout");
        d.Write16(0xCC002002, 0);
        d.Advance(858 * 2000);
        Check(observed == 1 && !d.Output().enabled, "disabled VI cannot submit another field");
        d.Reset();
        d.Advance(858 * 525);
        Check(observed == 1 && d.Output().field == 1, "reset clears scanout observer but preserves timing");
        std::cout << checks << " video/SI checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << "check " << checks << ": " << error.what() << '\n'; return 1;
    }
}
