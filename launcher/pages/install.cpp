// SPDX-License-Identifier: GPL-3.0-only
#include "pages.h"
#include <imgui.h>

namespace Launcher {
Action DrawInstall(State& state) {
    Action action = Action::None;
    ImGui::TextWrapped("Use your own extracted DATA folder, including sys/ and files/.");
    ImGui::Spacing();
    ImGui::BeginDisabled(state.selfTest || state.gameRunning);
    if (ImGui::Button("Choose DATA folder")) action = Action::ChooseData;
    if (ImGui::Button("Verify selected folder")) action = Action::VerifyData;
    ImGui::EndDisabled();
    ImGui::TextWrapped("%s", state.config.gameData.c_str());
    ImGui::TextWrapped("%s", state.dataStatus.c_str());
    ImGui::Spacing();
    ImGui::SeparatorText("Supported copy");
    ImGui::TextUnformatted("RK5E01 Rev 0 / USA");
    ImGui::TextWrapped("Other revisions cannot use this compiled game.");
    ImGui::Spacing();
    ImGui::SeparatorText("Import and build");
    ImGui::TextWrapped("WBFS, ISO, RVZ and 7z extraction, translation and compilation currently use the build guide. Setup from this window is not available yet.");
    if (ImGui::Button("Open Build Guide")) action = Action::BuildGuide;
    return action;
}
} // namespace Launcher
