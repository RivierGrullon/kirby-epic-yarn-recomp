// SPDX-License-Identifier: GPL-3.0-only
#pragma once

namespace Launcher {
// Call after the platform backend queues events, before ImGui::NewFrame().
// A gamepad is shared with other processes; its polling is not window-focused.
void SynchronizeInputFocus(bool focused);
} // namespace Launcher
