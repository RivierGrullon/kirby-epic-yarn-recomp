// SPDX-License-Identifier: GPL-3.0-only
#include "pages.h"
#include <imgui.h>

namespace Launcher {
Action DrawDisplay(State& state) {
    ImGui::BeginDisabled(state.selfTest || !state.settingsError.empty());
    state.settingsDirty |= ImGui::Checkbox("VSync", &state.config.vsync);
    ImGui::TextWrapped("Synchronize presentation with the display. Turning this off can reduce stalls on some drivers.");
    ImGui::Spacing();
    state.settingsDirty |= ImGui::Checkbox("Experimental performance mode", &state.config.gpuResidentCopies);
    ImGui::TextWrapped("Reduces GPU copy waits and repeated texture processing. Compatibility testing is ongoing.");
    ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::SeparatorText("Resolution and frame rate");
    ImGui::TextWrapped("Rendering uses the original game resolution. Resolution scaling and a validated 30/60 FPS selector are not available yet.");
    return Action::None;
}
} // namespace Launcher
