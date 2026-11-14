// SPDX-License-Identifier: GPL-3.0-only
#include "hle_stubs.h"
#include "generated/KirbyBootConfig.h"
#include "kirby/graphics/pos_matrix.h"

extern "C" void KirbyGXLoadPosMtxImm(CpuContext* ctx) { Kirby::LoadPositionMatrix(ctx); }

// This implementation preserves every nonvolatile FPR. The effect analyzer
// intentionally keeps the raw CPU boundary conservative for GQR/fallback access.
#undef REGISTER_NATIVE_FUNCTION
#define REGISTER_NATIVE_FUNCTION(address, fn) \
    static AbiTrampoline<decltype(fn)> kirby_##fn(address, #fn, fn, FunctionKind::Native, \
        true, 0, 0, 0, &AbiRawCpuThunk<&fn>::Invoke)
static_assert(KirbyBootConfig::GXLoadPosMtxImm==0x8068BD70u);
PPC_NATIVE_OVERRIDE_VOID(8068BD70, KirbyGXLoadPosMtxImm, (CpuContext* ctx), (ctx));
