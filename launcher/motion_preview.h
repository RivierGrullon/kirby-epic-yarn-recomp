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
};
}
