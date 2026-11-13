// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
struct CpuContext;
namespace Kirby {
bool AcceptWpadRegistration(uint32_t address, const char* name) noexcept;
void VerifyWpadRegistrations();
void PollWpad(CpuContext& cpu, bool force = false);
void SetWpadWindowActive(bool active);
void ConfigureControllerDefaults(); // Must precede Aurora's PADInit.
void ProbeWpad(CpuContext& cpu);
void ProbeSdlController(CpuContext& cpu, bool monitorPhysical = false);
}
