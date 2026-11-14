// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "kirby/input/bindings.h"
#include <SDL3/SDL.h>
#include <array>
#include <map>

namespace Launcher {
class BindingCapture {
public:
    enum class Phase { Idle, Listening, WaitingRelease, Conflict };
    Phase phase = Phase::Idle;
    size_t action = 0, conflict = Kirby::GameActions.size();
    Kirby::BindingDevice device = Kirby::BindingDevice::Gamepad;
    std::string message;

    void Begin(size_t target, Kirby::BindingDevice kind, uint64_t now);
    bool Handle(const SDL_Event& event, Kirby::ControlBindings& bindings, bool& dirty);
    void Tick(uint64_t now, bool focused);
    void Cancel();
    void Swap(Kirby::ControlBindings& bindings, bool& dirty);
    void BindEscape(Kirby::ControlBindings& bindings, bool& dirty);
    void Clear(Kirby::ControlBindings& bindings, size_t target, Kirby::BindingDevice kind, bool& dirty);
    bool Busy() const { return phase != Phase::Idle; }
    bool WaitingForRelease() const { return waitingRelease_; }
    bool Blocks(Kirby::BindingDevice kind) const {
        return Busy() && device == kind && (phase != Phase::Conflict || waitingRelease_);
    }
private:
    struct InitialState {
        std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> held{};
        std::array<bool, SDL_GAMEPAD_AXIS_COUNT> axisReady = [] { std::array<bool, SDL_GAMEPAD_AXIS_COUNT> a; a.fill(true); return a; }();
    };
    std::map<SDL_JoystickID, InitialState> initial_;
    std::array<bool, SDL_SCANCODE_COUNT> heldKeys_{};
    std::string proposed_;
    uint64_t started_ = 0;
    SDL_JoystickID source_ = 0;
    int code_ = -1;
    bool axis_ = false, waitingRelease_ = false;
    void Snapshot(SDL_JoystickID id);
    void Propose(Kirby::ControlBindings& bindings, std::string token, bool& dirty);
};
} // namespace Launcher
