// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <aurora/event.h>

namespace Kirby {
struct ExitRequested {};
inline void PollHostEvents() {
    // Throw only after Aurora's noexcept update has returned. Guest callbacks
    // and native continuations can then unwind to the application's root stack.
    for (const auto* event = aurora_update(); event && event->type != AURORA_NONE; ++event)
        if (event->type == AURORA_EXIT) throw ExitRequested{};
}
}
