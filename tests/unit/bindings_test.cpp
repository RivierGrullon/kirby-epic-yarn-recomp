// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/input/bindings.h"
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks;
void Check(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
template<class F> void Reject(F&& action) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    Check(rejected, "Invalid bindings were accepted");
}
}
int main() {
    using namespace Kirby;
    constexpr auto jump = static_cast<size_t>(GameAction::Jump);
    constexpr auto yarn = static_cast<size_t>(GameAction::Yarn);
    constexpr auto home = static_cast<size_t>(GameAction::Home);
    ControlBindings bindings;
    ValidateControlBindings(bindings);
    const auto defaults = bindings;
    Check(CanonicalBinding(BindingDevice::Keyboard, "space") == "Space", "Keyboard names were not canonicalized");
    Check(CanonicalBinding(BindingDevice::Gamepad, "left_trigger") == "left_trigger", "Trigger binding was rejected");
    Reject([] { CanonicalBinding(BindingDevice::Gamepad, "south@garbage"); });
    Reject([] { CanonicalBinding(BindingDevice::Gamepad, "unmapped"); });
    Reject([] { CanonicalBinding(BindingDevice::Keyboard, "not a key"); });

    auto result = Rebind(bindings, jump, BindingDevice::Gamepad, "east");
    Check(result.status == RebindResult::Status::Conflict && result.conflict == yarn, "Conflict did not identify Yarn");
    Check(bindings == defaults, "Unresolved conflict changed the bindings");
    result = Rebind(bindings, jump, BindingDevice::Gamepad, "east", true);
    Check(result.status == RebindResult::Status::Applied && bindings.gamepad[jump] == "east" &&
          bindings.gamepad[yarn] == "south", "Swap did not preserve both actions");
    Check(bindings.keyboard == defaults.keyboard, "Gamepad swap changed keyboard bindings");
    result = Rebind(bindings, jump, BindingDevice::Keyboard, "J");
    Check(result.status == RebindResult::Status::Applied && bindings.keyboard[jump] == "J", "Keyboard rebind failed");
    const auto good = bindings;
    result = Rebind(bindings, jump, BindingDevice::Keyboard, "bogus");
    Check(result.status == RebindResult::Status::Invalid && bindings == good, "Invalid token changed bindings");
    result = Rebind(bindings, jump, BindingDevice::Gamepad, "disabled");
    Check(result.status == RebindResult::Status::Applied, "Keyboard alternative did not allow a gamepad unbind");
    const auto alternative = bindings;
    result = Rebind(bindings, jump, BindingDevice::Keyboard, "disabled");
    Check(result.status == RebindResult::Status::Invalid && bindings == alternative, "Required action lost both bindings");
    Rebind(bindings, home, BindingDevice::Gamepad, "disabled");
    Rebind(bindings, home, BindingDevice::Keyboard, "disabled");
    ValidateControlBindings(bindings);
    Check(bindings.gamepad[home] == "disabled" && bindings.keyboard[home] == "disabled", "Optional action could not be unbound");
    bindings = defaults;
    bindings.keyboard[jump] = "escape";
    Reject([&] { ValidateControlBindings(bindings); });
    bindings = defaults;
    bindings.gamepad[yarn] = "south";
    Reject([&] { ValidateControlBindings(bindings); });
    Check(Rebind(bindings, GameActions.size(), BindingDevice::Keyboard, "J").status == RebindResult::Status::Invalid,
          "Out-of-range action accepted");
    std::cout << checks << " binding validation/rebind checks passed\n";
}
