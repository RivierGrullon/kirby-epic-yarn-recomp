// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/graphics/gx_fifo.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

extern "C" void GX_HLE_FIFO_Write8(uint8_t);
extern "C" void GX_HLE_FIFO_Write16(uint16_t);
extern "C" void GX_HLE_FIFO_Write32(uint32_t);
extern "C" void GX_HLE_FIFO_WriteFloat(float);
extern "C" void GX_HLE_FIFO_WriteBurst(const uint8_t*, uint32_t);
namespace {
unsigned checks;
std::array<uint8_t, 256> ram;
uint8_t* Resolve(uint32_t address, uint32_t size) {
    return uint64_t(address) + size <= ram.size() ? ram.data() + address : nullptr;
}
void Check(bool value, const char* reason) { ++checks; if (!value) throw std::runtime_error(reason); }
template<class F> void Throws(F fn, const char* reason) {
    bool failed = false;
    try { fn(); } catch (const std::runtime_error&) { failed = true; }
    Check(failed, reason);
}
void Word(Kirby::GxFifo& fifo, uint32_t offset, uint32_t value) {
    Check(fifo.Write16(0xCC000000 + offset, value), "CP low register accepted");
    Check(fifo.Write16(0xCC000002 + offset, value >> 16), "CP high register accepted");
}
void Setup(Kirby::GxFifo& fifo) {
    fifo.Reset();
    fifo.SetWritePipeAddress(0x0C008000);
    Word(fifo, 0x20, 32); Word(fifo, 0x24, 96);
    Word(fifo, 0x28, 64); Word(fifo, 0x2C, 32);
    Word(fifo, 0x34, 32); Word(fifo, 0x38, 32);
    Check(fifo.Write32(0xCC00300C, 32) && fifo.Write32(0xCC003010, 96) &&
          fifo.Write32(0xCC003014, 32), "PI FIFO configured");
}
}
int main() {
    try {
        Kirby::GxFifo fifo;
        uint16_t half = 0; uint32_t word = 0;
        Check(fifo.WritePipeAddress() == 0, "write-pipe address resets");
        Throws([&] { const uint8_t byte = 0; fifo.Gather({&byte, 1}); }, "gather requires configured SPR 921");
        Throws([&] { fifo.SetWritePipeAddress(0x12340000); }, "unsupported write-pipe address fails explicitly");
        fifo.SetWritePipeAddress(0x0C008001);
        Check(fifo.WritePipeAddress() == 0x0C008000, "read-only BNE bit cannot be set by writing WPAR");
        Kirby::PixelEngine pe;
        Check(pe.Read16(0xCC00100A, half) && half == 0 && !pe.TokenInterrupt() && !pe.FinishInterrupt(), "PE reset masks both interrupts");
        pe.SetToken(0xABCD, true); pe.Finish();
        Check(!pe.TokenInterrupt() && !pe.FinishInterrupt(), "PE events retain pending state while masked");
        pe.Write16(0xCC00100A, 3);
        Check(pe.TokenInterrupt() && pe.FinishInterrupt() && pe.Read16(0xCC00100E, half) && half == 0xABCD, "PE enable exposes pending events and token");
        pe.SetToken(0x2345, false);
        Check(pe.TokenInterrupt(), "non-interrupting token does not clear an earlier pending event");
        pe.Write16(0xCC00100A, 7);
        Check(!pe.TokenInterrupt() && pe.FinishInterrupt() && pe.Read16(0xCC00100A, half) && half == 3, "token ACK preserves finish and clears write-only bit");
        pe.Write16(0xCC00100A, 11);
        Check(!pe.FinishInterrupt(), "finish ACK clears its own event");
        Check(pe.Write16(0xCC001004, 0x1234) && pe.Read16(0xCC001004, half) && half == 0x1234, "PE access configuration retained");
        Check(!pe.Write16(0xCC00100E, 0) && !pe.Read16(0xCC001010, half) && !pe.Read16(0xCC001018, half), "PE token writes and renderer-dependent queries rejected");
        Check(fifo.Status() == 12 && !fifo.Interrupt(), "reset is idle without interrupts");
        Check(!fifo.Write16(0xCC000000, 0) && !fifo.Read16(0xCC000008, half) &&
              !fifo.Write16(0xCC000021, 1) && !fifo.Read32(0xCC003018, word), "unsupported MMIO rejected");
        Word(fifo, 0x20, 0xFFFFFFFF);
        Check(fifo.Base() == 0x1FFFFFE0, "CP Wii physical address and alignment mask");
        Check(fifo.Write32(0xCC00300C, 0xFFFFFFFF) && fifo.Read32(0xCC00300C, word) &&
              word == 0x1FFFFFE0, "PI uses the same address mask");
        Check(fifo.Write16(0xCC000002, 0xFFC0) && fifo.Control() == 0, "reserved control bits masked");
        Setup(fifo);
        std::vector<uint8_t> received;
        fifo.Configure(Resolve, [&](auto bytes) { received.insert(received.end(), bytes.begin(), bytes.end()); });
        fifo.Write16(0xCC000002, 0x19);
        Check(fifo.Interrupt() && (fifo.Status() & 2), "empty FIFO under low watermark asserts enabled PI source");
        std::array<uint8_t, 128> data;
        for (unsigned i = 0; i < data.size(); ++i) data[i] = i;
        fifo.Gather(std::span(data).first(31));
        Check(fifo.Bursts() == 0 && received.empty(), "partial gather does not reach GPU");
        Check(fifo.WritePipeAddress() == 0x0C008000, "partial gather is not an outstanding bus transfer");
        fifo.Gather(std::span(data).subspan(31));
        Check(fifo.Bursts() == 4 && fifo.ConsumedBursts() == 4 && fifo.Distance() == 0 &&
              std::equal(data.begin(), data.end(), received.begin(), received.end()), "ordered bursts survive CPU and GPU ring wrap");
        Check(fifo.Read32(0xCC003014, word) && word == 64 &&
              std::equal(data.begin() + 96, data.end(), ram.begin() + 32), "ring memory and write pointer wrap at inclusive end");

        Setup(fifo); fifo.Configure(Resolve); fifo.Write16(0xCC000002, 16);
        fifo.Gather(std::span(data).first(96));
        Check(fifo.Distance() == 96 && (fifo.Status() & 1) && !fifo.Interrupt(), "disabled reader retains data and overflow level");
        Throws([&] { fifo.Gather(std::span(data).first(32)); }, "full FIFO never silently overwrites unread data");
        Check(fifo.Distance() == 96 && fifo.Bursts() == 3, "failed overflow preserves queue");
        Throws([&] { fifo.Write16(0xCC000002, 21); }, "enabling reader without consumer stops explicitly");
        Check(fifo.Distance() == 96 && fifo.ConsumedBursts() == 0 && fifo.Interrupt(), "unhandled commands are not reported consumed");

        Setup(fifo); fifo.Configure(Resolve, [](auto) {});
        Word(fifo, 0x3C, 32); fifo.Write16(0xCC000002, 0x33);
        fifo.Gather(std::span(data).first(32));
        Check(fifo.Distance() == 32 && fifo.Interrupt() && (fifo.Status() & 16), "breakpoint stops before consumption and asserts interrupt");
        fifo.Write16(0xCC000002, 0x11);
        Check(fifo.Distance() == 0 && !fifo.Interrupt(), "disabling breakpoint resumes queued data");
        fifo.Write16(0xCC000004, 3);
        Check(fifo.Read16(0xCC000004, half) && half == 0, "clear strobe reads zero");

        Setup(fifo); fifo.Configure(Resolve); fifo.Write16(0xCC000002, 0);
        fifo.Gather(std::span(data).first(32));
        Check(fifo.Bursts() == 1 && fifo.Distance() == 0, "unlinked CPU FIFO only writes RAM");
        Setup(fifo); fifo.Configure(Resolve); fifo.Write16(0xCC000002, 16);
        fifo.Write32(0xCC003014, 0x100);
        Throws([&] { fifo.Gather(std::span(data).first(32)); }, "out-of-ring pointer rejected");
        Setup(fifo); fifo.Configure(Resolve); fifo.Write16(0xCC000002, 16);
        fifo.Write32(0xCC003014, 64);
        Throws([&] { fifo.Gather(std::span(data).first(32)); }, "linked pointer mismatch rejected");

        auto& exported = Kirby::GraphicsFifo(); Setup(exported);
        received.clear(); exported.Configure(Resolve, [&](auto bytes) { received.insert(received.end(), bytes.begin(), bytes.end()); });
        exported.Write16(0xCC000002, 17);
        GX_HLE_FIFO_Write8(0x61); GX_HLE_FIFO_Write32(0x12345678);
        GX_HLE_FIFO_Write16(0xABCD); GX_HLE_FIFO_WriteFloat(1.0f);
        std::array<uint8_t, 21> padding{}; GX_HLE_FIFO_WriteBurst(padding.data(), padding.size());
        const std::array<uint8_t, 11> expected{0x61,0x12,0x34,0x56,0x78,0xAB,0xCD,0x3F,0x80,0,0};
        Check(received.size() == 32 && std::equal(expected.begin(), expected.end(), received.begin()), "all generated FIFO exports preserve big-endian bytes");
        exported.Reset();
        Check(exported.Bursts() == 0 && exported.Distance() == 0 && exported.Control() == 0, "reset clears transport");
        std::cout << checks << " GX FIFO checks passed\n";
    } catch (const std::exception& e) { std::cerr << "check " << checks << ": " << e.what() << '\n'; return 1; }
}
