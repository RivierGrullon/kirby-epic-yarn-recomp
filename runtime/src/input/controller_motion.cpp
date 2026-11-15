// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/input/motion_device.h"
#include <aurora/aurora.h>
#include "kirby/app/events.h"
#include <aurora/input.hpp>
#include <aurora/wpad.h>
#include <dolphin/pad.h>
#include <SDL3/SDL_keyboard.h>
#include <chrono>
#include <optional>

namespace Kirby {
namespace {
std::optional<MotionSettings> configured;
std::array<MotionDevice, PAD_CHANMAX> devices;
std::chrono::steady_clock::time_point lastUpdate;
bool recenterKeyDown = false;
}
void ConfigureMotion(const MotionSettings& settings) {
    ValidateMotionSettings(settings); configured = settings;
    hostMotionUpdate = UpdateControllerMotion;
    devices = {}; lastUpdate = {}; recenterKeyDown = false;
}
void UpdateControllerMotion() {
    if (!configured) return;
    const auto now = std::chrono::steady_clock::now();
    const float dt = lastUpdate.time_since_epoch().count() ? std::chrono::duration<float>(now - lastUpdate).count() : 0.f;
    lastUpdate = now;
    const auto& settings = *configured;
    const bool blocked = PADIsInputBlocked();
    const bool keyboard = !blocked && settings.keyboard && !aurora::input::captured_above(
        aurora::input::keyboard_source().id, aurora::input::kGameLayerPriority);
    const bool* keys = SDL_GetKeyboardState(nullptr);
    const bool center = keys[SDL_SCANCODE_R];
    for (unsigned port = 0; port < devices.size(); ++port) {
        const auto index = PADGetIndexForPort(port);
        auto* pad = index < 0 ? nullptr : PADGetSDLGamepadForIndex(index);
        const bool active = !blocked && (!pad || !aurora::input::captured_above(
            aurora::input::source_for_gamepad(SDL_GetGamepadID(pad)), aurora::input::kGameLayerPriority));
        auto& device = devices[port];
        device.Update(pad, settings, dt, active);
        auto tilt = active ? device.Tilt(settings) : MotionVector{0, -1, 0};
        if (port == 0 && keyboard) {
            const int direction = int(keys[SDL_SCANCODE_E]) - int(keys[SDL_SCANCODE_Q]);
            if (direction) {
                const float angle = direction * .65f * (settings.invertTilt ? -1.f : 1.f);
                tilt = {0, -std::cos(angle), -std::sin(angle)};
            }
            if (center && !recenterKeyDown) device.state.Recenter();
        }
        const Vec acceleration{tilt[0], tilt[1], tilt[2]};
        aurora_wpad_set_acceleration(port, &acceleration, nullptr);
        if (settings.pointer == PointerSource::Mouse || (settings.pointer == PointerSource::Gyro && !device.gyroRead)) {
            aurora_wpad_set_pointer(port, nullptr);
        } else {
            const AuroraWpadPointer pointer{device.state.x, device.state.y, active &&
                ((settings.pointer == PointerSource::Gyro && device.gyroRead) ||
                 (settings.pointer == PointerSource::RightStick && pad))};
            aurora_wpad_set_pointer(port, &pointer);
        }
    }
    recenterKeyDown = center;
}
}
