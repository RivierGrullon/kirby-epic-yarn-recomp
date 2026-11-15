// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/input/motion_device.h"
#include <SDL3/SDL.h>
#include <iostream>
#include <limits>
#include <stdexcept>

void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
bool Near(float a, float b) { return std::abs(a - b) < .001f; }
int main() {
    using namespace Kirby;
    MotionSettings settings;
    auto tilt = SidewaysAcceleration({9.80665f, 0, 0}, 1, false);
    Check(Near(tilt[0], 0) && Near(tilt[1], 0) && Near(tilt[2], -1), "Sideways gravity axes are incorrect");
    Check(Near(SidewaysAcceleration({9.80665f, 0, 0}, 1, true)[2], 1), "Tilt inversion failed");
    MotionState a, b;
    for (int i = 0; i < 100; ++i) a.Sample({.1f, -.2f, 0}, {0, 9.80665f, 0}, .01f, settings, true);
    for (int i = 0; i < 50; ++i) b.Sample({.1f, -.2f, 0}, {0, 9.80665f, 0}, .02f, settings, true);
    Check(Near(a.x, -.3f) && Near(a.x, b.x) && Near(a.y, .15f), "Gyro integration depends on polling rate");
    a.Sample({10, 10, 0}, {0, 9.80665f, 0}, 1.f, settings, true);
    a.Sample({NAN, 1, 0}, {0, 9.80665f, 0}, .01f, settings, true);
    Check(Near(a.x, -.3f), "Stall or invalid sensor sample moved pointer");
    settings.invertX = settings.invertY = true;
    a.Recenter(); a.Sample({.1f, -.2f, 0}, {0, 9.80665f, 0}, .1f, settings, true);
    Check(a.x > 0 && a.y < 0, "Pointer inversion failed");
    a.Calibrate();
    for (int i = 0; i < 100; ++i) a.Sample({.02f, -.03f, 0}, {0, 9.80665f, 0}, .01f, settings, true);
    a.Sample({1, 0, 0}, {0, 9.80665f, 0}, .01f, settings, true);
    Check(a.calibrationSeconds == 0, "Movement did not restart calibration");
    for (int i = 0; i < 210; ++i) a.Sample({.02f, -.03f, 0}, {0, 9.80665f, 0}, .01f, settings, true);
    Check(a.calibrated && !a.calibrating && Near(a.bias[1], -.03f), "Stationary gyro bias was not calibrated");
    a.Sample({.02f, -.03f, 0}, {0, 9.80665f, 0}, .01f, settings, true);
    Check(a.x == 0 && a.y == 0, "Calibrated stationary gyro drifted");

    Check(SDL_Init(SDL_INIT_GAMEPAD), SDL_GetError());
    SDL_VirtualJoystickSensorDesc sensors[]{{SDL_SENSOR_GYRO, 200}, {SDL_SENSOR_ACCEL, 200}};
    SDL_VirtualJoystickDesc desc{}; SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD; desc.name = "Kirby synthetic motion test";
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT; desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    desc.nsensors = 2; desc.sensors = sensors;
    const auto id = SDL_AttachVirtualJoystick(&desc);
    Check(id != 0, SDL_GetError());
    auto* pad = SDL_OpenGamepad(id); Check(pad != nullptr, SDL_GetError());
    auto* joystick = SDL_GetGamepadJoystick(pad);
    MotionDevice device;
    settings = {};
    device.Update(pad, settings, .01f, true);
    const float gyro[]{0, -.5f, 0}, accel[]{9.80665f, 0, 0};
    const auto sample = [&] {
        Check(SDL_SendJoystickVirtualSensorData(joystick, SDL_SENSOR_GYRO, SDL_GetTicksNS(), gyro, 3), SDL_GetError());
        Check(SDL_SendJoystickVirtualSensorData(joystick, SDL_SENSOR_ACCEL, SDL_GetTicksNS(), accel, 3), SDL_GetError());
        SDL_UpdateGamepads();
        device.Update(pad, settings, .01f, true);
    };
    for (int i = 0; i < 50; ++i) sample();
    Check(device.gyroRead && device.accelRead && device.state.x < -.3f && device.Tilt(settings)[2] < -.99f,
          "Virtual SDL sensors did not reach motion output");
    const float x = device.state.x;
    device.Update(pad, settings, .05f, false);
    Check(device.state.x == x, "Inactive device integrated gyro");
    settings.tilt = false;
    Check(device.Tilt(settings) == MotionVector{0, -1, 0}, "Disabled tilt leaked sensor input");
    settings.calibrationDevice = device.identity; settings.bias = {.01f, -.02f, 0};
    device.Update(nullptr, settings, .01f, true);
    Check(!device.gyroRead && !device.accelRead && device.state.x == 0, "Disconnect retained motion state");
    device.Update(pad, settings, .01f, true);
    Check(device.state.calibrated && device.state.bias == settings.bias, "Matching controller calibration not restored");
    device.Update(nullptr, settings, .01f, true);
    settings.calibrationDevice = "00000000000000000000000000000000";
    device.Update(pad, settings, .01f, true);
    Check(!device.state.calibrated, "Calibration applied to unrelated controller");
    settings.pointer = PointerSource::RightStick;
    Check(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHTX, 32767), SDL_GetError());
    SDL_UpdateGamepads(); device.Update(pad, settings, .05f, true);
    Check(device.state.x > 0, "Right-stick pointer did not move");
    Check(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_RIGHT_STICK, true), SDL_GetError());
    SDL_UpdateGamepads(); device.Update(pad, settings, .01f, true);
    Check(device.state.x == 0, "Right-stick click did not recenter");
    SDL_CloseGamepad(pad); SDL_DetachVirtualJoystick(id); SDL_Quit();
    std::cout << "Motion math, calibration, virtual sensors, disconnect and focus checks passed\n";
}
