// SPDX-License-Identifier: GPL-3.0-only
#include "input_focus.h"
#include <imgui.h>

namespace Launcher {
void SynchronizeInputFocus(bool focused) {
    auto& io = ImGui::GetIO();
    if (!focused) {
        // The SDL backend can poll gamepads even if this window never received
        // focus. Discard its queued input as well as any held state from the
        // previous frame, including an interrupted mouse press.
        io.ClearEventsQueue();
        io.ClearInputKeys();
        io.ClearInputMouse();
    }
    io.AddFocusEvent(focused);
}
} // namespace Launcher
