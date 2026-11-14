// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/graphics/gx_stream.h"
#include <array>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks;
void Check(bool value, const char* reason) { ++checks; if (!value) throw std::runtime_error(reason); }
template<class F> void Throws(F fn, const char* reason) {
    bool threw = false; try { fn(); } catch (const std::runtime_error&) { threw = true; }
    Check(threw, reason);
}
struct Fixture {
    std::array<uint8_t, 128> memory{};
    std::vector<std::vector<uint8_t>> received;
    uint32_t stride = 4;
    Kirby::GxStream stream{
        [this](auto packet) { received.emplace_back(packet.begin(), packet.end()); },
        [this](auto) { return stride; },
        [this](uint32_t address, uint32_t size) -> std::span<const uint8_t> {
            if (uint64_t(address) + size > memory.size()) return {};
            return std::span(memory).subspan(address, size);
        }};
};
}
int main() {
    try {
        const std::vector<uint8_t> commands{0x08,0x30,0x12,0x34,0x56,0x78,
            0x61,0x41,0,0,1, 0x10,0,1,0x10,0x0A,0xAA,0xBB,0xCC,0xDD,1,2,3,4,
            0x20,0,1,0xB0,0, 0x48};
        for (size_t split = 0; split <= commands.size(); ++split) {
            Fixture f;
            f.stream.Feed(std::span(commands).first(split));
            f.stream.Feed(std::span(commands).subspan(split));
            Check(f.stream.PendingBytes() == 0 && f.received.size() == 5, "every byte split preserves packet boundaries");
            Check(f.received[2].size() == 13 && f.received[2].back() == 4, "multiword XF remains complete and big-endian");
        }
        Fixture oneByte;
        for (auto byte : commands) oneByte.stream.Feed({&byte, 1});
        Check(oneByte.received.size() == 5 && oneByte.stream.Packets() == 5, "single-byte feeds preserve all commands");
        const std::array<uint8_t, 9> call{0x40,0,0,0,32,0,0,0,32};
        Fixture dl;
        std::copy_n(commands.begin(), 11, dl.memory.begin() + 32);
        dl.stream.Feed(call);
        Check(dl.stream.DisplayLists() == 1 && dl.received[0][0] == 8 && dl.received[1][0] == 0x61,
              "display lists are expanded in order instead of discarded");
        Fixture nested;
        std::copy(call.begin(), call.end(), nested.memory.begin() + 64);
        auto outer = call; outer[4] = 64;
        nested.stream.Feed(outer);
        Check(nested.stream.DisplayLists() == 2, "nested display lists resolve guest memory");
        Fixture cycle; std::copy(call.begin(), call.end(), cycle.memory.begin() + 32);
        Throws([&] { cycle.stream.Feed(call); }, "recursive display list terminates at explicit bound");
        Fixture invalid;
        auto outside = call; outside[3] = 1;
        Throws([&] { invalid.stream.Feed(outside); }, "unmapped display-list span rejected");
        Fixture unaligned; auto odd = call; odd[4] = 33;
        Throws([&] { unaligned.stream.Feed(odd); }, "unaligned display list rejected");
        Fixture truncated; truncated.memory[63] = 0x61;
        Throws([&] { truncated.stream.Feed(call); }, "truncated display-list command cannot borrow from parent FIFO");
        Fixture draw;
        const std::array<uint8_t, 11> vertices{0x90,0,2,1,2,3,4,5,6,7,8};
        draw.stream.Feed(std::span(vertices).first(10));
        Check(draw.received.empty() && draw.stream.PendingBytes() == 10, "incomplete draw is retained");
        draw.stream.Feed(std::span(vertices).last(1));
        Check(draw.received.size() == 1 && draw.received[0].size() == 11, "vertex count and stride determine draw packet size");
        Fixture fragmented;
        std::vector<uint8_t> longDraw(3+4*100,0x12);
        longDraw[0]=0x90; longDraw[1]=0; longDraw[2]=100;
        for (size_t offset=0;offset<longDraw.size();offset+=32)
            fragmented.stream.Feed(std::span(longDraw).subspan(offset,std::min(size_t(32),longDraw.size()-offset)));
        Check(fragmented.received.size()==1 && fragmented.received[0]==longDraw,
              "fragmented vertex payload reaches the backend exactly once and byte-for-byte");
        Check(fragmented.stream.VertexSizeQueries()==1,
              "one draw header derives its stride once while awaiting FIFO fragments");
        Fixture baseline;
        baseline.stream.SetPacketLengthReuse(false);
        for (size_t offset=0;offset<longDraw.size();offset+=32)
            baseline.stream.Feed(std::span(longDraw).subspan(offset,std::min(size_t(32),longDraw.size()-offset)));
        Check(baseline.received==fragmented.received && baseline.stream.VertexSizeQueries()>1,
              "diagnostic baseline reparses headers but delivers identical packets");
        uint32_t changingStride=2;
        std::array<uint8_t,32> child{};
        const std::array<uint8_t,6> cp4{8,0x70,0,0,0,4};
        std::copy(cp4.begin(),cp4.end(),child.begin());
        child[6]=0x90; child[8]=2;
        std::vector<size_t> drawSizes;
        Kirby::GxStream changing([&](auto packet) {
            if (packet[0]==8) changingStride=packet[5];
            else if (packet[0]>=0x80) drawSizes.push_back(packet.size());
        },[&](auto) {return changingStride;},[&](uint32_t,uint32_t) {return std::span<const uint8_t>(child);});
        std::vector<uint8_t> mixed{0x90,0,2,1,2,3,4};
        mixed.insert(mixed.end(),call.begin(),call.end());
        mixed.insert(mixed.end(),{0x90,0,3}); mixed.resize(mixed.size()+12,0);
        for (auto byte:mixed) changing.Feed({&byte,1});
        Check(drawSizes==std::vector<size_t>{7,11,15} && changing.PendingBytes()==0,
              "CP changes in a nested list update the next draw without corrupting fragmented packet lengths");
        Check(changing.VertexSizeQueries()==3,
              "nested and following draw headers each derive their own current format");
        Fixture oversized; oversized.stride = 0xFFFFFFFF;
        Throws([&] { oversized.stream.Feed(vertices); }, "vertex-size arithmetic cannot wrap allocation bound");
        Fixture zero; zero.stride = 0;
        Throws([&] { zero.stream.Feed(vertices); }, "nonempty draw with zero stride rejected");
        Fixture unknown; const uint8_t bad = 0xFF;
        Throws([&] { unknown.stream.Feed({&bad, 1}); }, "unknown opcode rejected");
        Throws([&] { unknown.stream.Feed(commands); }, "failed stream cannot retry partially dispatched commands");
        unsigned calls = 0;
        Kirby::GxStream rejecting([&](auto) { ++calls; throw std::runtime_error("backend rejected"); }, [](auto) {return 4;}, {});
        Throws([&] { rejecting.Feed(commands); }, "backend failures propagate");
        Throws([&] { rejecting.Feed(commands); }, "backend failure poisons stream");
        Check(calls == 1 && rejecting.Packets() == 0, "failed packet is never replayed or reported completed");
        std::cout << checks << " GX stream checks passed\n";
    } catch (const std::exception& e) { std::cerr << "check " << checks << ": " << e.what() << '\n'; return 1; }
}
