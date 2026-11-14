// SPDX-License-Identifier: GPL-3.0-only
#include "pages.h"
#include <imgui.h>

namespace Launcher {
namespace {
const char* StageLabel(const std::string& stage) {
    if (stage=="hash") return "Reading source";
    if (stage=="unpack") return "Unpacking archive";
    if (stage=="extract") return "Extracting game data";
    if (stage=="validate") return "Verifying game data";
    return "Inspecting source";
}
}
Action DrawInstall(State& state) {
    Action action = Action::None;
    auto& setup=state.setup;
    ImGui::TextWrapped("Import your own RK5E01 Rev 0 (USA) copy.");
    ImGui::TextWrapped("Choose a WBFS, ISO, RVZ, 7z archive or an extracted DATA folder.");
    ImGui::Spacing();
    ImGui::BeginDisabled(state.selfTest || state.gameRunning || setup.Busy() || state.choosingSource);
    if (ImGui::Button("Choose disc image")) action=Action::ChooseImage;
    ImGui::SameLine();
    if (ImGui::Button("Choose DATA folder")) action = Action::ChooseData;
    ImGui::EndDisabled();
    if (state.gameRunning) ImGui::TextWrapped("Close the game before importing another copy.");
    if (state.choosingSource) ImGui::TextUnformatted("Waiting for source selection...");
    if (setup.status!=Setup::Status::Idle) {
        ImGui::Spacing();
        if (setup.Busy()) {
            ImGui::TextUnformatted(setup.status==Setup::Status::Cancelling ? "Cancelling import..." : StageLabel(setup.stage));
            if (setup.completed && setup.total && *setup.total>0) {
                const auto label=std::to_string(*setup.completed/1048576)+" / "+std::to_string(*setup.total/1048576)+" MiB";
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram,ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
                ImGui::ProgressBar(static_cast<float>(double(*setup.completed)/double(*setup.total)),{-1,0},label.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::BeginDisabled(state.selfTest || setup.status==Setup::Status::Cancelling);
            if (ImGui::Button("Cancel import")) action=Action::CancelSetup;
            ImGui::EndDisabled();
        } else {
            ImGui::TextWrapped("%s",setup.message.c_str());
            if (setup.result) ImGui::Text("Verified %llu files%s",static_cast<unsigned long long>(setup.result->assetFiles),
                                        setup.result->cached ? " (cached export)" : "");
        }
        if (!setup.source.empty()) ImGui::TextWrapped("Source: %s",setup.source.c_str());
        if (!setup.logPath.empty() && ImGui::Button("Open import logs")) action=Action::OpenSetupLogs;
    }
    ImGui::Spacing();
    ImGui::SeparatorText("Selected game data");
    ImGui::TextWrapped("%s", state.config.gameData.c_str());
    ImGui::TextWrapped("%s", state.dataStatus.c_str());
    ImGui::BeginDisabled(state.selfTest || state.gameRunning || setup.Busy() || state.choosingSource);
    if (ImGui::Button("Verify selected folder")) action=Action::VerifyData;
    ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::SeparatorText("Native build");
    ImGui::TextWrapped("Import verifies and keeps complete game data. Translation and compilation still use the build guide.");
    if (ImGui::Button("Open Build Guide")) action = Action::BuildGuide;
    return action;
}
} // namespace Launcher
