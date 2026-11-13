// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/graphics/cache.h"
#include "guest_abi/cpu_context.h"
#include "isa/ppc_isa_cr.h"
#include <utility>

extern "C" uint32_t OSSystemCall();

namespace Kirby {
namespace { CacheObserver observer; }
void ObserveDataCache(CacheObserver next) { observer=std::move(next); }
void MaintainDataCache(CpuContext* ctx, bool systemCall) {
    const uint32_t address=ctx->gpr[3], length=ctx->gpr[4];
    SetCRResident(ctx->cr,ctx->xer,0,length,0u);
    if (!length) return;
    const uint32_t offset=address & 31;
    const uint32_t lines=(length+offset+31) >> 5;
    // PPC arithmetic wraps before shifting. A zero CTR still executes the
    // do/while loop 2^32 times; report its full span without iterating on host.
    if (observer) observer(address & ~31u,(lines ? uint64_t(lines) : uint64_t(1)<<32)*32);
    ctx->gpr[3]=address+lines*32;
    ctx->gpr[4]=lines;
    ctx->gpr[5]=offset;
    ctx->ctr=0;
    if (systemCall) OSSystemCall();
}
}
