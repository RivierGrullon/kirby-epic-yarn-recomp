// SPDX-License-Identifier: GPL-3.0-only
#include "pages.h"
#include <imgui.h>

namespace Launcher {
namespace {
void DrawBuild(const char* label,const Kirby::BuildInfo& info) {
    ImGui::PushID(label);
    ImGui::Text("%s: %.12s%s",label,info.sourceRevision.c_str(),info.sourceModified ? " (modified)" : "");
    ImGui::Text("Source fingerprint: %.12s",info.sourceSha256.c_str());
    if (ImGui::TreeNode("Source and dependency details")) {
        ImGui::TextWrapped("%s / %s",info.compiler.c_str(),info.buildType.c_str());
        ImGui::TextWrapped("Project revision: %s",info.sourceRevision.c_str());
        ImGui::TextWrapped("Project source SHA-256: %s",info.sourceSha256.c_str());
        ImGui::TextWrapped("Pinned WiiCompiled: %s",info.translatorRevision.c_str());
        ImGui::TextWrapped("Pinned Aurora: %s",info.auroraRevision.c_str());
        ImGui::TextWrapped("Game: %s Rev %d",info.gameId.c_str(),info.gameRevision);
        ImGui::TextWrapped("DOL SHA-1: %s",info.dolSha1.c_str());
        ImGui::TextWrapped("CMake compiler options: %s",info.compilerOptions.c_str());
        ImGui::TreePop();
    }
    ImGui::PopID();
}
}
Action DrawDiagnostics(State& state) {
    Action action = Action::None;
    ImGui::TextWrapped("%s", state.gameRunning ? "Game is running" : "Game is not running");
    ImGui::TextWrapped("%s", state.dataStatus.c_str());
    if (!state.gameStatus.empty()) ImGui::TextWrapped("%s", state.gameStatus.c_str());
    ImGui::Spacing();
    ImGui::SeparatorText("Build information");
    DrawBuild("Launcher",state.launcherBuild);
    ImGui::Spacing();
    if (state.runtimeBuild) {
        DrawBuild("Native game",*state.runtimeBuild);
        ImGui::TextWrapped("%s",Kirby::SameBuildInputs(state.launcherBuild,*state.runtimeBuild)
            ? "Launcher and runtime build records match." : "Launcher and runtime build records differ.");
    }
    if (!state.buildStatus.empty()) ImGui::TextWrapped("%s",state.buildStatus.c_str());
    ImGui::BeginDisabled(state.selfTest || state.checkingBuild || state.gameRunning);
    if (ImGui::Button(state.checkingBuild ? "Checking native build..." : "Refresh build information"))
        action=Action::RefreshBuildInfo;
    ImGui::EndDisabled();
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
    ImGui::Spacing();
    ImGui::TextWrapped("WiiCompiled + Aurora. GPL-3.0-only and third-party licenses preserved.");
    return action;
}
} // namespace Launcher
