// SPDX-License-Identifier: GPL-3.0-only
#include "binding_capture.h"
#include <algorithm>
#include <cmath>

namespace Launcher {
void BindingCapture::Snapshot(SDL_JoystickID id) {
    if (auto* pad = SDL_OpenGamepad(id)) {
        auto& state = initial_[id];
        for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i)
            state.held[i] = SDL_GetGamepadButton(pad, static_cast<SDL_GamepadButton>(i));
        for (int i = 0; i < SDL_GAMEPAD_AXIS_COUNT; ++i)
            state.axisReady[i] = std::abs(int(SDL_GetGamepadAxis(pad, static_cast<SDL_GamepadAxis>(i)))) <= 8192;
        SDL_CloseGamepad(pad);
    }
}
void BindingCapture::Begin(size_t target, Kirby::BindingDevice kind, uint64_t now) {
    if (target >= Kirby::GameActions.size()) { message = "Unknown action"; return; }
    action = target; device = kind; started_ = now;
    phase = Phase::Listening; waitingRelease_ = false; code_ = -1;
    conflict = Kirby::GameActions.size(); proposed_.clear(); message.clear(); initial_.clear(); heldKeys_.fill(false);
    if (kind == Kirby::BindingDevice::Gamepad) {
        int count = 0;
        auto* ids = SDL_GetGamepads(&count);
        for (int i = 0; i < count; ++i) Snapshot(ids[i]);
        SDL_free(ids);
    } else if (SDL_WasInit(SDL_INIT_VIDEO)) {
        int count = 0;
        const auto* keys = SDL_GetKeyboardState(&count);
        if (keys) for (int i = 0; i < std::min(count, int(SDL_SCANCODE_COUNT)); ++i) heldKeys_[i] = keys[i];
    }
}
void BindingCapture::Propose(Kirby::ControlBindings& bindings, std::string token, bool& dirty) {
    proposed_ = std::move(token);
    const auto result = Kirby::Rebind(bindings, action, device, proposed_);
    message = result.message;
    if (result.status == Kirby::RebindResult::Status::Applied) {
        dirty = true; phase = Phase::WaitingRelease;
    } else if (result.status == Kirby::RebindResult::Status::Conflict) {
        conflict = result.conflict; phase = Phase::Conflict;
    } else { phase = Phase::Listening; waitingRelease_ = false; }
}
bool BindingCapture::Handle(const SDL_Event& event, Kirby::ControlBindings& bindings, bool& dirty) {
    if (!Busy()) return false;
    const auto released = [&] {
        waitingRelease_ = false;
        if (phase == Phase::WaitingRelease) phase = Phase::Idle;
    };
    if (device == Kirby::BindingDevice::Gamepad && event.type == SDL_EVENT_GAMEPAD_REMOVED) {
        if (phase == Phase::Listening || event.gdevice.which == source_) Cancel();
        return false; // The platform backend must also observe the disconnect.
    }
    if (device == Kirby::BindingDevice::Gamepad && phase == Phase::Listening && event.type == SDL_EVENT_GAMEPAD_ADDED) {
        Snapshot(event.gdevice.which); return false;
    }
    if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
        const auto code = event.key.scancode;
        if (!waitingRelease_ && event.type == SDL_EVENT_KEY_DOWN && code == SDL_SCANCODE_ESCAPE) {
            Cancel(); return true;
        }
        if (device == Kirby::BindingDevice::Gamepad) {
            return false;
        }
        if (phase == Phase::Conflict && !waitingRelease_) return false;
        if (code <= SDL_SCANCODE_UNKNOWN || code >= SDL_SCANCODE_COUNT) return true;
        if (waitingRelease_) {
            if (event.type == SDL_EVENT_KEY_UP && code == code_) released();
            return true;
        }
        if (heldKeys_[code]) { if (event.type == SDL_EVENT_KEY_UP) heldKeys_[code] = false; return true; }
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
            const char* name = SDL_GetScancodeName(code);
            if (name && *name) { code_ = code; waitingRelease_ = true; Propose(bindings, name, dirty); }
        }
        return true;
    }
    if (device == Kirby::BindingDevice::Keyboard)
        return event.type == SDL_EVENT_TEXT_INPUT || event.type == SDL_EVENT_TEXT_EDITING;
    if (phase == Phase::Conflict && !waitingRelease_) return false;
    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || event.type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
        const auto code = event.gbutton.button;
        if (code >= SDL_GAMEPAD_BUTTON_COUNT) return true;
        if (waitingRelease_) {
            if (!axis_ && event.gbutton.which == source_ && code == code_ && event.type == SDL_EVENT_GAMEPAD_BUTTON_UP) released();
            return true;
        }
        auto& initial = initial_[event.gbutton.which];
        if (initial.held[code]) { if (event.type == SDL_EVENT_GAMEPAD_BUTTON_UP) initial.held[code] = false; return true; }
        if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            source_ = event.gbutton.which; code_ = code; axis_ = false; waitingRelease_ = true;
            Propose(bindings, Kirby::GamepadButtonBinding(code), dirty);
        }
        return true;
    }
    if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
        const auto code = event.gaxis.axis;
        if (code >= SDL_GAMEPAD_AXIS_COUNT) return true;
        const int value = event.gaxis.value;
        if (waitingRelease_) {
            if (axis_ && event.gaxis.which == source_ && code == code_ && std::abs(value) <= 8192) released();
            return true;
        }
        auto& ready = initial_[event.gaxis.which].axisReady[code];
        if (std::abs(value) <= 8192) { ready = true; return true; }
        if (ready && std::abs(value) >= 22000 && !(code >= SDL_GAMEPAD_AXIS_LEFT_TRIGGER && value < 0)) {
            source_ = event.gaxis.which; code_ = code; axis_ = true; waitingRelease_ = true;
            Propose(bindings, Kirby::GamepadAxisBinding(code, value < 0), dirty);
        }
        return true;
    }
    return false;
}
void BindingCapture::Tick(uint64_t now, bool focused) {
    if (Busy() && (!focused || (phase == Phase::Listening && now - started_ >= 15000))) Cancel();
}
void BindingCapture::Cancel() {
    message = phase == Phase::WaitingRelease ? "Binding updated" : "Binding canceled";
    phase = Phase::Idle; waitingRelease_ = false; proposed_.clear();
}
void BindingCapture::Swap(Kirby::ControlBindings& bindings, bool& dirty) {
    if (phase != Phase::Conflict || waitingRelease_) return;
    const auto result = Kirby::Rebind(bindings, action, device, proposed_, true);
    message = result.message;
    if (result.status == Kirby::RebindResult::Status::Applied) { dirty = true; phase = Phase::Idle; }
}
void BindingCapture::BindEscape(Kirby::ControlBindings& bindings, bool& dirty) {
    if (phase != Phase::Listening || device != Kirby::BindingDevice::Keyboard) return;
    waitingRelease_ = false;
    Propose(bindings, "Escape", dirty);
    if (phase == Phase::WaitingRelease) phase = Phase::Idle;
}
void BindingCapture::Clear(Kirby::ControlBindings& bindings, size_t target, Kirby::BindingDevice kind, bool& dirty) {
    if (Busy()) return;
    const auto result = Kirby::Rebind(bindings, target, kind, "disabled");
    message = result.message;
    if (result.status == Kirby::RebindResult::Status::Applied) dirty = true;
}
} // namespace Launcher
