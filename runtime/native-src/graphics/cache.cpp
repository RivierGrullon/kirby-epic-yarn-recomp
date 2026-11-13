// SPDX-License-Identifier: GPL-3.0-only
#include "hle_stubs.h"
#include "generated/KirbyBootConfig.h"
#include "kirby/graphics/cache.h"

extern "C" void KirbyDCInvalidateRange(CpuContext* ctx) { Kirby::MaintainDataCache(ctx,false,Kirby::CacheOperation::Invalidate); }
extern "C" void KirbyDCFlushRange(CpuContext* ctx) { Kirby::MaintainDataCache(ctx,true,Kirby::CacheOperation::Writeback); }
extern "C" void KirbyDCStoreRange(CpuContext* ctx) { Kirby::MaintainDataCache(ctx,true,Kirby::CacheOperation::Writeback); }
extern "C" void KirbyDCFlushRangeNoSync(CpuContext* ctx) { Kirby::MaintainDataCache(ctx,false,Kirby::CacheOperation::Writeback); }
extern "C" void KirbyDCStoreRangeNoSync(CpuContext* ctx) { Kirby::MaintainDataCache(ctx,false,Kirby::CacheOperation::Writeback); }

// These are audited Kirby addresses; never apply upstream region mapping.
#undef REGISTER_NATIVE_FUNCTION
#define REGISTER_NATIVE_FUNCTION(address, fn) \
    static AbiTrampoline<decltype(fn)> kirby_##fn(address, #fn, fn, FunctionKind::Native, \
        false, kPpcAllNonvolatileFprMask, 0, 0, &AbiRawCpuThunk<&fn>::Invoke)
static_assert(KirbyBootConfig::DCInvalidateRange == 0x80663A80u);
static_assert(KirbyBootConfig::DCFlushRange == 0x80663AB0u);
static_assert(KirbyBootConfig::DCStoreRange == 0x80663AE0u);
static_assert(KirbyBootConfig::DCFlushRangeNoSync == 0x80663B10u);
static_assert(KirbyBootConfig::DCStoreRangeNoSync == 0x80663B40u);
PPC_NATIVE_OVERRIDE_VOID(80663A80, KirbyDCInvalidateRange, (CpuContext* ctx), (ctx));
PPC_NATIVE_OVERRIDE_VOID(80663AB0, KirbyDCFlushRange, (CpuContext* ctx), (ctx));
PPC_NATIVE_OVERRIDE_VOID(80663AE0, KirbyDCStoreRange, (CpuContext* ctx), (ctx));
PPC_NATIVE_OVERRIDE_VOID(80663B10, KirbyDCFlushRangeNoSync, (CpuContext* ctx), (ctx));
PPC_NATIVE_OVERRIDE_VOID(80663B40, KirbyDCStoreRangeNoSync, (CpuContext* ctx), (ctx));
