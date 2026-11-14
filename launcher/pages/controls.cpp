// SPDX-License-Identifier: GPL-3.0-only
#include "pages.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <algorithm>

namespace Launcher {
Action DrawControls(State& state) {
    int count = 0;
    SDL_JoystickID* controllers = SDL_GetGamepads(&count);
    ImGui::Text("Controllers detected: %d", count);
    for (int i = 0; i < count; ++i) {
        const char* name = SDL_GetGamepadNameForID(controllers[i]);
        ImGui::TextWrapped("%s", name ? name : "Unnamed controller");
    }
    SDL_free(controllers);
    if (count == 0) ImGui::TextWrapped("Connect a controller to see it here.");
    ImGui::Spacing();
    ImGui::TextWrapped("Select a binding, then press the new input. Gamepad bindings apply to all players; keyboard bindings apply to player 1. Launcher navigation stays unchanged.");
    auto& capture = state.bindingCapture;
    if (capture.Busy()) {
        ImGui::SeparatorText(Kirby::GameActions[capture.action].label);
        if (capture.WaitingForRelease()) ImGui::TextWrapped("Release the captured input to continue.");
        else if (capture.phase == BindingCapture::Phase::Listening)
            ImGui::TextWrapped("%s", capture.device == Kirby::BindingDevice::Gamepad
                ? "Press a gamepad button or move a centered stick/trigger. Escape or Cancel stops capture."
                : "Press a keyboard key. Escape cancels capture; Use Escape assigns that key.");
        if (!capture.message.empty()) ImGui::TextWrapped("%s", capture.message.c_str());
        ImGui::BeginDisabled(capture.WaitingForRelease());
        if (capture.phase == BindingCapture::Phase::Conflict) {
            if (ImGui::Button("Swap bindings")) capture.Swap(state.config.controls, state.settingsDirty);
            ImGui::SameLine();
        }
        if (capture.phase == BindingCapture::Phase::Listening && capture.device == Kirby::BindingDevice::Keyboard) {
            if (ImGui::Button("Use Escape")) capture.BindEscape(state.config.controls, state.settingsDirty);
            ImGui::SameLine();
        }
        if (ImGui::Button("Cancel capture")) capture.Cancel();
        ImGui::EndDisabled();
    } else if (!capture.message.empty()) ImGui::TextWrapped("%s", capture.message.c_str());
    const float tableHeight = std::max(110.f, ImGui::GetContentRegionAvail().y - 100.f);
    if (ImGui::BeginTable("Bindings", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp |
                                          ImGuiTableFlags_ScrollY, {0, tableHeight})) {
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, .30f);
        ImGui::TableSetupColumn("Gamepad", ImGuiTableColumnFlags_WidthStretch, .43f);
        ImGui::TableSetupColumn("Keyboard", ImGuiTableColumnFlags_WidthStretch, .27f);
        ImGui::TableSetupScrollFreeze(0, 1); ImGui::TableHeadersRow();
        for (size_t i = 0; i < Kirby::GameActions.size(); ++i) {
            ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextWrapped("%s", Kirby::GameActions[i].label);
            ImGui::PushID(static_cast<int>(i));
            for (const auto device : {Kirby::BindingDevice::Gamepad, Kirby::BindingDevice::Keyboard}) {
                ImGui::TableNextColumn(); ImGui::PushID(static_cast<int>(device));
                const auto& token = device == Kirby::BindingDevice::Gamepad ? state.config.controls.gamepad[i] : state.config.controls.keyboard[i];
                const char* label = device == Kirby::BindingDevice::Gamepad ? Kirby::GamepadBindingLabel(token)
                    : token == "disabled" ? "Unmapped" : token.c_str();
                ImGui::BeginDisabled(state.selfTest || !state.settingsError.empty() || capture.Busy());
                const float clearWidth = ImGui::CalcTextSize("Clear").x + ImGui::GetStyle().FramePadding.x * 2;
                const float width = std::max(40.f, ImGui::GetContentRegionAvail().x - clearWidth - ImGui::GetStyle().ItemSpacing.x);
                const std::string buttonId = std::string(label) + "###Binding";
                if (ImGui::Button(buttonId.c_str(), {width, 0})) {
                    capture.Begin(i, device, SDL_GetTicks());
                    ImGui::GetIO().ClearInputKeys();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s: %s", Kirby::GameActions[i].label, label);
                ImGui::SameLine();
                if (ImGui::Button("Clear", {clearWidth, 0}))
                    capture.Clear(state.config.controls, i, device, state.settingsDirty);
                ImGui::EndDisabled(); ImGui::PopID();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    ImGui::BeginDisabled(state.selfTest || !state.settingsError.empty() || capture.Busy());
    if (ImGui::Button("Reset bindings")) {
        state.config.controls = {}; state.settingsDirty = true; capture.message = "Default bindings restored";
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("Per-device profiles, motion configuration and player assignment are not available yet.");
    return Action::None;
}
} // namespace Launcher
