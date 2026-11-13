// SPDX-License-Identifier: GPL-3.0-only
// Invokes the user's translated SDK; no game instructions are embedded here.
#include "abi_bridge.h"
#include "generated/KirbyBootConfig.h"
#include "kirby/threading/continuations.h"
#include "kirby/threading/scheduler.h"
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace Kirby {
void ProbeAlarms(CpuContext& cpu) {
    namespace C = KirbyBootConfig;
    auto check = [](bool value, const char* why) { if (!value) throw std::runtime_error(why); };
    InvokeIndirectCpu(C::InitRegisters, &cpu);
    cpu.msr = 0x9032;
    Memory::Write32(C::ExceptionTablePointer, 0x80003000);
    InvokeIndirectCpu(C::InterruptInit, &cpu);
    InvokeIndirectCpu(C::ThreadInit, &cpu);
    InvokeIndirectCpu(C::AlarmInit, &cpu);
    const auto thread = Memory::Read32(0x800000E4), stack = cpu.gpr[1];
    const auto before = DeliveredAlarms();
    for (uint32_t ticks : {0u, 60750u, 303750u}) {
        cpu.gpr[3] = 0; cpu.gpr[4] = ticks;
        const auto start = std::chrono::steady_clock::now();
        InvokeIndirectCpu(C::SleepTicks, &cpu);
        check(std::chrono::steady_clock::now()-start >= std::chrono::microseconds(ticks*1000000ull/60750000),
              "Original OSSleepTicks resumed before its deadline");
        check(cpu.gpr[1] == stack && Memory::Read32(0x800000E4) == thread &&
              Memory::Read32(0x800000D4) == thread && Memory::Read16(thread+0x2C8) == 2 &&
              Memory::Read32(thread+0x2CC) == 0 && Memory::Read32(C::AlarmQueue) == 0,
              "Sleep alarm did not restore the original thread and empty its queue");
    }
    check(DeliveredAlarms() == before+3, "Sleep alarms were lost or delivered twice");
    constexpr uint32_t alarm = 0x81001000;
    Memory::Write32(alarm,0); Memory::Write32(alarm+4,0);
    cpu.gpr[3]=alarm; cpu.gpr[5]=0; cpu.gpr[6]=60750000; cpu.gpr[7]=C::WPADSetCallbackByKPAD;
    InvokeIndirectCpu(C::SetAlarm, &cpu);
    check(!DispatchAlarm(cpu,thread,cpu.lr), "Future alarm fired early");
    Memory::Write64(alarm+8,0); // Make the test alarm overdue while EE is masked.
    cpu.msr &= ~0x8000u;
    check(!DispatchAlarm(cpu,thread,cpu.lr) && Memory::Read32(C::AlarmQueue)==alarm,
          "Masked alarm was consumed");
    cpu.gpr[3]=alarm; InvokeIndirectCpu(C::CancelAlarm, &cpu);
    cpu.msr |= 0x8000u;
    check(!DispatchAlarm(cpu,thread,cpu.lr) && Memory::Read32(C::AlarmQueue)==0,
          "Canceled alarm remained pending");
    std::cout << "[kirby] three original sleep alarms resumed; deadline, mask, cancellation and SDK context verified\n";
}
void ProbeThreads(CpuContext& cpu, bool fault) {
    namespace C = KirbyBootConfig;
    auto check = [](bool value, const char* reason) { if (!value) throw std::runtime_error(reason); };
    auto call = [&](uint32_t address, uint32_t arg=0) {
        cpu.gpr[3] = arg; InvokeIndirectCpu(address, &cpu); return cpu.gpr[3];
    };
    InvokeIndirectCpu(C::InitRegisters, &cpu);
    cpu.msr = 0x9032;
    InvokeIndirectCpu(C::ThreadInit, &cpu);
    const auto mainThread = Memory::Read32(0x800000E4), mainStack = cpu.gpr[1];
    constexpr uint32_t sleeper=0x81000000, leaf=0x81001000, queue=0x81002000;
    constexpr uint32_t used=0x808E6558, argument=0x13579BDF;
    for (unsigned i=14; i<32; ++i) {
        cpu.gpr[i] = 0xA5000000+i;
        cpu.fpr[i].raw = 0x400921FB54442D00ull+i;
    }
    for (unsigned i=1; i<8; ++i) cpu.gqr[i] = 0x00040004;
    cpu.fpscr = 4;
    MkwApplyHostNiMode(cpu.fpscr);
    const auto expected = cpu;
    const auto expectedFp = MkwGetHostFpControl();
    auto preserved = [&] {
        check(cpu.gpr[1] == mainStack && cpu.gpr[2] == expected.gpr[2] && cpu.gpr[13] == expected.gpr[13],
              "Thread switch changed the main stack or SDA registers");
        for (unsigned i=14; i<32; ++i)
            check(cpu.gpr[i] == expected.gpr[i] && cpu.fpr[i].raw == expected.fpr[i].raw,
                  "Thread switch corrupted nonvolatile registers");
        for (unsigned i=1; i<8; ++i) check(cpu.gqr[i] == expected.gqr[i], "Thread switch changed GQR");
        check(cpu.fpscr == expected.fpscr && (MkwGetHostFpControl() & ~0x3Fu) == (expectedFp & ~0x3Fu) && TryGetCpuContext() == &cpu,
              "Thread switch lost the active CPU pointer or FP environment");
        check(Memory::Read32(0x800000E4) == mainThread && Memory::Read32(0x800000D4) == mainThread &&
              Memory::Read16(mainThread+0x2C8) == 2 && Memory::Read32(C::RunQueueBits) == 0,
              "SDK queues disagree with the resumed main thread");
    };
    auto create = [&](uint32_t thread, uint32_t entry, uint32_t arg, uint32_t top) {
        cpu.gpr[3]=thread; cpu.gpr[4]=entry; cpu.gpr[5]=arg;
        cpu.gpr[6]=top; cpu.gpr[7]=0x4000; cpu.gpr[8]=15; cpu.gpr[9]=0;
        InvokeIndirectCpu(C::CreateThread, &cpu);
        check(cpu.gpr[3] == 1 && Memory::Read32(thread+0x84) == C::ExitThread,
              "Original OSCreateThread did not establish a guest return context");
    };
    if (fault) {
        create(leaf, C::WPADRead, 0, 0x81024000);
        call(C::ResumeThread, leaf); // Null output must propagate from the worker to main.
        throw std::runtime_error("Worker failure did not reach the root stack");
    }
    Memory::Write32(queue,0); Memory::Write32(queue+4,0);
    create(sleeper, C::SleepThread, queue, 0x81018000);
    call(C::ResumeThread, sleeper);
    check(ThreadStackSwitches() == 2 && Memory::Read16(sleeper+0x2C8) == 4 &&
          Memory::Read32(queue) == sleeper && Memory::Read32(sleeper+0x2DC) == queue,
          "Original OSSleepThread did not yield its native stack onto the wait queue");
    preserved();
    call(C::WakeupThread, queue);
    check(ThreadStackSwitches() == 4 && Memory::Read16(sleeper+0x2C8) == 8 && Memory::Read32(queue) == 0,
          "Woken thread did not resume and finish through original OSExitThread");
    preserved();
    // A short original leaf proves entry arguments and thread return values.
    // Recreate the completed thread at the same address to exercise stack retirement.
    for (unsigned pass=0; pass<2; ++pass) {
        Memory::Write32(used,0);
        create(leaf, C::WPADSetCallbackByKPAD, argument+pass, 0x81024000);
        call(C::ResumeThread, leaf);
        check(Memory::Read32(used) == argument+pass && Memory::Read16(leaf+0x2C8) == 8 &&
              Memory::Read32(leaf+0x2D8) == argument+pass,
              "Thread entry argument, SDK exit result or stack recreation failed");
        preserved();
    }
    check(ThreadStackSwitches() == 8, "Unexpected native thread switch count");
    std::cout << "[kirby] original SDK threads: sleep, wake, exit and CPU state verified\n";
}
}
