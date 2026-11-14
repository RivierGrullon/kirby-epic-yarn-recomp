// SPDX-License-Identifier: GPL-3.0-only
#include "pages.h"
#include <imgui.h>

namespace Launcher {
Action DrawDiagnostics(State& state) {
    Action action = Action::None;
    ImGui::TextWrapped("%s", state.gameRunning ? "Game is running" : "Game is not running");
    ImGui::TextWrapped("%s", state.dataStatus.c_str());
    if (!state.gameStatus.empty()) ImGui::TextWrapped("%s", state.gameStatus.c_str());
    ImGui::Spacing();
    if (ImGui::Button("Open Logs")) action = Action::OpenLogs;
    if (ImGui::Button("Open Build Guide")) action = Action::BuildGuide;
    ImGui::Spacing();
    ImGui::SeparatorText("Shared settings");
    ImGui::TextWrapped("%s", state.settingsFile.c_str());
    if (ImGui::Button("Open Settings Folder")) action = Action::OpenSettings;
    ImGui::BeginDisabled(state.selfTest);
    if (ImGui::Button(state.settingsDirty ? "Discard changes and reload" : "Reload settings"))
        action = Action::ReloadSettings;
    ImGui::EndDisabled();
    if (!state.settingsError.empty()) {
        ImGui::TextWrapped("Settings error: %s", state.settingsError.c_str());
        ImGui::TextWrapped("The original file was preserved. Correct it and reload, or move it aside before restarting.");
    }
    ImGui::Spacing();
    ImGui::SeparatorText("Native executable");
    ImGui::TextWrapped("%s", state.executable.c_str());
    ImGui::TextWrapped("WiiCompiled + Aurora. GPL-3.0-only and third-party licenses preserved.");
    return action;
}
} // namespace Launcher
