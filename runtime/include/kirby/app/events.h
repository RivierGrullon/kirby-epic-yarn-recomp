// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <aurora/event.h>

namespace Kirby {
void UpdateControllerMotion();
inline void (*hostMotionUpdate)() = nullptr;
inline void (*hostButtonPromptEvents)(const AuroraEvent*) = nullptr;
struct ExitRequested {};
inline void PollHostEvents() {
    if (hostMotionUpdate) hostMotionUpdate();
    // Throw only after Aurora's noexcept update has returned. Guest callbacks
    // and native continuations can then unwind to the application's root stack.
    const auto* events = aurora_update();
    if (hostButtonPromptEvents) hostButtonPromptEvents(events);
    for (const auto* event = events; event && event->type != AURORA_NONE; ++event)
        if (event->type == AURORA_EXIT) throw ExitRequested{};
}
}
