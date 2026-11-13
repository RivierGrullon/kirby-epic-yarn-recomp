// SPDX-License-Identifier: GPL-3.0-only
#pragma once
struct CpuContext;
namespace Kirby {
void ConfigureAx();
void ShutdownAx();
void ProbeAx(CpuContext& cpu);
}
