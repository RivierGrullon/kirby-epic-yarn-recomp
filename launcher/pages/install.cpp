// SPDX-License-Identifier: GPL-3.0-only
#include "pages.h"
#include "../platform.h"
#include <imgui.h>

namespace Launcher {
Action DrawInstall(State& state) {
    Action action = Action::None;
    if (state.portable) {
        ImGui::TextWrapped("Choose the extracted DATA folder from your own RK5E01 Rev 0 (USA) copy, then open Play.");
        ImGui::BeginDisabled(state.selfTest || state.gameRunning || state.choosingSource);
        if (ImGui::Button("Choose DATA folder")) action=Action::ChooseData;
        ImGui::SameLine();
        if (ImGui::Button("Verify selected folder")) action=Action::VerifyData;
        ImGui::EndDisabled();
        ImGui::TextWrapped("%s", Utf8(state.config.gameData).c_str());
        ImGui::TextWrapped("%s", state.dataStatus.c_str());
        if (state.setup.status==Setup::Status::Failed) ImGui::TextWrapped("%s", state.setup.message.c_str());
        ImGui::TextWrapped("This package is already compiled. To use a WBFS or rebuild, run tools/windows/build-windows.ps1 in the source checkout.");
        return action;
    }
    ImGui::TextWrapped("Kirby Setup handles installation and updates in a separate window.");
    ImGui::TextWrapped("Choose your own disc image in Kirby Setup. It downloads the tools, builds the game and shows progress. Return here to play and change settings.");
    const bool available=std::filesystem::is_regular_file(state.root/"local/setup-command.json");
    ImGui::Spacing();
    ImGui::BeginDisabled(state.selfTest || state.gameRunning || !available);
    if (ImGui::Button("Open Kirby Setup")) action=Action::OpenSetup;
    ImGui::EndDisabled();
    if (state.gameRunning) ImGui::TextWrapped("Close the game before updating the installation.");
    if (!available) ImGui::TextWrapped("Open your Kirby Setup AppImage or Windows setup application to install the game.");
    if (!state.gameStatus.empty()) ImGui::TextWrapped("%s",state.gameStatus.c_str());
    ImGui::Spacing();ImGui::SeparatorText("Installed game");
    ImGui::TextWrapped("%s",state.dataStatus.c_str());
    if (!state.installationId.empty()) ImGui::TextWrapped("Installation: %.12s",state.installationId.c_str());
    if (!state.installationError.empty()) ImGui::TextWrapped("%s",state.installationError.c_str());
    return action;
}
} // namespace Launcher
