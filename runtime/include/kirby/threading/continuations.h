// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
struct CpuContext;
namespace Kirby {
void PrepareThreadSwitch(uint32_t oldThread, uint32_t selected, CpuContext& cpu);
void SwitchThreadStack(uint32_t oldThread, uint32_t selected, CpuContext& cpu);
void ShutdownThreadStacks();
uint64_t ThreadStackSwitches();
void ProbeThreads(CpuContext& cpu, bool fault = false);
}
