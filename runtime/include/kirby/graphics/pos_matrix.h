// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>

struct CpuContext;
namespace Kirby {
struct PosMatrixStats { uint64_t fast = 0, fallback = 0; };
PosMatrixStats PositionMatrixStats();
void LoadPositionMatrix(CpuContext* ctx);
void ProbePositionMatrix(CpuContext& cpu);
void BenchmarkPositionMatrix(CpuContext& cpu);
}
