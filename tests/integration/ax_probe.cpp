// SPDX-License-Identifier: GPL-3.0-only
#include "abi_bridge.h"
#include "generated/KirbyBootConfig.h"
#include "kirby/devices/ax.h"
#include "kirby/devices/interrupts.h"
#include "kirby/threading/scheduler.h"
#include "hle/audio/ax_internal.h"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace Kirby {
void ProbeAx(CpuContext& cpu) {
    namespace C = KirbyBootConfig;
    auto check = [](bool value, const char* why) { if (!value) throw std::runtime_error(why); };
    InvokeIndirectCpu(C::InitRegisters, &cpu);
    const auto stack = cpu.gpr[1];
    cpu.msr = 0x9032;
    Memory::Write32(C::ExceptionTablePointer, 0x80003000);
    InvokeIndirectCpu(C::InterruptInit, &cpu);
    InvokeIndirectCpu(C::ThreadInit, &cpu);
    const auto context = Memory::Read32(0x800000D4);
    std::array<uint8_t, 32> foreignGlobals{};
    std::memcpy(foreignGlobals.data(), Memory::GetPointer(0x80386608, foreignGlobals.size()), foreignGlobals.size());
    const auto irqs = DeliveredInterrupts();
    InvokeIndirectCpu(C::AxOutInitDsp, &cpu);
    check(cpu.gpr[1] == stack && cpu.gpr[2] == 0x808EE8E0 && cpu.gpr[13] == 0x808E73E0 &&
          Memory::Read32(0x800000D4) == context && DeliveredInterrupts() == irqs+1,
          "AX task boot did not restore the interrupted CPU/context");
    check(Interrupts().Dsp().TaskBootCount() == 1 && Memory::Read32(C::AxInitFlag) == 1 &&
          Memory::Read32(C::AxTask) == 1 && !(Memory::Read16(context+0x1A2) & 2),
          "Original DSP handler and AX init callback did not complete");
    check(std::memcmp(foreignGlobals.data(), Memory::GetPointer(0x80386608, foreignGlobals.size()), foreignGlobals.size()) == 0,
          "Hardware mixer initialization modified another title's globals");

    Memory::Write8(0x91010000, 0x5A);
    Memory::Write8(0x93FFFFFF, 0xA5);
    check(AxDspHle::ReadAramByte(0x11010000) == 0x5A &&
          AxDspHle::ReadAramByte(0x13FFFFFF) == 0xA5,
          "AX voice sample fetch cannot read retail MEM2");
    check(AxDspHle::MixResolveRange(0x14000000, 1) == nullptr &&
          AxDspHle::MixResolveRange(0x13FFFFFF, 2) == nullptr,
          "AX sample range exceeds mapped retail MEM2");

    // Synthetic SETUP/OUTPUT/END exercises the real mixer after original boot.
    // Each bus is a 32-bit initial value and signed 16-bit increment.
    constexpr uint32_t setup=0x81010000, list=0x81010100, output=0x81011000, surround=0x81012000;
    std::memset(Memory::GetPointer(setup, 120), 0, 120);
    Memory::Write32(setup, 1000); Memory::Write32(setup+6, 2000);
    const std::array<uint16_t, 10> words{0,0x8101,0,0xB,0x8000,0x8101,0x2000,0x8101,0x1000,0xE};
    for (unsigned i=0; i<words.size(); ++i) Memory::Write16(list+i*2, words[i]);
    auto send = [](uint32_t mail) { Memory::Write16(0xCC005000, mail >> 16); Memory::Write16(0xCC005002, mail); };
    auto reply = [&] {
        const auto high=Memory::Read16(0xCC005004);
        check(high & 0x8000, "AX task reply is absent");
        const auto mail=(uint32_t(high)<<16)|Memory::Read16(0xCC005006);
        Memory::Write16(0xCC00500A, (Memory::Read16(0xCC00500A) & ~0x28u) | 0x80);
        return mail;
    };
    send(0xBABE0000 | words.size()); send(list);
    for (unsigned i=0; i<96; ++i)
        check(Memory::Read16(output+i*4) == 2000 && Memory::Read16(output+i*4+2) == 1000 &&
              Memory::Read32(surround+i*4) == 0, "AX output did not contain the computed stereo samples");
    check(reply() == 0xDCD10004 && reply() == 0xDCD10002, "AX sync/yield replies are out of order");
    send(0xCDD10000);
    check(reply() == 0xDCD10001, "AX resume acknowledgment is missing");

    // Retail AXPB offsets: Swiftshine/key revolution/AX/AXPB.h (CC0).
    // A constant PCM16 voice in MEM2, unity envelope/channel gains, no SRC.
    constexpr uint32_t pb = 0x81013000, sample = 0x11010000;
    std::memset(Memory::GetPointer(pb, 0x140), 0, 0x140);
    std::memset(Memory::GetPointer(setup, 120), 0, 120);
    Memory::Write16(pb+8, 2); Memory::Write32(pb+0xC, 3);
    Memory::Write16(pb+0x10, 1);
    for (auto offset : {0x14, 0x18, 0x6A}) Memory::Write16(pb+offset, 0x8000);
    Memory::Write16(pb+0x6E, 1); Memory::Write16(pb+0x70, 0xA);
    Memory::Write32(pb+0x72, sample/2);
    Memory::Write32(pb+0x76, sample/2+95);
    Memory::Write32(pb+0x7A, sample/2);
    Memory::Write16(pb+0x9E, 0x800);
    for (unsigned i=0; i<96; ++i) Memory::Write16(sample+i*2, 1000);
    const std::array<uint16_t,13> voiceWords{0,0x8101,0,4,0x8101,0x3000,
        0xB,0x8000,0x8101,0x2000,0x8101,0x1000,0xE};
    for (unsigned i=0; i<voiceWords.size(); ++i) Memory::Write16(list+i*2, voiceWords[i]);
    send(0xBABE0000 | voiceWords.size()); send(list);
    for (unsigned i=0; i<192; ++i)
        check(Memory::Read16(output+i*2) == 1000, "MEM2 PCM voice did not reach stereo AX output");
    check(reply() == 0xDCD10004 && reply() == 0xDCD10002,
          "MEM2 voice task sync/yield replies are out of order");
    // A ready DSP reply must interrupt an enabled, running guest without an
    // artificial scheduler-idle call. Keep EE masked first to verify deferral.
    send(0xCDD10000);
    send(0xCDD10000);
    const auto beforeRestore = DeliveredInterrupts();
    const auto resume = cpu.lr;
    cpu.msr &= ~0x8000u;
    cpu.gpr[3] = 0;
    InvokeIndirectCpu(C::RestoreInterrupts, &cpu);
    check(DeliveredInterrupts() == beforeRestore && (Memory::Read16(0xCC005004) & 0x8000),
          "Masked OSRestoreInterrupts consumed a pending DSP reply");
    cpu.gpr[3] = 1;
    InvokeIndirectCpu(C::RestoreInterrupts, &cpu);
    check(DeliveredInterrupts() == beforeRestore+2 && !(Memory::Read16(0xCC005004) & 0x8000),
          "OSRestoreInterrupts did not drain both pending DSP replies outside scheduler idle");
    check(cpu.gpr[1] == stack && cpu.gpr[2] == 0x808EE8E0 && cpu.gpr[13] == 0x808E73E0 &&
          cpu.lr == resume && (cpu.msr & 0x8000) && Memory::Read32(0x800000D4) == context &&
          !(Memory::Read16(context+0x1A2) & 2),
          "Native interrupt-enable boundary failed to restore the interrupted context");
    std::cout << "[kirby] pending DSP reply delivered at interrupt-enable boundary without scheduler idle\n";
    std::cout << "[kirby] retail MEM2 sample bounds and PCM voice stereo output verified\n";
    std::cout << "[kirby] original AX task boot and IRQ callback returned; 96 stereo samples mixed; task replies verified\n";
}
}
