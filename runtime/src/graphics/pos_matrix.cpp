// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/graphics/pos_matrix.h"
#include "guest_abi/guest_abi.h"
#include "kirby/graphics/gx_fifo.h"
#include <array>

extern "C" void func_8068BD70(CpuContext*);
namespace Kirby {
namespace { PosMatrixStats stats; }
PosMatrixStats PositionMatrixStats() { return stats; }
void LoadPositionMatrix(CpuContext* ctx) {
    const auto address=ctx->gpr[3], index=ctx->gpr[4];
    uint8_t* source=nullptr;
    if (ctx->gqr[0]!=0 || !MemoryInline::TryGetPointerFast(address,48,source)) {
        ++stats.fallback;
        func_8068BD70(ctx);
        return;
    }
    const uint32_t command=(index<<2)|0x000B0000;
    const std::array<uint8_t,5> header{0x10,uint8_t(command>>24),uint8_t(command>>16),
                                     uint8_t(command>>8),uint8_t(command)};
    // The header may complete an earlier command whose writeback modifies the
    // source matrix. Preserve its position before all six paired loads.
    GraphicsFifo().Gather(header);
    std::array<double,6> pairs;
    std::array<uint8_t,48> payload;
    for (unsigned i=0;i<pairs.size();++i) {
        // Reuse the pinned ISA helpers: loads quiet signaling NaNs; stores also
        // flush subnormals to signed zero. A raw copy would change those cases.
        pairs[i]=PpcLoadPairPsqFloatFromHostInline(source+i*8);
        PpcStorePairPsqFloatToHostInline(payload.data()+i*8,pairs[i]);
    }
    GraphicsFifo().Gather(payload);
    // Match the original translated body's commit boundary and volatile state.
    ctx->gpr[0]=command; ctx->gpr[4]=0xCC008000; ctx->gpr[5]=0xCC010000;
    for (unsigned i=0;i<pairs.size();++i) ctx->fpr[5-i].d=pairs[i];
    ++stats.fast;
}
}
