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
    if (stage=="prepare") return "Preparing pinned build tools";
    if (stage=="translate") return "Translating PowerPC code to C++";
    if (stage=="compile") return "Compiling the native game";
    if (stage=="verify") return "Checking native startup (silent)";
    if (stage=="publish") return "Saving the verified native build";
    if (stage=="select") return "Selecting the verified installation";
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
            ImGui::TextUnformatted(setup.status==Setup::Status::Cancelling ? "Cancelling setup..." : StageLabel(setup.stage));
            if (setup.completed && setup.total && *setup.total>0) {
                const auto label=std::to_string(*setup.completed/1048576)+" / "+std::to_string(*setup.total/1048576)+" MiB";
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram,ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
                ImGui::ProgressBar(static_cast<float>(double(*setup.completed)/double(*setup.total)),{-1,0},label.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::BeginDisabled(state.selfTest || setup.status==Setup::Status::Cancelling);
            if (ImGui::Button(setup.operation==Setup::Operation::Install ? "Cancel build" : "Cancel import")) action=Action::CancelSetup;
            ImGui::EndDisabled();
        } else {
            ImGui::TextWrapped("%s",setup.message.c_str());
            if (setup.result) ImGui::Text("Verified %llu files%s",static_cast<unsigned long long>(setup.result->assetFiles),
                                        setup.result->cached ? " (cached export)" : "");
        }
        if (!setup.source.empty()) ImGui::TextWrapped("Source: %s",setup.source.c_str());
        if (!setup.logPath.empty() && ImGui::Button("Open setup logs")) action=Action::OpenSetupLogs;
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
    ImGui::TextWrapped("Build from the selected DATA folder. A matching build is reused; damaged compiler outputs are rebuilt. Startup checks run silently before selection.");
    if (!state.installationId.empty()) ImGui::TextWrapped("Selected installation: %.12s",state.installationId.c_str());
    if (!state.installationError.empty()) ImGui::TextWrapped("%s",state.installationError.c_str());
    ImGui::BeginDisabled(state.selfTest || state.gameRunning || setup.Busy() || state.choosingSource || !state.dataVerified);
    if (ImGui::Button("Build / update native game")) action=Action::BuildGame;
    ImGui::EndDisabled();
    ImGui::TextWrapped("The pinned local toolchain image must already be prepared. First builds can take several minutes; details are in the build guide.");
    if (ImGui::Button("Open Build Guide")) action = Action::BuildGuide;
    return action;
}
} // namespace Launcher
