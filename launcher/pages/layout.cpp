// SPDX-License-Identifier: GPL-3.0-only
#include "pages.h"
#include <imgui.h>

namespace Launcher {
namespace {
Action DrawPage(State& state, Page page) {
    switch (page) {
    case Page::Play: return DrawPlay(state);
    case Page::Install: return DrawInstall(state);
    case Page::Display: return DrawDisplay(state);
    case Page::Audio: return DrawAudio(state);
    case Page::Controls: return DrawControls(state);
    case Page::Saves: return DrawSaves(state);
    case Page::Diagnostics: return DrawDiagnostics(state);
    case Page::Count: break;
    }
    return Action::None;
}
}

Action DrawLauncher(State& state, Page& selected) {
    auto& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Launcher", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                      ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextColored({1, 0.66f, 0.80f, 1}, "Kirby's Epic Yarn");
    ImGui::SameLine(); ImGui::TextDisabled("Native / Linux");
    ImGui::Separator();
    ImGui::BeginDisabled(state.bindingCapture.Busy());
    if (io.DisplaySize.x < 760 && ImGui::BeginTabBar("Pages", ImGuiTabBarFlags_FittingPolicyScroll)) {
        // Let ImGui handle user tab selection. Only force a tab when a page was
        // requested elsewhere (for example, the settings-error action).
        static Page lastCompactPage = Page::Count;
        const auto requested = selected;
        const bool synchronize = requested != lastCompactPage;
        for (size_t i = 0; i < PageNames.size(); ++i) {
            const auto page = static_cast<Page>(i);
            const auto flags = synchronize && requested == page ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            if (ImGui::BeginTabItem(PageNames[i], nullptr, flags)) {
                selected = page;
                ImGui::EndTabItem();
            }
        }
        lastCompactPage = selected;
        ImGui::EndTabBar();
    }
    ImGui::EndDisabled();
    const float footerHeight = state.settingsError.empty() ? 88.f : 128.f;
    ImGui::BeginChild("Body", {0, -footerHeight});
    if (io.DisplaySize.x >= 760) {
        ImGui::BeginChild("Navigation", {164, 0});
        ImGui::BeginDisabled(state.bindingCapture.Busy());
        for (size_t i = 0; i < PageNames.size(); ++i) {
            const auto page = static_cast<Page>(i);
            if (ImGui::Selectable(PageNames[i], selected == page, 0, {0, 40})) selected = page;
            if (selected == page) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndDisabled();
        ImGui::EndChild(); ImGui::SameLine();
    }
    ImGui::BeginChild("Content");
    ImGui::TextUnformatted(PageNames[static_cast<size_t>(selected)]);
    ImGui::Spacing();
    Action action = DrawPage(state, selected);
    ImGui::EndChild(); ImGui::EndChild();

    ImGui::Separator();
    if (!state.settingsError.empty()) {
        ImGui::TextWrapped("Settings need attention. Open Diagnostics to fix them.");
        if (ImGui::SmallButton("Show Diagnostics")) selected = Page::Diagnostics;
    }
    ImGui::BeginDisabled(state.selfTest || !state.settingsError.empty() || state.bindingCapture.Busy());
    if (ImGui::Button("Save settings")) action = Action::SaveSettings;
    ImGui::EndDisabled(); ImGui::SameLine();
    ImGui::TextDisabled("%s", state.settingsDirty ? "Unsaved changes" : "No unsaved changes");
    ImGui::TextDisabled("Changes apply on the next game launch. Development build.");
    ImGui::End();
    return action;
}
} // namespace Launcher
