// SPDX-License-Identifier: GPL-3.0-only
#include "pages.h"
#include "../platform.h"
#include <imgui.h>

namespace Launcher {
namespace {
void DrawBuild(const char* label,const Kirby::BuildInfo& info) {
    ImGui::PushID(label);
    ImGui::Text("%s: %.12s%s",label,info.sourceRevision.c_str(),info.sourceModified ? Tr(" (modified)") : "");
    ImGui::Text(Tr("Source fingerprint: %.12s"),info.sourceSha256.c_str());
    if (ImGui::TreeNode(Tr("Source and dependency details"))) {
        ImGui::TextWrapped("%s / %s",info.compiler.c_str(),info.buildType.c_str());
        ImGui::TextWrapped(Tr("Project revision: %s"),info.sourceRevision.c_str());
        ImGui::TextWrapped(Tr("Project source SHA-256: %s"),info.sourceSha256.c_str());
        ImGui::TextWrapped(Tr("Pinned WiiCompiled: %s"),info.translatorRevision.c_str());
        ImGui::TextWrapped(Tr("Pinned Aurora: %s"),info.auroraRevision.c_str());
        ImGui::TextWrapped(Tr("Game: %s Rev %d"),info.gameId.c_str(),info.gameRevision);
        ImGui::TextWrapped("DOL SHA-1: %s",info.dolSha1.c_str());
        ImGui::TextWrapped(Tr("CMake compiler options: %s"),info.compilerOptions.c_str());
        ImGui::TreePop();
    }
    ImGui::PopID();
}
}
Action DrawDiagnostics(State& state) {
    Action action = Action::None;
    ImGui::TextWrapped("%s", state.gameRunning ? Tr("Game is running") : Tr("Game is not running"));
    ImGui::TextWrapped("%s", StatusText(state.dataStatus).c_str());
    if (!state.gameStatus.empty()) ImGui::TextWrapped("%s", StatusText(state.gameStatus).c_str());
    ImGui::Spacing();
    ImGui::SeparatorText(Tr("Build information"));
    DrawBuild(Tr("Launcher"),state.launcherBuild);
    ImGui::Spacing();
    if (state.runtimeBuild) {
        DrawBuild(Tr("Native game"),*state.runtimeBuild);
        ImGui::TextWrapped("%s",Kirby::SameBuildInputs(state.launcherBuild,*state.runtimeBuild)
            ? Tr("Launcher and runtime build records match.") : Tr("Launcher and runtime build records differ."));
    }
    if (!state.buildStatus.empty()) ImGui::TextWrapped("%s",StatusText(state.buildStatus).c_str());
    ImGui::BeginDisabled(state.selfTest || state.checkingBuild || state.gameRunning);
    if (ImGui::Button(state.checkingBuild ? Tr("Checking native build...") : Tr("Refresh build information")))
        action=Action::RefreshBuildInfo;
    ImGui::EndDisabled();
    ImGui::Spacing();
    if (ImGui::Button(Tr("Open Logs"))) action = Action::OpenLogs;
    if (ImGui::Button(Tr("Open Build Guide"))) action = Action::BuildGuide;
    ImGui::Spacing();
    ImGui::SeparatorText(Tr("Shared settings"));
    ImGui::TextWrapped("%s", Utf8(state.settingsFile).c_str());
    if (ImGui::Button(Tr("Open Settings Folder"))) action = Action::OpenSettings;
    ImGui::BeginDisabled(state.selfTest);
    if (ImGui::Button(state.settingsDirty ? Tr("Discard changes and reload") : Tr("Reload settings")))
        action = Action::ReloadSettings;
    ImGui::EndDisabled();
    if (!state.settingsError.empty()) {
        ImGui::TextWrapped(Tr("Settings error: %s"), state.settingsError.c_str());
        ImGui::TextWrapped("%s", Tr("The original file was preserved. Correct it and reload, or move it aside before restarting."));
    }
    ImGui::Spacing();
    ImGui::SeparatorText(Tr("Native executable"));
    ImGui::TextWrapped("%s", Utf8(state.executable).c_str());
    ImGui::Spacing();
    ImGui::TextWrapped("%s", Tr("WiiCompiled + Aurora. GPL-3.0-only and third-party licenses preserved."));
    return action;
}
} // namespace Launcher
