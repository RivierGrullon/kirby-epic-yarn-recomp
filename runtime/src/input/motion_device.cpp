// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/input/motion_device.h"
#include <SDL3/SDL_guid.h>
#include <stdexcept>

namespace Kirby {
void ValidateMotionSettings(const MotionSettings& s) {
    const auto range = [](float v, float low, float high) { return std::isfinite(v) && v >= low && v <= high; };
    if (s.pointer < PointerSource::Mouse || s.pointer > PointerSource::Disabled ||
        !range(s.sensitivity, .1f, 5.f) || !range(s.tiltGain, .25f, 3.f) ||
        !range(s.deadzone, 0, .1f) || !range(s.stickDeadzone, 0, .9f))
        throw std::runtime_error("motion: invalid pointer source, sensitivity or deadzone");
    if (!s.calibrationDevice.empty() && (s.calibrationDevice.size() != 32 ||
        s.calibrationDevice.find_first_not_of("0123456789abcdef") != std::string::npos))
        throw std::runtime_error("motion.calibration_device must be an SDL GUID or empty");
    for (float v : s.bias) if (!range(v, -.15f, .15f))
        throw std::runtime_error("motion: gyro bias must be between -0.15 and 0.15 rad/s");
}
void MotionDevice::Update(SDL_Gamepad* pad, const MotionSettings& settings, float dt, bool active) {
    const auto next = pad ? SDL_GetGamepadID(pad) : 0;
    if (id != next) {
        *this = {}; id = next;
        if (pad) {
            char guid[33]{};
            SDL_GUIDToString(SDL_GetGamepadGUIDForID(id), guid, sizeof(guid));
            identity = guid;
            gyroAvailable = SDL_GamepadHasSensor(pad, SDL_SENSOR_GYRO) && SDL_SetGamepadSensorEnabled(pad, SDL_SENSOR_GYRO, true);
            accelAvailable = SDL_GamepadHasSensor(pad, SDL_SENSOR_ACCEL) && SDL_SetGamepadSensorEnabled(pad, SDL_SENSOR_ACCEL, true);
            if (identity == settings.calibrationDevice) { state.bias = settings.bias; state.calibrated = true; }
        }
    }
    MotionVector gyro{}, accel{0, 9.80665f, 0};
    gyroRead = pad && gyroAvailable && SDL_GetGamepadSensorData(pad, SDL_SENSOR_GYRO, gyro.data(), 3) && FiniteMotion(gyro);
    accelRead = pad && accelAvailable && SDL_GetGamepadSensorData(pad, SDL_SENSOR_ACCEL, accel.data(), 3) && FiniteMotion(accel);
    if (!gyroRead || !accelRead || !active) state.calibrating = false;
    if (active) {
        state.Sample(gyroRead ? gyro : MotionVector{}, accelRead ? accel : MotionVector{0, 9.80665f, 0},
                     dt, settings, gyroRead && settings.pointer == PointerSource::Gyro);
        if (pad && settings.pointer == PointerSource::RightStick && dt > 0 && dt <= .1f) {
            const auto axis = [&](SDL_GamepadAxis a) {
                const float v = SDL_GetGamepadAxis(pad, a) / 32768.f;
                return std::abs(v) <= settings.stickDeadzone ? 0.f : v;
            };
            state.x = std::clamp(state.x + axis(SDL_GAMEPAD_AXIS_RIGHTX) * dt * settings.sensitivity * (settings.invertX ? -1 : 1), -1.f, 1.f);
            state.y = std::clamp(state.y + axis(SDL_GAMEPAD_AXIS_RIGHTY) * dt * settings.sensitivity * (settings.invertY ? -1 : 1), -1.f, 1.f);
        }
    }
    const bool center = pad && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_STICK);
    if (active && center && !recenterDown) state.Recenter();
    recenterDown = center;
}
MotionVector MotionDevice::Tilt(const MotionSettings& settings) const {
    return settings.tilt && accelRead ? SidewaysAcceleration(state.acceleration, settings.tiltGain, settings.invertTilt)
                                     : MotionVector{0, -1, 0};
}
}
