// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <aurora/event.h>

namespace Kirby {
void UpdateControllerMotion();
inline void (*hostMotionUpdate)() = nullptr;
struct ExitRequested {};
inline void PollHostEvents() {
    if (hostMotionUpdate) hostMotionUpdate();
    // Throw only after Aurora's noexcept update has returned. Guest callbacks
    // and native continuations can then unwind to the application's root stack.
    for (const auto* event = aurora_update(); event && event->type != AURORA_NONE; ++event)
        if (event->type == AURORA_EXIT) throw ExitRequested{};
}
}
