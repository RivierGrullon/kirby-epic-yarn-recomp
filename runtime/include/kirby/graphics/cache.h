// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <functional>

struct CpuContext;
namespace Kirby {
// The range covers whole 32-byte PPC cache lines, in guest address space.
// Its 64-bit size can cross the 32-bit address boundary; consumers must handle
// that wrap conservatively. A zero-length SDK request does not notify observers.
enum class CacheOperation { Invalidate, Writeback };
using CacheObserver = std::function<void(uint32_t, uint64_t, CacheOperation)>;
void ObserveDataCache(CacheObserver observer);
void MaintainDataCache(CpuContext* ctx, bool systemCall, CacheOperation operation);
}
