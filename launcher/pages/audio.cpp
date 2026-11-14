// SPDX-License-Identifier: GPL-3.0-only
#include "pages.h"
#include <imgui.h>

namespace Launcher {
Action DrawAudio(State& state) {
    ImGui::TextUnformatted("Master volume");
    ImGui::BeginDisabled(state.selfTest || !state.settingsError.empty());
    ImGui::SetNextItemWidth(-1);
    state.settingsDirty |= ImGui::SliderFloat("##Master volume", &state.config.masterVolume, 0, 100, "%.0f%%");
    ImGui::EndDisabled();
    ImGui::TextWrapped("Adjusts the complete game mix. Set to 0%% to mute it.");
    ImGui::Spacing();
    ImGui::TextWrapped("Separate music, effects and voice controls are not available yet.");
    return Action::None;
}
} // namespace Launcher
