// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "kirby/input/motion.h"
#include <SDL3/SDL_gamepad.h>

namespace Kirby {
// Samples a borrowed SDL handle. The caller owns device lifetime and input focus.
struct MotionDevice {
    SDL_JoystickID id = 0;
    std::string identity;
    MotionState state;
    bool gyroAvailable = false, accelAvailable = false;
    bool gyroRead = false, accelRead = false;
    bool recenterDown = false;
    void Update(SDL_Gamepad* pad, const MotionSettings& settings, float dt, bool active);
    MotionVector Tilt(const MotionSettings& settings) const;
};
void ConfigureMotion(const MotionSettings& settings);
void UpdateControllerMotion();
}
