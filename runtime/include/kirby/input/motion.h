// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace Kirby {
using MotionVector = std::array<float, 3>;
enum class PointerSource { Mouse, Gyro, RightStick, Disabled };
struct MotionSettings {
    bool tilt = true, invertTilt = false, invertX = false, invertY = false;
    bool keyboard = true;
    PointerSource pointer = PointerSource::Gyro;
    float sensitivity = 1.5f, tiltGain = 1.f, deadzone = .008f, stickDeadzone = .2f;
    // Calibration belongs to this SDL GUID, not to unrelated controller models.
    std::string calibrationDevice;
    MotionVector bias{};
    bool operator==(const MotionSettings&) const = default;
};
void ValidateMotionSettings(const MotionSettings& settings);
inline bool FiniteMotion(const MotionVector& v) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}
inline MotionVector SidewaysAcceleration(MotionVector a, float gain, bool invert) {
    if (!FiniteMotion(a)) return {0, -1, 0};
    const float angle = std::atan2(a[0], a[1]) * gain * (invert ? -1.f : 1.f);
    const float radius = std::hypot(a[0], a[1]) / 9.80665f;
    return {-a[2] / 9.80665f, -radius * std::cos(angle), -radius * std::sin(angle)};
}
struct MotionState {
    float x = 0, y = 0;
    MotionVector bias{}, sum{}, acceleration{0, 9.80665f, 0};
    float calibrationSeconds = 0;
    unsigned calibrationSamples = 0;
    bool calibrating = false, calibrated = false;
    void Recenter() { x = y = 0; }
    void Calibrate() {
        sum = {}; calibrationSeconds = 0; calibrationSamples = 0; calibrating = true;
    }
    void Sample(MotionVector gyro, MotionVector accel, float dt, const MotionSettings& settings, bool integrate) {
        if (!FiniteMotion(gyro) || !FiniteMotion(accel) || !std::isfinite(dt) || dt <= 0 || dt > .1f) return;
        const float blend = 1 - std::exp(-dt / .035f);
        for (unsigned i = 0; i < 3; ++i) acceleration[i] += blend * (accel[i] - acceleration[i]);
        if (calibrating) {
            const float gravity = std::hypot(accel[0], accel[1], accel[2]);
            const float speed = std::hypot(gyro[0], gyro[1], gyro[2]);
            if (speed > .15f || std::abs(gravity - 9.80665f) > .7f) {
                sum = {}; calibrationSeconds = 0; calibrationSamples = 0; return;
            }
            for (unsigned i = 0; i < 3; ++i) sum[i] += gyro[i] * dt;
            calibrationSeconds += dt; ++calibrationSamples;
            if (calibrationSeconds >= 2.f && calibrationSamples >= 50) {
                for (unsigned i = 0; i < 3; ++i) bias[i] = sum[i] / calibrationSeconds;
                calibrated = true; calibrating = false; Recenter();
            }
            return;
        }
        if (!integrate) return;
        const auto rate = [&](unsigned axis) {
            const float v = gyro[axis] - bias[axis];
            return std::abs(v) <= settings.deadzone ? 0.f : v;
        };
        x = std::clamp(x + rate(1) * settings.sensitivity * dt * (settings.invertX ? -1.f : 1.f), -1.f, 1.f);
        y = std::clamp(y + rate(0) * settings.sensitivity * dt * (settings.invertY ? -1.f : 1.f), -1.f, 1.f);
    }
};
}
