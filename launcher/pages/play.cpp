// SPDX-License-Identifier: GPL-3.0-only
#include "pages.h"
#include <imgui.h>

namespace Launcher {
Action DrawPlay(State& state) {
    Action action = Action::None;
    ImGui::TextWrapped("%s", state.dataStatus.c_str());
    if (!state.gameStatus.empty()) ImGui::TextWrapped("%s", state.gameStatus.c_str());
    std::error_code ec;
    const bool installed = std::filesystem::is_regular_file(state.executable, ec);
    ImGui::BeginDisabled(!state.dataVerified || !installed || state.gameRunning ||
                         state.selfTest || !state.settingsError.empty());
    if (ImGui::Button(state.gameRunning ? "Game running" : "Play", {200, 48})) action = Action::Play;
    ImGui::EndDisabled();
    if (!state.dataVerified || !installed) {
        ImGui::TextWrapped("%s", installed ? "Choose the extracted DATA folder from your own copy."
                                          : "The native executable is missing. See Install for the build guide.");
        if (ImGui::Button("Open Install")) action = Action::ShowInstall;
    }
    ImGui::Spacing();
    ImGui::SeparatorText("Game data");
    ImGui::TextWrapped("%s", state.config.gameData.c_str());
    ImGui::SeparatorText("Next launch");
    ImGui::Text("Master volume: %.0f%%", state.config.masterVolume);
    ImGui::Text("VSync: %s", state.config.vsync ? "On" : "Off");
    ImGui::Text("Experimental performance mode: %s", state.config.gpuResidentCopies ? "On" : "Off");
    ImGui::Spacing();
    ImGui::TextWrapped("Use Display, Audio and Controls to review the current settings.");
    return action;
}
} // namespace Launcher
