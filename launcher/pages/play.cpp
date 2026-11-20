// SPDX-License-Identifier: GPL-3.0-only
#include "pages.h"
#include "../platform.h"
#include <algorithm>
#include <imgui.h>

namespace Launcher {
namespace {
void LibraryCard(Page destination, const char* title, const char* description, ImU32 color,
                 float width, Page& selected) {
    const auto pos = ImGui::GetCursorScreenPos();
    auto* d = ImGui::GetWindowDrawList();
    ImGui::PushID(static_cast<int>(destination));
    const bool compact = width < 236;
    const float height = compact ? 55.f : 74.f;
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(243,240,224,255));
    if (ImGui::Button("##card", {width,height})) selected = destination;
    ImGui::PopStyleColor();
    const float badge = compact ? 8.f : 15.f;
    SewButton(d, {pos.x+badge+11,pos.y+height/2}, badge, color);
    const float textX = pos.x+badge*2+20;
    d->PushClipRect(pos, {pos.x+width-4,pos.y+height}, true);
    d->AddText({textX,pos.y+(compact ? 14.f : 10.f)}, IM_COL32(48,65,54,255), Tr(title));
    if (!compact) d->AddText({textX,pos.y+39}, IM_COL32(84,102,86,255), Tr(description));
    d->PopClipRect();
    ImGui::PopID();
}
}

Action DrawPlay(State& state, Page& selected) {
    Action action = Action::None;
    const auto pos = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = std::clamp(ImGui::GetWindowHeight()*.62f, 234.f, 330.f);
    Quilt(state.fabric, pos, {width,height});
    ImGui::Dummy({width,height+3});
    const ImVec2 resume = ImGui::GetCursorScreenPos();
    const float inset = width < 740 ? 21.f : 33.f;
    const float x = pos.x+inset;
    const float y = pos.y + (height-210.f)/2;
    auto* d = ImGui::GetWindowDrawList();
    const bool chinese = CurrentLanguage() == Language::Chinese;
    d->AddText({x,y+3}, IM_COL32(68,99,80,255), chinese ? "Kirby's Epic Yarn" : "Kirby's");
    auto* heading = state.fabric.heading ? state.fabric.heading : ImGui::GetFont();
    d->AddText(heading, width < 700 ? 43.f : 56.f, {x,y+27}, IM_COL32(42,71,55,255),
               chinese ? Tr("Kirby's Epic Yarn") : "Epic Yarn");
    d->AddText({x,y+102}, IM_COL32(60,88,67,255), "RK5E01   /   USA   /   Rev. 0");
    std::error_code ec;
    const bool installed = std::filesystem::is_regular_file(state.executable, ec);
    const bool ready = state.dataVerified && installed && state.installationError.empty();
    d->AddCircleFilled({x+4,y+140}, 4, ready ? IM_COL32(79,134,103,255) : IM_COL32(180,119,78,255));
    d->AddText({x+15,y+127}, IM_COL32(47,78,57,255), Tr(ready ? "Game data verified" : "Game data needs attention"));
    ImGui::SetCursorScreenPos({x,y+162});
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(166,72,102,255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(146,55,84,255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(128,47,73,255));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255,249,236,255));
    ImGui::BeginDisabled(state.selfTest || state.gameRunning || state.setup.Busy() || state.choosingSource ||
                         (ready && !state.settingsError.empty()));
    const char* label = Tr(state.gameRunning ? "Game running" : ready ? "Start game" : "Open Install");
    ImGui::PushFont(state.fabric.emphasis);
    if (ImGui::Button(label, {std::min(width*.39f,205.f),48})) action = ready ? Action::Play : Action::ShowInstall;
    ImGui::PopFont();
    ImGui::EndDisabled();
    const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    for (float lineY : {a.y+5,b.y-5}) Stitch(d,{a.x+11,lineY},{b.x-11,lineY},IM_COL32(244,175,195,200));
    ImGui::PopStyleColor(4);
    ImGui::SetCursorScreenPos(resume);

    if (!state.gameStatus.empty()) ImGui::TextWrapped("%s", StatusText(state.gameStatus).c_str());
    ImGui::SeparatorText(Tr("Game library"));
    const float card = (width-2*ImGui::GetStyle().ItemSpacing.x)/3;
    LibraryCard(Page::Display,"Display","Resolution and effects",IM_COL32(228,185,107,255),card,selected);
    ImGui::SameLine();
    LibraryCard(Page::Controls,"Controls","Button mapping",IM_COL32(157,191,211,255),card,selected);
    ImGui::SameLine();
    LibraryCard(Page::Saves,"Saves","Save files",IM_COL32(223,160,174,255),card,selected);
    if (ImGui::TreeNode(Tr("Next launch"))) {
        ImGui::TextWrapped("%s  ·  %s", Tr("Game data"), Utf8(state.config.gameData).c_str());
        ImGui::Text("%s  %.0f%%", Tr("Master volume"), state.config.masterVolume);
        ImGui::Text("%s  %s", Tr("VSync"), Tr(state.config.vsync ? "On" : "Off"));
        ImGui::Text("%s  %s", Tr("Experimental performance mode"), Tr(state.config.gpuResidentCopies ? "On" : "Off"));
        ImGui::Text("%s  %dx", Tr("Internal resolution"), state.config.resolutionScale);
        ImGui::Text("%s  %s", Tr("Anti-aliasing"), Tr(Kirby::AntiAliasingName(state.config.msaaSamples, state.config.fxaa)));
        ImGui::TreePop();
    }
    return action;
}
} // namespace Launcher
