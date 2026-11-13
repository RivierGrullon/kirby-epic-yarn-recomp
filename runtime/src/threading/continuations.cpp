// SPDX-License-Identifier: GPL-3.0-only
// Uses the pinned WiiCompiled HostContext backend (GPL-3.0 / libco ISC).
// SDK context layout and initialization: Swiftshine/key OSContext/OSThread (CC0).
#include "abi_bridge.h"
#include "host_context.h"
#include "generated/KirbyBootConfig.h"
#include "kirby/threading/continuations.h"
#include "kirby/threading/scheduler.h"
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace {
namespace C = KirbyBootConfig;
struct Continuation {
    uint32_t thread = 0;
    HostContext::Handle handle = nullptr;
    CpuContext initial{};
    CpuContext* activeCpu = nullptr;
    uint32_t fpControl = 0;
    uint32_t savedPc = 0, savedLr = 0, savedSp = 0;
    Kirby::InterruptState interrupts{};
    bool finished = false;
};
std::unordered_map<uint32_t, std::unique_ptr<Continuation>> threads;
Continuation* root = nullptr;
std::exception_ptr failure;
CpuContext failedCpu{};
uint64_t switches = 0;

void Transfer(Continuation& source, Continuation& target) {
    source.activeCpu = TryGetCpuContext();
    source.fpControl = MkwGetHostFpControl();
    source.interrupts = Kirby::GetInterruptState();
    Kirby::SetInterruptState(target.interrupts);
    g_currentCpuContext = target.activeCpu;
    MkwRestoreHostMxcsr(target.fpControl);
    ++switches;
    HostContext::Switch(target.handle);
    // A host context does not switch C++ thread-local variables. Restore the
    // exact scope pointer, including a suspended guest ABI wrapper's scope.
    g_currentCpuContext = source.activeCpu;
    Kirby::SetInterruptState(source.interrupts);
    MkwRestoreHostMxcsr(source.fpControl);
}

void Worker(void* argument) {
    auto& thread = *static_cast<Continuation*>(argument);
    auto& cpu = thread.initial;
    try {
        CpuContextScope scope(&cpu);
        InvokeIndirectCpu(cpu.pc, &cpu);
        // OSCreateThread supplies OSExitThread as the guest return address.
        // Preserve the entry function's r3 result for the original SDK exit.
        InvokeIndirectCpu(C::ExitThread, &cpu);
        throw std::runtime_error("Original OSExitThread returned to a retired native stack");
    } catch (...) {
        failedCpu = cpu;
        failure = std::current_exception();
    }
    // Leave the catch scope before switching: the C++ exception stack is TLS,
    // not fiber-local. The root rethrows after regaining its own host stack.
    Transfer(thread, *root);
    std::terminate();
}

std::unique_ptr<Continuation> NewWorker(uint32_t address, const CpuContext& caller) {
    Memory::GetPointer(address, 0x318);
    if (Memory::Read32(address+0x84) != C::ExitThread || Memory::Read16(address+0x1A2) & 2)
        throw std::runtime_error("New host stack requires an original OSCreateThread context");
    const auto top = Memory::Read32(address+0x304), bottom = Memory::Read32(address+0x308);
    const auto sp = Memory::Read32(address+4), pc = Memory::Read32(address+0x198);
    if (bottom >= top || sp < bottom || sp >= top || (sp & 7) ||
        !TranslatedFunctionRegistry::FindByAddressPtr(pc))
        throw std::runtime_error("New guest thread has an invalid stack or entry point");
    Memory::GetPointer(bottom, top-bottom);
    if (Memory::Read32(bottom) != 0xDEADBABE)
        throw std::runtime_error("New guest thread stack sentinel is damaged");
    auto result = std::make_unique<Continuation>();
    result->thread = address;
    auto& cpu = result->initial;
    for (unsigned i=0; i<32; ++i) cpu.gpr[i] = Memory::Read32(address+i*4);
    cpu.cr = Memory::Read32(address+0x80); cpu.lr = Memory::Read32(address+0x84);
    cpu.ctr = Memory::Read32(address+0x88); cpu.xer = Memory::Read32(address+0x8C);
    cpu.srr0 = cpu.pc = pc;
    cpu.srr1 = cpu.msr = Memory::Read32(address+0x19C);
    for (unsigned i=0; i<8; ++i) cpu.gqr[i] = Memory::Read32(address+0x1A4+i*4);
    if (Memory::Read16(address+0x1A2) & 1) {
        cpu.fpscr = Memory::Read32(address+0x194);
        for (unsigned i=0; i<32; ++i) cpu.fpr[i].raw = Memory::Read64(address+0x90+i*8);
    }
    cpu.hid0 = caller.hid0; cpu.hid1 = caller.hid1; cpu.hid2 = caller.hid2;
    result->activeCpu = &cpu;
    const auto originalFp = MkwGetHostFpControl();
    MkwApplyHostNiMode(cpu.fpscr);
    result->fpControl = MkwGetHostFpControl();
    MkwRestoreHostMxcsr(originalFp);
    result->handle = HostContext::Create(1024*1024, Worker, result.get());
    if (!result->handle) throw std::runtime_error("Unable to allocate a native guest-thread stack");
    std::cout << "[kirby] native thread stack: guest=0x" << std::hex << address
              << " entry=0x" << pc << std::dec << '\n';
    return result;
}
}

namespace Kirby {
uint64_t ThreadStackSwitches() { return switches; }
void PrepareThreadSwitch(uint32_t oldThread, uint32_t selected, CpuContext& cpu) {
    if (!root) {
        auto initial = std::make_unique<Continuation>();
        initial->thread = oldThread;
        initial->activeCpu = TryGetCpuContext();
        if (!HostContext::InitializeScheduler(&initial->handle))
            throw std::runtime_error("Unable to register the main native thread stack");
        root = initial.get();
        threads.emplace(oldThread, std::move(initial));
    }
    auto found = threads.find(oldThread);
    if (found == threads.end() || !HostContext::IsCurrent(found->second->handle))
        throw std::runtime_error("Guest scheduler and active native stack disagree");
    auto& old = *found->second;
    old.savedPc = Memory::Read32(oldThread+0x198);
    old.savedLr = Memory::Read32(oldThread+0x84);
    old.savedSp = Memory::Read32(oldThread+4);
    const auto state = Memory::Read16(oldThread+0x2C8);
    old.finished = state == 0 || state == 8;
    auto next = threads.find(selected);
    if (next != threads.end() && next->second->finished) {
        if (next->second.get() == root) throw std::runtime_error("Cannot restart the main host stack");
        HostContext::Destroy(next->second->handle);
        threads.erase(next);
        next = threads.end();
    }
    if (next == threads.end()) {
        threads.emplace(selected, NewWorker(selected, cpu));
    } else if (Memory::Read32(selected+0x198) != next->second->savedPc ||
               Memory::Read32(selected+0x84) != next->second->savedLr ||
               Memory::Read32(selected+4) != next->second->savedSp) {
        throw std::runtime_error("Suspended guest context was replaced; native continuation cannot be reused");
    }
}
void SwitchThreadStack(uint32_t oldThread, uint32_t selected, CpuContext& cpu) {
    auto& old = *threads.at(oldThread);
    Transfer(old, *threads.at(selected));
    if (failure) {
        cpu = failedCpu;
        std::rethrow_exception(std::exchange(failure, {}));
    }
    if (old.finished || Memory::Read32(0x800000E4) != oldThread ||
        Memory::Read32(0x800000D4) != oldThread || Memory::Read16(oldThread+0x2C8) != 2)
        throw std::runtime_error("Native stack resumed without its running SDK context");
}
void ShutdownThreadStacks() {
    if (!root) return;
    if (!HostContext::IsCurrent(root->handle)) std::terminate();
    for (auto& [address, thread] : threads)
        if (thread.get() != root) HostContext::Destroy(thread->handle);
    HostContext::ShutdownScheduler(root->handle);
    threads.clear(); root = nullptr; failure = {};
}
}
