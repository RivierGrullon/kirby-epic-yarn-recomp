// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace Kirby {
enum class GameAction { Up, Down, Left, Right, Jump, Yarn, WiiA, WiiB, Pause, Map, Home, Count };
enum class BindingDevice { Gamepad, Keyboard };
struct ActionInfo {
    const char* key;
    const char* label;
    const char* defaultGamepad;
    const char* defaultKeyboard;
    bool required;
};
inline constexpr std::array<ActionInfo, static_cast<size_t>(GameAction::Count)> GameActions{{
    {"up", "Move up", "dpad_up", "Up", true},
    {"down", "Move down", "dpad_down", "Down", true},
    {"left", "Move left", "dpad_left", "Left", true},
    {"right", "Move right", "dpad_right", "Right", true},
    {"a", "Jump / Confirm", "south", "Space", true},
    {"b", "Yarn / Cancel", "east", "Left Ctrl", true},
    {"y", "Wii A", "north", "X", false},
    {"x", "Wii B", "west", "Z", false},
    {"start", "Pause / Plus", "start", "Escape", true},
    {"back", "Map / Minus", "back", "Tab", true},
    {"guide", "Home", "guide", "F1", false},
}};
struct ControlBindings {
    std::array<std::string, GameActions.size()> gamepad, keyboard;
    ControlBindings() {
        for (size_t i = 0; i < GameActions.size(); ++i) {
            gamepad[i] = GameActions[i].defaultGamepad;
            keyboard[i] = GameActions[i].defaultKeyboard;
        }
    }
    bool operator==(const ControlBindings&) const = default;
};

// Canonical positional gamepad tokens and physical SDL scancode names.
// "disabled" is the explicit unbound value. Invalid tokens throw.
std::string CanonicalBinding(BindingDevice device, std::string_view token);
const char* GamepadBindingLabel(std::string_view token);
std::string GamepadButtonBinding(int button);
std::string GamepadAxisBinding(int axis, bool negative);
void ValidateControlBindings(const ControlBindings& bindings);
struct RebindResult {
    enum class Status { Applied, Conflict, Invalid } status;
    size_t conflict = GameActions.size();
    std::string message;
};
// Conflicts do not mutate bindings unless the caller explicitly requests a swap.
RebindResult Rebind(ControlBindings& bindings, size_t action, BindingDevice device,
                    std::string_view token, bool swap = false);
} // namespace Kirby
