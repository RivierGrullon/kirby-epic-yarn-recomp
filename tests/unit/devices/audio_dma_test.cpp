// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/devices/audio_dma.h"
#include <array>
#include <algorithm>
#include <vector>
#include <iostream>
#include <stdexcept>
namespace {
std::array<uint8_t,128> ram;
std::vector<uint8_t> received;
uint64_t ticks = 0;
unsigned checks = 0;
void Check(bool ok, const char* why) { ++checks; if (!ok) throw std::runtime_error(why); }
uint8_t* Resolve(uint32_t address, uint32_t size) { return uint64_t(address)+size <= ram.size() ? ram.data()+address : nullptr; }
}
int main() {
    try {
        Kirby::AudioDma audio;
        audio.Configure(Resolve, [] { return ticks; }, [](const uint8_t* p, uint32_t size) {
            received.insert(received.end(),p,p+size); return true;
        });
        for (unsigned i=0; i<ram.size(); ++i) ram[i]=i;
        auto read = [&](uint32_t address) { uint16_t v=0; Check(audio.Read16(address,v), "register read"); return v; };
        Check(audio.Write16(0xCC005030,0xE000) && read(0xCC005030)==0, "address high masks Wii physical bits");
        Check(audio.Write16(0xCC005032,7) && read(0xCC005032)==0, "source is 32-byte aligned");
        Check(!audio.Write16(0xCC005036,0x8000), "zero-length start is rejected");
        Check(audio.Write16(0xCC005032,128) && !audio.Write16(0xCC005036,0x8002) && read(0xCC005036)==0,
              "invalid source cannot start DMA");
        Check(audio.Write16(0xCC005032,0) && audio.Write16(0xCC005036,0x8002), "start two-block DMA");
        Check(read(0xCC00503A)==1 && !audio.Poll(), "count is zero-based and initial IRQ is delayed");
        ticks=16; Check(!audio.Poll(), "no early AID interrupt");
        ticks=17; Check(audio.Poll() && !audio.Poll() && received.empty(), "first interrupt fires once before samples");
        ticks=15187; Check(!audio.Poll() && received.empty(), "no early sample fetch");
        ticks=15188; Check(!audio.Poll() && received.size()==32 && read(0xCC00503A)==0, "eight samples consume one block");
        Check(audio.Write16(0xCC005032,64) && audio.Write16(0xCC005036,0x8001), "program next buffer while DMA runs");
        ticks=30375; Check(audio.Poll() && received.size()==64 && read(0xCC00503A)==0, "old buffer finishes before reload");
        Check(std::equal(received.begin(),received.end(),ram.begin()), "latched address preserves exact first-buffer bytes");
        ticks=45563; Check(audio.Poll() && received.size()==96 && std::equal(received.begin()+64,received.end(),ram.begin()+64),
                           "reload fetches the newly programmed buffer");
        Check(audio.Blocks()==3 && audio.Write16(0xCC005036,1), "stop retains programmed length");
        ticks=100000; Check(!audio.Poll() && received.size()==96, "disabled DMA does not consume samples");
        Check(!audio.Write16(0xCC00503A,0), "remaining count is read-only");
        audio.Reset(); Check(read(0xCC005036)==0 && read(0xCC005030)==0 && audio.Blocks()==0, "reset clears DMA state");
        received.clear();
        Check(audio.Write16(0xCC005032,0) && audio.Write16(0xCC005036,0x8002), "restart with a latched buffer");
        Check(audio.Write16(0xCC005032,64) && audio.Write16(0xCC005036,0x8001), "program a different reload buffer");
        ticks += 2*60750000;
        Check(audio.Poll() && audio.Blocks()==256 && audio.DroppedBlocks()==7744 && received.size()==256*32,
              "long pause advances DMA but bounds host audio catch-up");
        for (unsigned i=0; i<256; ++i)
            Check(std::equal(received.begin()+i*32,received.begin()+(i+1)*32,ram.begin()+64),
                  "skipped latched tail and reload cycles preserve the current source");
        Check(read(0xCC00503A)==0 && !audio.Poll(), "catch-up leaves the exact reload cursor without duplicate IRQ");
        ticks += 15188;
        Check(audio.Poll() && audio.Blocks()==257 && audio.DroppedBlocks()==7744,
              "normal DMA timing resumes after a long pause");
        audio.Reset(); Check(audio.DroppedBlocks()==0, "reset clears skipped-block diagnostics");
        received.clear();
        Check(audio.Write16(0xCC005032,0) && audio.Write16(0xCC005036,0x8002),
              "start native event-stepped DMA");
        ticks += 121500; // Four complete 16-sample buffers are overdue.
        Check(audio.Poll(true) && received.empty(),
              "initial AID callback runs before overdue buffer consumption");
        for (unsigned frame=0; frame<4; ++frame) {
            std::fill(ram.begin(),ram.end(),uint8_t(0x31+frame));
            Check(audio.Poll(true) && received.size()==(frame+1)*64 &&
                  std::all_of(received.begin()+frame*64,received.end(),
                              [frame](uint8_t sample) { return sample==0x31+frame; }),
                  "each overdue buffer can be refilled before the next buffer is consumed");
        }
        Check(!audio.Poll(true) && audio.Blocks()==8 && !audio.DroppedBlocks(),
              "event-stepped DMA catches up without repeating a stale buffer or adding samples");
        std::cout << checks << " audio DMA checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
