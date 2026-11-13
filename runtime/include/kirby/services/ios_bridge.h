// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "kirby/services/ios_service.h"
struct CpuContext;
namespace Kirby {
IosServices& Ios();
void DrainIosCompletions(CpuContext* caller);
uint64_t CompletedIosCallbacks();
}
