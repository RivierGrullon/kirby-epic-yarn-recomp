// SPDX-License-Identifier: GPL-3.0-only
#include "pages.h"
#include "../platform.h"
#include <imgui.h>
#include <chrono>
#include <cstdio>

namespace Launcher {
namespace {
const char* StageLabel(const std::string& stage) {
    if (stage=="hash") return "Reading source";
    if (stage=="unpack") return "Unpacking archive";
    if (stage=="extract") return "Extracting game data";
    if (stage=="validate") return "Verifying game data";
    if (stage=="prepare") return "Preparing installation tools";
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
    ImGui::TextWrapped("Choose a WBFS, ISO, RVZ, 7z archive or an extracted DATA folder. Setup prepares and checks the native game automatically.");
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
            ImGui::TextUnformatted("Installation progress");
            ImGui::TextUnformatted(setup.status==Setup::Status::Cancelling ? "Cancelling setup..." :
                                   setup.detail.empty() ? StageLabel(setup.stage) : setup.detail.c_str());
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram,ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
            if (setup.completed && setup.total && *setup.total>0) {
                const double fraction=double(*setup.completed)/double(*setup.total);
                char label[128];
                if (setup.unit=="tasks")
                    std::snprintf(label,sizeof(label),"%.0f%% - %llu / %llu build tasks",fraction*100.,
                                  static_cast<unsigned long long>(*setup.completed),static_cast<unsigned long long>(*setup.total));
                else
                    std::snprintf(label,sizeof(label),"%.0f%% - %.1f / %.1f MiB",fraction*100.,
                                  double(*setup.completed)/1048576.,double(*setup.total)/1048576.);
                ImGui::ProgressBar(static_cast<float>(fraction),{-1,0},label);
            } else {
                ImGui::ProgressBar(-static_cast<float>(ImGui::GetTime()),{-1,0},"Working...");
            }
            ImGui::PopStyleColor();
            const auto seconds=std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now()-setup.startedAt).count();
            ImGui::Text("Elapsed: %lld min %02lld sec",static_cast<long long>(seconds/60),static_cast<long long>(seconds%60));
            ImGui::TextWrapped("The first installation can take several minutes. You can leave this window open.");
            ImGui::BeginDisabled(state.selfTest || setup.status==Setup::Status::Cancelling);
            if (ImGui::Button(setup.operation==Setup::Operation::Install ? "Cancel build" : "Cancel import")) action=Action::CancelSetup;
            ImGui::EndDisabled();
        } else {
            ImGui::TextWrapped("%s",setup.message.c_str());
            if (setup.result) ImGui::Text("Verified %llu files%s",static_cast<unsigned long long>(setup.result->assetFiles),
                                        setup.result->cached ? " (cached export)" : "");
        }
        if (!setup.source.empty()) ImGui::TextWrapped("Source: %s",Utf8(setup.source).c_str());
        if (!setup.logPath.empty() && ImGui::Button("Open setup logs")) action=Action::OpenSetupLogs;
    }
    ImGui::Spacing();
    ImGui::SeparatorText("Selected game data");
    ImGui::TextWrapped("%s", Utf8(state.config.gameData).c_str());
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
    if (std::filesystem::exists(state.root/"tools/runtime/toolchain-download.json"))
        ImGui::TextWrapped("Installation tools download automatically the first time and are reused for future updates. Your game files stay on this computer.");
    else if (std::filesystem::exists(state.root/"tools/runtime/toolchain.json"))
        ImGui::TextWrapped("Build tools are included. The first preparation needs internet access and can take several minutes. Your game files stay on this computer.");
    else {
        ImGui::TextWrapped("Developer checkout: prepare the build tools using the Build Guide. Application packages include their own tools.");
        if (ImGui::Button("Open Build Guide")) action = Action::BuildGuide;
    }
    return action;
}
} // namespace Launcher
