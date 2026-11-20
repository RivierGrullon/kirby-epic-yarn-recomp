// SPDX-License-Identifier: GPL-3.0-only
#include "pages.h"
#include <imgui.h>

namespace Launcher {
namespace {
Action DrawPage(State& state, Page& page) {
    switch (page) {
    case Page::Play: return DrawPlay(state, page);
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

void LanguagePicker(State& state) {
    const auto current = CurrentLanguage();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(Tr("Language"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(128);
    if (ImGui::BeginCombo("##language", LanguageNames[static_cast<size_t>(current)])) {
        for (size_t i = 0; i < LanguageNames.size(); ++i) {
            const auto language = static_cast<Language>(i);
            if (ImGui::Selectable(LanguageNames[i], language == current)) {
                SetLanguage(language);
                state.languageSaveError = !state.selfTest && !WriteLanguage(state.settingsFile.parent_path(), language);
            }
            if (language == current) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

void Navigation(Page& selected) {
    const float width = ImGui::GetContentRegionAvail().x;
    if (width < 900) {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##pages", Tr(PageNames[static_cast<size_t>(selected)]))) {
            for (size_t i = 0; i < PageNames.size(); ++i) {
                const auto page = static_cast<Page>(i);
                if (ImGui::Selectable(Tr(PageNames[i]), page == selected)) selected = page;
                if (page == selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        return;
    }
    const float tabWidth = (width - ImGui::GetStyle().ItemSpacing.x*(PageNames.size()-1)) / PageNames.size();
    for (size_t i = 0; i < PageNames.size(); ++i) {
        if (i) ImGui::SameLine();
        const auto page = static_cast<Page>(i);
        const bool active = selected == page;
        ImGui::PushStyleColor(ImGuiCol_Button, active ? IM_COL32(244,210,218,255) : IM_COL32(237,237,220,255));
        if (ImGui::Button(Tr(PageNames[i]), {tabWidth, 35})) selected = page;
        ImGui::PopStyleColor();
    }
}
}

Action DrawLauncher(State& state, Page& selected) {
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Launcher", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                      ImGuiWindowFlags_NoSavedSettings);
    const auto top = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    auto* draw = ImGui::GetWindowDrawList();
    const auto viewport = ImGui::GetIO().DisplaySize;
    for (float x = 0; x < viewport.x; x += 8)
        draw->AddLine({x,0}, {x,viewport.y}, IM_COL32(143,116,82,9));
    for (float y = 0; y < viewport.y; y += 8)
        draw->AddLine({0,y}, {viewport.x,y}, IM_COL32(143,116,82,9));
    SewButton(draw, {top.x+14,top.y+18}, 13, IM_COL32(229,144,163,255));
    auto* brand = state.fabric.emphasis ? state.fabric.emphasis : ImGui::GetFont();
    draw->AddText(brand, brand->FontSize, {top.x+36,top.y+3}, IM_COL32(46,65,53,255), "Kirby's Epic Yarn");
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + width - 149 - ImGui::CalcTextSize(Tr("Language")).x);
    ImGui::BeginDisabled(state.bindingCapture.Busy());
    LanguagePicker(state);
    Navigation(selected);
    ImGui::EndDisabled();
    if (state.languageSaveError) ImGui::TextUnformatted(Tr("Could not save the language preference."));

    const float footer = ImGui::GetFrameHeightWithSpacing() + (state.settingsError.empty() ? 6 : 50);
    const auto page = selected;
    ImGui::PushID(static_cast<int>(page)); // Separate scroll positions for each page.
    ImGui::BeginChild("Page", {0,-footer}, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
    if (page != Page::Play) ImGui::SeparatorText(Tr(PageNames[static_cast<size_t>(page)]));
    Action action = DrawPage(state, selected);
    ImGui::EndChild();
    ImGui::PopID();

    if (!state.settingsError.empty()) {
        if (ImGui::SmallButton(Tr("Show Diagnostics"))) selected = Page::Diagnostics;
        ImGui::SameLine();
        ImGui::TextUnformatted(Tr("Settings need attention. Open Diagnostics to fix them."));
    }
    ImGui::BeginDisabled(state.selfTest || !state.settingsError.empty() || state.bindingCapture.Busy());
    if (ImGui::Button(Tr("Save settings"))) action = Action::SaveSettings;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", Tr(state.settingsDirty ? "Unsaved changes" : "No unsaved changes"));
    ImGui::End();
    return action;
}
} // namespace Launcher
