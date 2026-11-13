// SPDX-License-Identifier: GPL-3.0-only
// SDK queue semantics: Swiftshine/key OSThread.c. Host continuation approach:
// WarioRecomp os_scheduler.cpp (GPL-3.0). All addresses are audited for RK5E01.
#include "hle_stubs.h"
#include "generated/KirbyBootConfig.h"
#include "kirby/threading/scheduler.h"
#include "kirby/threading/continuations.h"
#include "kirby/input/wpad.h"
#include "kirby/devices/interrupts.h"
#include "kirby/devices/video_si.h"
#include "kirby/services/ios_bridge.h"
#include <bit>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
namespace C = KirbyBootConfig;
constexpr uint32_t CurrentContext = 0x800000D4, CurrentThread = 0x800000E4;
constexpr uint32_t State = 0x2C8, Priority = 0x2D0, Queue = 0x2DC, Next = 0x2E0, Prev = 0x2E4;
struct InterruptReturn { CpuContext restored; };
Kirby::InterruptState interruptState;
uint64_t delivered = 0, wakeups = 0;
uint64_t alarms = 0;

void Call(CpuContext& cpu, uint32_t address, uint32_t a, uint32_t b = 0) {
    CpuContext scratch = cpu;
    scratch.gpr[3] = a; scratch.gpr[4] = b;
    CpuContextScope scope(&scratch);
    InvokeIndirectCpu(address, &scratch);
}
void SwitchCallback(CpuContext& cpu, uint32_t oldThread, uint32_t newThread) {
    const auto callback = Memory::Read32(C::SwitchThreadCallback);
    if (callback) Call(cpu, callback, oldThread, newThread);
}
void SaveException(const CpuContext& cpu, uint32_t address, uint32_t resume) {
    Memory::GetPointer(address, 0x2C8); // Validate the complete context before writing.
    for (unsigned i = 0; i < 32; ++i) Memory::Write32(address + i*4, cpu.gpr[i]);
    Memory::Write32(address + 0x80, cpu.cr); Memory::Write32(address + 0x84, cpu.lr);
    Memory::Write32(address + 0x88, cpu.ctr); Memory::Write32(address + 0x8C, cpu.xer);
    Memory::Write32(address + 0x198, resume); Memory::Write32(address + 0x19C, cpu.msr);
    Memory::Write16(address + 0x1A2, Memory::Read16(address + 0x1A2) | 2);
    for (unsigned i = 1; i < 8; ++i) Memory::Write32(address + 0x1A4 + i*4, cpu.gqr[i]);
    // The host retains the FPU state; the SDK's exception entry also leaves it
    // resident until an FPU context switch requests a save.
}
}

namespace Kirby {
InterruptState GetInterruptState() { return interruptState; }
void SetInterruptState(InterruptState state) { interruptState = state; }
uint64_t DeliveredInterrupts() { return delivered; }
uint64_t SchedulerWakeups() { return wakeups; }
static bool DispatchException(CpuContext& cpu, uint32_t guestContext, uint32_t resume, uint32_t handler, uint32_t type) {
    if (interruptState.context || !(cpu.msr & 0x8000)) return false;
    SaveException(cpu, guestContext, resume);
    const CpuContext saved = cpu;
    CpuContext interrupt = cpu;
    interrupt.msr &= ~0x8000u;
    interrupt.gpr[3] = type; interrupt.gpr[4] = guestContext;
    interrupt.pc = handler;
    struct Guard {
        Guard(uint32_t context, const CpuContext& saved) { interruptState = {context, &saved}; }
        ~Guard() { interruptState = {}; }
    } guard(guestContext, saved);
    CpuContextScope scope(&interrupt);
    try {
        InvokeIndirectCpu(handler, &interrupt);
    } catch (const InterruptReturn& returned) {
        if (returned.restored.pc != resume)
            throw std::runtime_error("IRQ handler changed its return PC; host continuation is unavailable");
        cpu = returned.restored;
        ++delivered;
        return true;
    }
    throw std::runtime_error("Original interrupt dispatcher returned without OSLoadContext");
}
bool DispatchInterrupt(CpuContext& cpu, uint32_t guestContext, uint32_t resume) {
    if (interruptState.context || !(cpu.msr & 0x8000)) return false;
    uint32_t cause = 0, mask = 0;
    KirbyMmioRead32(0xCC003000, cause);
    KirbyMmioRead32(0xCC003004, mask);
    if (!(cause & mask & ~0x10000u)) return false;
    return DispatchException(cpu, guestContext, resume, C::DispatchInterrupt, 4);
}
uint64_t DeliveredAlarms() { return alarms; }
bool DispatchAlarm(CpuContext& cpu, uint32_t guestContext, uint32_t resume) {
    if (interruptState.context || !(cpu.msr & 0x8000)) return false;
    const auto head = Memory::Read32(C::AlarmQueue);
    if (!head) return false;
    Memory::GetPointer(head, 0x2C);
    uint64_t ticks;
    for (;;) {
        const auto hi=PPC_Mftbu(), lo=PPC_Mftb();
        if (hi == PPC_Mftbu()) { ticks=(uint64_t(hi)<<32)|lo; break; }
    }
    // __OSGetSystemTime's two loads at 0x800030D8 are covered by the DOL audit.
    const auto now = static_cast<int64_t>(ticks + Memory::Read64(0x800030D8));
    if (now < static_cast<int64_t>(Memory::Read64(head+8))) return false;
    // SaveException already performs the assembly exception-entry context save;
    // enter the original C callback without saving handler scratch registers.
    const bool dispatched = DispatchException(cpu, guestContext, resume, C::AlarmCallback, 8);
    if (dispatched) ++alarms;
    return dispatched;
}
}

extern "C" void KirbyLoadContext(CpuContext* cpu) {
    const auto address = cpu->gpr[3];
    if (!interruptState.context || address != interruptState.context)
        throw std::runtime_error("OSLoadContext requires a registered host continuation outside IRQ return");
    Memory::GetPointer(address, 0x2C8);
    CpuContext restored = *interruptState.savedCpu;
    for (unsigned i = 0; i < 32; ++i) restored.gpr[i] = Memory::Read32(address + i*4);
    restored.cr = Memory::Read32(address + 0x80); restored.lr = Memory::Read32(address + 0x84);
    restored.ctr = Memory::Read32(address + 0x88); restored.xer = Memory::Read32(address + 0x8C);
    restored.srr0 = restored.pc = Memory::Read32(address + 0x198);
    restored.srr1 = restored.msr = Memory::Read32(address + 0x19C);
    for (unsigned i = 1; i < 8; ++i) restored.gqr[i] = Memory::Read32(address + 0x1A4 + i*4);
    Memory::Write16(address + 0x1A2, Memory::Read16(address + 0x1A2) & ~2u);
    throw InterruptReturn{restored};
}

extern "C" void KirbySelectThread(CpuContext* cpu) {
    const auto entry = *cpu;
    const auto old = Memory::Read32(CurrentThread);
    if (static_cast<int32_t>(Memory::Read32(C::SchedulerDisableCount)) > 0 ||
        Memory::Read32(CurrentContext) != old) { cpu->gpr[3] = 0; return; }
    if (!old) throw std::runtime_error("Scheduler has no active host thread continuation");
    Memory::GetPointer(old, 0x318);
    if (Memory::Read16(old + State) == 2) {
        const auto priority = Memory::Read32(old + Priority);
        if (priority >= 32) throw std::runtime_error("Guest thread priority exceeds SDK run queues");
        if (!entry.gpr[3] && priority <= std::countl_zero(Memory::Read32(C::RunQueueBits))) {
            cpu->gpr[3] = 0; return;
        }
        const auto queue = C::RunQueue + priority*8, tail = Memory::Read32(queue + 4);
        Memory::Write16(old + State, 1);
        Memory::Write32(old + Queue, queue);
        Memory::Write32(tail ? tail + Next : queue, old);
        Memory::Write32(old + Prev, tail); Memory::Write32(old + Next, 0);
        Memory::Write32(queue + 4, old);
        Memory::Write32(C::RunQueueBits, Memory::Read32(C::RunQueueBits) | (1u << (31-priority)));
        Memory::Write32(C::RunQueueHint, 1);
    }
    const bool preempted = Memory::Read16(old + 0x1A2) & 2;
    if (preempted && (interruptState.context != old || !interruptState.savedCpu))
        throw std::runtime_error("Preempted guest context has no matching native IRQ continuation");
    // Save the SDK-visible registers with the original code. The corresponding
    // host continuation is this native invocation, including its complete CPU snapshot.
    // Exception entry already saved a preempted context. Saving the handler's
    // scratch registers over it would destroy the interrupted thread's state.
    if (!preempted) Call(*cpu, C::SaveContext, old);
    if (!Memory::Read32(C::RunQueueBits)) {
        SwitchCallback(*cpu, old, 0);
        Memory::Write32(CurrentThread, 0);
        Call(*cpu, C::SetCurrentContext, C::IdleContext);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        cpu->msr |= 0x8000;
        while (!Memory::Read32(C::RunQueueBits)) {
            Kirby::DrainIosCompletions(cpu);
            Kirby::PollWpad(*cpu);
            Kirby::DispatchAlarm(*cpu, C::IdleContext, C::SelectThread + 0x150);
            Kirby::DispatchInterrupt(*cpu, C::IdleContext, C::SelectThread + 0x150);
            if (std::chrono::steady_clock::now() >= deadline)
                throw std::runtime_error("Scheduler idle: no runnable thread after five seconds of device/IRQ polling");
            if (!Memory::Read32(C::RunQueueBits)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        cpu->msr &= ~0x8000u;
        Call(*cpu, C::ClearContext, C::IdleContext);
    }
    const auto bits = Memory::Read32(C::RunQueueBits);
    const auto priority = std::countl_zero(bits);
    const auto queue = C::RunQueue + priority*8, selected = Memory::Read32(queue);
    if (!selected || Memory::Read16(selected + State) != 1 || Memory::Read32(selected + Queue) != queue)
        throw std::runtime_error("Scheduler run queue contains an invalid ready thread");
    if (selected != old) Kirby::PrepareThreadSwitch(old, selected, *cpu);
    const auto next = Memory::Read32(selected + Next);
    Memory::Write32(queue, next);
    if (next) Memory::Write32(next + Prev, 0);
    else {
        Memory::Write32(queue + 4, 0);
        Memory::Write32(C::RunQueueBits, bits & ~(1u << (31-priority)));
    }
    Memory::Write32(C::RunQueueHint, 0);
    Memory::Write32(selected + Queue, 0); Memory::Write16(selected + State, 2);
    SwitchCallback(*cpu, old, selected);
    Memory::Write32(CurrentThread, selected);
    Call(*cpu, C::SetCurrentContext, selected);
    if (selected != old) Kirby::SwitchThreadStack(old, selected, *cpu);
    *cpu = entry;
    cpu->gpr[3] = 0; // OSSaveContext's resumed path returns from SelectThread.
    ++wakeups;
}

#undef REGISTER_NATIVE_FUNCTION
#define REGISTER_NATIVE_FUNCTION(address, fn) \
    static AbiTrampoline<decltype(fn)> kirby_##fn(address, #fn, fn, FunctionKind::Native, \
        false, kPpcAllNonvolatileFprMask, 0, 0, &AbiRawCpuThunk<&fn>::Invoke)
static_assert(C::SelectThread == 0x8066B220u && C::LoadContext == 0x80664450u);
// WiiCompiled's effect analyzer uses the parameter name in this signature to
// inspect the function body, so it must match the definition exactly.
PPC_NATIVE_OVERRIDE_VOID(8066B220, KirbySelectThread, (CpuContext* cpu), (cpu));
PPC_NATIVE_OVERRIDE_VOID(80664450, KirbyLoadContext, (CpuContext* cpu), (cpu));
