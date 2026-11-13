// SPDX-License-Identifier: GPL-3.0-only
#include "hle_stubs.h"
#include "generated/KirbyBootConfig.h"
#include "kirby/boot/boot_memory.h"
#include "kirby/services/ios_bridge.h"
#include "kirby/input/wpad.h"
#include "kirby/threading/scheduler.h"
#include <deque>
#include <stdexcept>

namespace {
uint8_t* Resolve(uint32_t address, uint32_t size) {
    if (!address || uint64_t(address) + size > UINT32_MAX) return nullptr;
    for (const uint32_t base : {0u, 0x80000000u, 0xC0000000u})
        if (address >= base && uint64_t(address) + size <= uint64_t(base) + Kirby::Mem1Size)
            return Memory::GetPointer(address, size);
    for (const uint32_t base : {0x10000000u, 0x90000000u, 0xD0000000u})
        if (address >= base && uint64_t(address) + size <= uint64_t(base) + Kirby::Mem2Size)
            return Memory::GetPointer(address, size);
    return nullptr;
}
std::deque<Kirby::IosServices::Completion> ready;
bool draining = false;
uint64_t completedCallbacks = 0;

void FreeRequest(CpuContext* caller, uint32_t request) {
    // iosFree uses the original SDK heap. Its register effects must not leak
    // into the transport caller or the guest callback's interrupted context.
    CpuContext scratch = *caller;
    CpuContextScope scope(&scratch);
    scratch.gpr[3] = Memory::Read32(KirbyBootConfig::IpcHeapId);
    scratch.gpr[4] = request;
    InvokeIndirectCpu(KirbyBootConfig::IosFree, &scratch);
    if (scratch.gpr[3] != 0) throw std::runtime_error("IOS request could not be freed by the SDK heap");
}
}

namespace Kirby {
IosServices& Ios() {
    static IosServices services(Resolve);
    return services;
}

void DrainIosCompletions(CpuContext* caller) {
    if (draining || !(caller->msr & 0x8000)) return;
    struct Guard { Guard() { draining = true; } ~Guard() { draining = false; } } guard;
    while (auto completion = Ios().TakeCompletion()) ready.push_back(*completion);
    // Bounded delivery only at explicit guest call boundaries. Device MMIO
    // cannot safely interrupt a translated function's cached register values.
    const auto count = ready.size();
    for (size_t i = 0; i < count; ++i) {
        const auto completion = ready.front();
        ready.pop_front();
        const auto request = completion.request;
        const auto callback = Memory::Read32(request + 32);
        if (!callback) throw std::runtime_error("Completed asynchronous IOS request has no callback");
        Memory::Write32(request + 4, completion.result);
        CpuContext scratch = *caller;
        scratch.msr &= ~0x8000u;
        CpuContextScope scope(&scratch);
        scratch.gpr[3] = completion.result;
        scratch.gpr[4] = Memory::Read32(request + 36);
        InvokeIndirectCpu(callback, &scratch);
        FreeRequest(caller, request);
        ++completedCallbacks;
    }
}
uint64_t CompletedIosCallbacks() { return completedCallbacks; }
}

extern "C" void KirbyIosSubmit(CpuContext* ctx) {
    const auto request = ctx->gpr[3];
    const bool asynchronous = ctx->gpr[4] != 0;
    if (!request) { ctx->gpr[3] = uint32_t(-4); return; }
    if ((request & 31) || !Resolve(request, 64))
        throw std::runtime_error("Invalid SDK IOS request allocation");
    if (Memory::Read32(request + 32) != ctx->gpr[4])
        throw std::runtime_error("SDK IOS callback disagrees with transport argument");
    Kirby::DrainIosCompletions(ctx);
    const auto result = Kirby::Ios().Submit(request, asynchronous);
    if (asynchronous) {
        if (result) ready.push_back({request, *result});
        ctx->gpr[3] = 0;
        // __ios_Ipc2 restores the caller's interrupt state after queuing work.
        // The native service is already done: deliver at this full-context
        // boundary when EE permits it, including callers that next busy-wait.
        // The SDK ioctl wrapper does not touch the allocation after this call.
        Kirby::DrainIosCompletions(ctx);
    } else {
        if (!result) throw std::runtime_error("Pending synchronous IOS operation requires a guest scheduler");
        Memory::Write32(request + 4, *result);
        FreeRequest(ctx, request);
        ctx->gpr[3] = *result;
    }
}

extern "C" void KirbyRestoreInterrupts(CpuContext* ctx) {
    const auto requested = ctx->gpr[3], old = ctx->msr;
    SetCRResident(ctx->cr, ctx->xer, 0, static_cast<int32_t>(requested), 0);
    ctx->gpr[4] = old;
    ctx->gpr[5] = requested ? old | 0x8000u : old & ~0x8000u;
    ctx->msr = ctx->gpr[5];
    ctx->gpr[3] = (old >> 15) & 1;
    // The original SDK operation's register effects above are retained. This
    // native boundary also lets completed IOS work interrupt an enabled guest.
    // Passing the whole context makes the translator flush resident registers.
    Kirby::DrainIosCompletions(ctx);
    Kirby::PollWpad(*ctx);
    // This audited SDK boundary has flushed the translated registers. Pending
    // device IRQs can run as EE is enabled, without waiting for scheduler idle.
    if ((ctx->msr & 0x8000) && Memory::Contains(0x800000D4, 4)) {
        const auto context = Memory::Read32(0x800000D4);
        // Returning with EE set and an asserted IRQ immediately re-enters
        // exception handling. Drain queued mailbox replies at this boundary.
        if (context) while (Kirby::DispatchInterrupt(*ctx, context, ctx->lr)) {}
    }
}

// Kirby addresses are already RK5E01 addresses. Never apply MKW's PAL region map.
#undef REGISTER_NATIVE_FUNCTION
#define REGISTER_NATIVE_FUNCTION(address, fn) \
    static AbiTrampoline<decltype(fn)> kirby_##fn(address, #fn, fn, FunctionKind::Native, \
        false, kPpcAllNonvolatileFprMask, 0, 0, &AbiRawCpuThunk<&fn>::Invoke)
static_assert(KirbyBootConfig::IosTransport == 0x806A8FA0u);
PPC_NATIVE_OVERRIDE_VOID(806A8FA0, KirbyIosSubmit, (CpuContext* ctx), (ctx));
static_assert(KirbyBootConfig::RestoreInterrupts == 0x806681C0u);
PPC_NATIVE_OVERRIDE_VOID(806681C0, KirbyRestoreInterrupts, (CpuContext* ctx), (ctx));
