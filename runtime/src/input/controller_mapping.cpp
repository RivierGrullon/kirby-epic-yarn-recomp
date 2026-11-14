// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/input/wpad.h"
#include "kirby/input/bindings.h"
#include "controller_button_names.h"
#include <aurora/pad.hpp>
#include <dolphin/pad.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_keyboard.h>
#include <algorithm>
#include <iostream>
#include <optional>

// Default axis/button layout adapted from Aurora's lib/dolphin/pad/pad.cpp.
// The MIT License
// Copyright (c) 2022 Luke Street
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

namespace Kirby {
namespace {
std::optional<ControlBindings> configuredBindings;
}
void ConfigureControlBindings(const ControlBindings& bindings) {
    ValidateControlBindings(bindings);
    configuredBindings = bindings;
}
void ApplyConfiguredControlBindings() {
    if (!configuredBindings) return;
    using namespace aurora::binding;
    const auto& controls = aurora::pad::controls();
    const std::vector<ControlId> targets{
        controls.up, controls.down, controls.left, controls.right,
        controls.a, controls.b, controls.y, controls.x, controls.start,
        aurora::pad::control_for_ext_button(PAD_BUTTON_BACK),
        aurora::pad::control_for_ext_button(PAD_BUTTON_GUIDE)};
    for (uint32_t port = 0; port < PAD_CHANMAX; ++port) {
        std::vector<Binding> bindings;
        for (size_t i = 0; i < GameActions.size(); ++i) {
            const auto& token = configuredBindings->gamepad[i];
            if (token != "disabled") {
                const auto native = ControllerNames::FindNativeButton(token)->nativeButton;
                Binding binding;
                binding.target = targets[i];
                if (PADIsAxisButton(native)) {
                    using Direction = PhysicalInput::GamepadAxis::Direction;
                    binding.input.control = PhysicalInput::GamepadAxis{
                        static_cast<SDL_GamepadAxis>(PADAxisButtonAxis(native)),
                        PADAxisButtonNegative(native) ? Direction::Negative : Direction::Positive};
                    binding.threshold = PADAxisButtonThreshold(native) / 100.f;
                } else binding.input.control = PhysicalInput::GamepadButton{static_cast<SDL_GamepadButton>(native)};
                bindings.push_back(std::move(binding));
            }
            const auto& key = configuredBindings->keyboard[i];
            if (port == 0 && key != "disabled") {
                Binding binding;
                binding.target = targets[i];
                binding.input.control = PhysicalInput::Key{SDL_GetScancodeFromName(key.c_str())};
                bindings.push_back(std::move(binding));
            }
        }
        // The shared TOML schema owns bindings. Do not select Aurora's separate
        // exclusive keyboard mode, which would hide the assigned controller.
        PADSetKeyboardActive(port, FALSE);
        const bool keyboard = port == 0 && std::ranges::any_of(configuredBindings->keyboard,
            [](const auto& value) { return value != "disabled"; });
        aurora::pad::set_action_bindings(port, std::move(bindings), targets, keyboard);
    }
    std::cout << "[kirby] configured gamepad/keyboard bindings applied\n";
}
void ConfigureControllerDefaults() {
    // Kirby uses positional face buttons, unlike Aurora's GameCube Xbox preset.
    // Apply before PADInit. Video launches replace button controls from the
    // shared settings afterward; isolated SDK probes keep these defaults.
    const PADDefaultMapping mapping{
        {{SDL_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
         {SDL_GAMEPAD_BUTTON_EAST, PAD_BUTTON_B},
         {SDL_GAMEPAD_BUTTON_WEST, PAD_BUTTON_X},
         {SDL_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
         {SDL_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
         {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
         {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L},
         {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R},
         {SDL_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
         {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
         {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
         {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT}},
        {{{SDL_GAMEPAD_AXIS_LEFTX, AXIS_SIGN_POSITIVE}, SDL_GAMEPAD_BUTTON_INVALID, PAD_AXIS_LEFT_X_POS},
         {{SDL_GAMEPAD_AXIS_LEFTX, AXIS_SIGN_NEGATIVE}, SDL_GAMEPAD_BUTTON_INVALID, PAD_AXIS_LEFT_X_NEG},
         {{SDL_GAMEPAD_AXIS_LEFTY, AXIS_SIGN_NEGATIVE}, SDL_GAMEPAD_BUTTON_INVALID, PAD_AXIS_LEFT_Y_POS},
         {{SDL_GAMEPAD_AXIS_LEFTY, AXIS_SIGN_POSITIVE}, SDL_GAMEPAD_BUTTON_INVALID, PAD_AXIS_LEFT_Y_NEG},
         {{SDL_GAMEPAD_AXIS_RIGHTX, AXIS_SIGN_POSITIVE}, SDL_GAMEPAD_BUTTON_INVALID, PAD_AXIS_RIGHT_X_POS},
         {{SDL_GAMEPAD_AXIS_RIGHTX, AXIS_SIGN_NEGATIVE}, SDL_GAMEPAD_BUTTON_INVALID, PAD_AXIS_RIGHT_X_NEG},
         {{SDL_GAMEPAD_AXIS_RIGHTY, AXIS_SIGN_NEGATIVE}, SDL_GAMEPAD_BUTTON_INVALID, PAD_AXIS_RIGHT_Y_POS},
         {{SDL_GAMEPAD_AXIS_RIGHTY, AXIS_SIGN_POSITIVE}, SDL_GAMEPAD_BUTTON_INVALID, PAD_AXIS_RIGHT_Y_NEG},
         {{SDL_GAMEPAD_AXIS_LEFT_TRIGGER, AXIS_SIGN_POSITIVE}, SDL_GAMEPAD_BUTTON_INVALID, PAD_AXIS_TRIGGER_L},
         {{SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, AXIS_SIGN_POSITIVE}, SDL_GAMEPAD_BUTTON_INVALID, PAD_AXIS_TRIGGER_R}}};
    PADSetDefaultMapping(&mapping, PAD_TYPE_XBOX360);
    PADSetDefaultMapping(&mapping, PAD_TYPE_XBOXONE);
}
}
