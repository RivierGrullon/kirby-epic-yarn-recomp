// SPDX-License-Identifier: GPL-3.0-only
#include "pages.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <algorithm>

namespace Launcher {
namespace {
void DrawMotion(State& state, const SDL_JoystickID* controllers, int count) {
    ImGui::SeparatorText("Gyroscope & gravity / tilt");
    ImGui::Text("Controllers detected: %d", count);
    for (int i = 0; i < count; ++i) {
        const char* name = SDL_GetGamepadNameForID(controllers[i]);
        ImGui::TextWrapped("%s", name ? name : "Unnamed controller");
    }
    if (count == 0) ImGui::TextWrapped("Connect a controller to see it here.");
    auto& settings = state.config.motion;
    auto& preview = state.motionPreview;
    const bool active = !state.selfTest && state.settingsError.empty() && !state.bindingCapture.Busy() &&
        SDL_GetKeyboardFocus() && !(SDL_GetWindowFlags(SDL_GetKeyboardFocus()) & SDL_WINDOW_MINIMIZED);
    if (!active) preview.Close();
    if (preview.pad && !SDL_GamepadConnected(preview.pad)) preview.Close();
    if (active && !preview.pad && count > 0) preview.Select(controllers[0]);
    ImGui::BeginDisabled(!active);
    const char* name = preview.pad ? SDL_GetGamepadName(preview.pad) : "No controller connected";
    if (ImGui::BeginCombo("Preview controller", name ? name : "Unnamed controller")) {
        for (int i = 0; i < count; ++i) {
            ImGui::PushID(static_cast<int>(controllers[i]));
            const char* label = SDL_GetGamepadNameForID(controllers[i]);
            if (ImGui::Selectable(label ? label : "Unnamed controller", preview.pad && SDL_GetGamepadID(preview.pad) == controllers[i]))
                preview.Select(controllers[i]);
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    const Uint64 now = SDL_GetTicksNS();
    const float dt = preview.lastTick ? float(now - preview.lastTick) / 1.e9f : 0.f;
    preview.lastTick = now;
    const bool calibrating = preview.device.state.calibrating;
    preview.device.Update(preview.pad, settings, dt, active);
    auto& motion = preview.device.state;
    if (calibrating && !motion.calibrating) {
        if (motion.calibrated && motion.calibrationSeconds >= 2.f) {
            settings.calibrationDevice = preview.device.identity; settings.bias = motion.bias;
            state.settingsDirty = true; preview.message = "Calibration complete. Save settings to use it in game.";
        } else preview.message = "Calibration stopped: sensor data unavailable.";
    }
    ImGui::TextWrapped("Gyroscope: %s    Accelerometer: %s",
        preview.device.gyroRead ? "available" : "unavailable", preview.device.accelRead ? "available" : "unavailable");
    if (preview.pad && (!preview.device.gyroRead || !preview.device.accelRead))
        ImGui::TextWrapped("Use a controller with motion sensors exposed to SDL. Xbox-emulated devices may hide them.");
    state.settingsDirty |= ImGui::Checkbox("Use controller tilt (gravity)", &settings.tilt);
    ImGui::SameLine(); state.settingsDirty |= ImGui::Checkbox("Reverse tilt", &settings.invertTilt);
    state.settingsDirty |= ImGui::SliderFloat("Tilt sensitivity", &settings.tiltGain, .25f, 3.f, "%.2fx");
    int pointer = static_cast<int>(settings.pointer);
    if (ImGui::Combo("Pointer source", &pointer, "Mouse\0Gyroscope (mouse fallback)\0Right stick\0Disabled\0")) {
        settings.pointer = static_cast<Kirby::PointerSource>(pointer); state.settingsDirty = true; motion.Recenter();
    }
    ImGui::BeginDisabled(settings.pointer != Kirby::PointerSource::Gyro && settings.pointer != Kirby::PointerSource::RightStick);
    state.settingsDirty |= ImGui::SliderFloat("Pointer sensitivity", &settings.sensitivity, .1f, 5.f, "%.2fx");
    state.settingsDirty |= ImGui::Checkbox("Invert pointer X", &settings.invertX);
    ImGui::SameLine(); state.settingsDirty |= ImGui::Checkbox("Invert pointer Y", &settings.invertY);
    ImGui::EndDisabled();
    state.settingsDirty |= ImGui::SliderFloat("Gyro deadzone (rad/s)", &settings.deadzone, 0.f, .1f, "%.3f");
    state.settingsDirty |= ImGui::SliderFloat("Pointer stick deadzone", &settings.stickDeadzone, 0.f, .9f, "%.2f");
    state.settingsDirty |= ImGui::Checkbox("Keyboard motion: Q / E tilt, R recenter", &settings.keyboard);
    ImGui::TextWrapped("Press the right stick to recenter in game. Motion settings apply to all players; keyboard motion controls player 1.");
    if (ImGui::Button("Recenter preview")) motion.Recenter();
    ImGui::SameLine();
    ImGui::BeginDisabled(!preview.device.gyroRead || !preview.device.accelRead || motion.calibrating);
    if (ImGui::Button("Calibrate gyro")) { motion.Calibrate(); preview.message = "Hold the controller still for 2 seconds."; }
    ImGui::EndDisabled();
    if (motion.calibrating) ImGui::ProgressBar(motion.calibrationSeconds / 2.f, {-1, 0}, "Hold still; movement restarts calibration");
    if (!preview.message.empty()) ImGui::TextWrapped("%s", preview.message.c_str());
    const auto tilt = preview.device.Tilt(settings);
    ImGui::Text("Tilt: %+.1f deg    Pointer: %+.2f, %+.2f", std::atan2(-tilt[2], -tilt[1]) * 57.29578f, motion.x, motion.y);
    const ImVec2 size{ImGui::GetContentRegionAvail().x, 64};
    const auto origin = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, IM_COL32(30, 34, 45, 255), 4);
    ImGui::GetWindowDrawList()->AddCircleFilled({origin.x + (motion.x + 1) * .5f * (size.x - 12) + 6,
        origin.y + (motion.y + 1) * .5f * (size.y - 12) + 6}, 5, IM_COL32(255, 168, 204, 255));
    ImGui::Dummy(size);
    if (ImGui::Button("Reset motion settings")) {
        settings = {}; preview.device = {}; preview.message.clear(); state.settingsDirty = true;
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("Calibration is saved for the selected controller model. Settings and calibration apply on the next game launch.");
}
}
Action DrawControls(State& state) {
    ImGui::SeparatorText("Button bindings");
    ImGui::TextWrapped("Select a binding, then press the new input. Gamepad bindings apply to all players; keyboard bindings apply to player 1. Launcher navigation stays unchanged.");
    auto& capture = state.bindingCapture;
    ImGui::BeginDisabled(state.selfTest || !state.settingsError.empty() || capture.Busy());
    float stickActivation = state.config.controls.stickActivation * 100.f;
    float triggerActivation = state.config.controls.triggerActivation * 100.f;
    if (ImGui::SliderFloat("Stick activation", &stickActivation, 5.f, 95.f, "%.0f%%")) {
        state.config.controls.stickActivation = stickActivation / 100.f;
        state.settingsDirty = true;
    }
    if (ImGui::SliderFloat("Trigger activation", &triggerActivation, 5.f, 95.f, "%.0f%%")) {
        state.config.controls.triggerActivation = triggerActivation / 100.f;
        state.settingsDirty = true;
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("For actions assigned to an axis, choose how far to move the stick or trigger before the action activates.");
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
    const float tableHeight = ImGui::GetTextLineHeightWithSpacing() * 13.f;
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
    if (ImGui::Button("Reset controls")) {
        state.config.controls = {}; state.settingsDirty = true; capture.message = "Default bindings and activation levels restored";
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("Per-device binding profiles and player assignment are not available yet.");
    ImGui::Spacing();
    int count = 0;
    SDL_JoystickID* controllers = SDL_GetGamepads(&count);
    DrawMotion(state, controllers, count);
    SDL_free(controllers);
    return Action::None;
}
} // namespace Launcher
