// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "kirby/input/motion_device.h"

namespace Launcher {
struct MotionPreview {
    SDL_Gamepad* pad = nullptr;
    Kirby::MotionDevice device;
    Uint64 lastTick = 0;
    std::string message;
    void Close() {
        if (pad) SDL_CloseGamepad(pad);
        pad = nullptr; device = {}; lastTick = 0;
    }
    void Select(SDL_JoystickID id) {
        Close();
        if (id) pad = SDL_OpenGamepad(id);
        message.clear();
    }
    void Update(const Kirby::MotionSettings& gameSettings, float dt) {
        auto settings = gameSettings;
        // Mouse and disabled are game output modes; the sensor preview still receives gyro input.
        if (settings.pointer != Kirby::PointerSource::RightStick)
            settings.pointer = Kirby::PointerSource::Gyro;
        device.Update(pad, settings, dt, true);
    }
    float TiltDegrees(const Kirby::MotionSettings& settings) const {
        const auto tilt = Kirby::SidewaysAcceleration(device.state.acceleration, settings.tiltGain, settings.invertTilt);
        return std::atan2(-tilt[2], -tilt[1]) * 57.29578f;
    }
};
}
