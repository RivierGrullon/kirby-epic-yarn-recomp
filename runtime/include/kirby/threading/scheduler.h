// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
struct CpuContext;
namespace Kirby {
// IRQ frames live on their owning native stack, including while preempted.
struct InterruptState {
    uint32_t context = 0;
    const CpuContext* savedCpu = nullptr;
};
InterruptState GetInterruptState();
void SetInterruptState(InterruptState state);
bool DispatchInterrupt(CpuContext& cpu, uint32_t guestContext, uint32_t resume);
bool DispatchAlarm(CpuContext& cpu, uint32_t guestContext, uint32_t resume);
uint64_t DeliveredAlarms();
void ProbeAlarms(CpuContext& cpu);
uint64_t DeliveredInterrupts();
uint64_t SchedulerWakeups();
}
