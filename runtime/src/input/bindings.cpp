// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/input/bindings.h"
#include "controller_button_names.h"
#include <SDL3/SDL_keyboard.h>
#include <stdexcept>
namespace Kirby {
std::string CanonicalBinding(BindingDevice device, std::string_view token) {
    if (token == "disabled") return "disabled";
    if (device == BindingDevice::Gamepad) {
        // Reuse the runtime's positional vocabulary, including signed axes.
        const auto* item = ControllerNames::FindNativeButton(token);
        if (item && token == item->configName && item->nativeButton != PAD_NATIVE_BUTTON_INVALID)
            return item->configName;
    } else {
        const auto scancode = SDL_GetScancodeFromName(std::string(token).c_str());
        if (scancode != SDL_SCANCODE_UNKNOWN) return SDL_GetScancodeName(scancode);
    }
    throw std::runtime_error(std::string(device == BindingDevice::Gamepad ? "Unknown gamepad binding: " : "Unknown keyboard binding: ") + std::string(token));
}
const char* GamepadBindingLabel(std::string_view token) {
    const auto* item = ControllerNames::FindNativeButton(token);
    return item ? item->label : "Unknown binding";
}
std::string GamepadButtonBinding(int button) {
    if (button < 0 || button >= SDL_GAMEPAD_BUTTON_COUNT) throw std::runtime_error("Unknown gamepad button");
    return ControllerNames::NativeButtonForValue(static_cast<uint32_t>(button)).configName;
}
std::string GamepadAxisBinding(int axis, bool negative) {
    if (axis < 0 || axis >= SDL_GAMEPAD_AXIS_COUNT || (negative && axis >= SDL_GAMEPAD_AXIS_LEFT_TRIGGER))
        throw std::runtime_error("Unknown gamepad axis direction");
    return ControllerNames::NativeButtonForValue(PADEncodeAxisButton(static_cast<uint32_t>(axis), negative)).configName;
}
void ValidateControlBindings(const ControlBindings& bindings) {
    std::array<std::string, GameActions.size()> gamepad, keyboard;
    for (size_t i = 0; i < GameActions.size(); ++i) {
        gamepad[i] = CanonicalBinding(BindingDevice::Gamepad, bindings.gamepad[i]);
        keyboard[i] = CanonicalBinding(BindingDevice::Keyboard, bindings.keyboard[i]);
        if (GameActions[i].required && gamepad[i] == "disabled" && keyboard[i] == "disabled")
            throw std::runtime_error(std::string(GameActions[i].label) + " needs a gamepad or keyboard binding");
        for (size_t j = 0; j < i; ++j) {
            if ((gamepad[i] != "disabled" && gamepad[i] == gamepad[j]) ||
                (keyboard[i] != "disabled" && keyboard[i] == keyboard[j]))
                throw std::runtime_error(std::string("Binding conflict between ") + GameActions[j].label + " and " + GameActions[i].label);
        }
    }
}
RebindResult Rebind(ControlBindings& bindings, size_t action, BindingDevice device, std::string_view token, bool swap) {
    if (action >= GameActions.size()) return {RebindResult::Status::Invalid, GameActions.size(), "Unknown action"};
    try {
        const auto value = CanonicalBinding(device, token);
        auto candidate = bindings;
        auto& inputs = device == BindingDevice::Gamepad ? candidate.gamepad : candidate.keyboard;
        for (size_t i = 0; i < inputs.size(); ++i) {
            if (i == action || value == "disabled" || CanonicalBinding(device, inputs[i]) != value) continue;
            if (!swap) return {RebindResult::Status::Conflict, i, std::string("Already assigned to ") + GameActions[i].label};
            inputs[i] = inputs[action];
            break;
        }
        inputs[action] = value;
        ValidateControlBindings(candidate);
        bindings = std::move(candidate);
        return {RebindResult::Status::Applied, GameActions.size(), "Binding updated"};
    } catch (const std::exception& error) {
        return {RebindResult::Status::Invalid, GameActions.size(), error.what()};
    }
}
} // namespace Kirby
