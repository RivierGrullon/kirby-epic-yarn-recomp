// SPDX-License-Identifier: GPL-3.0-only
// Preserve the original task upload; deliver its IRQ at a complete CPU boundary.
#include "hle_stubs.h"
#include "generated/KirbyBootConfig.h"
#include "kirby/threading/scheduler.h"
#include "kirby/devices/interrupts.h"
#include <stdexcept>

// Explicitly compiled original body, excluded from normal base dispatch by this
// override. Calling its symbol avoids recursively dispatching this wrapper.
extern "C" void func_80684770(CpuContext* ctx);
extern "C" void KirbyDspBootTask(CpuContext* ctx) {
    const auto resume = ctx->lr;
    func_80684770(ctx);
    // The caller busy-waits for the original DSP handler's init callback.
    // MMIO helpers cannot deliver it: translated registers may still be local.
    for (unsigned i = 0; i < 16 && Kirby::Interrupts().Dsp().Interrupt(); ++i) {
        if (!Kirby::DispatchInterrupt(*ctx, Memory::Read32(0x800000D4), resume)) break;
    }
}

#undef REGISTER_NATIVE_FUNCTION
#define REGISTER_NATIVE_FUNCTION(address, fn) \
    static AbiTrampoline<decltype(fn)> kirby_##fn(address, #fn, fn, FunctionKind::Native, \
        false, kPpcAllNonvolatileFprMask, 0, 0, &AbiRawCpuThunk<&fn>::Invoke)
static_assert(KirbyBootConfig::DspBootTask == 0x80684770);
PPC_NATIVE_OVERRIDE_VOID(80684770, KirbyDspBootTask, (CpuContext* ctx), (ctx));
