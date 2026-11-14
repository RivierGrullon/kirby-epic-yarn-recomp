// SPDX-License-Identifier: GPL-3.0-only
#include "binding_capture.h"
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks;
void Check(bool pass, const char* message) { ++checks; if (!pass) throw std::runtime_error(message); }
SDL_Event Key(SDL_Scancode code, bool down, bool repeat = false) {
    SDL_Event e{}; e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.scancode = code; e.key.down = down; e.key.repeat = repeat; return e;
}
SDL_Event Button(SDL_GamepadButton code, bool down) {
    SDL_Event e{}; e.type = down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
    e.gbutton.which = 123; e.gbutton.button = code; e.gbutton.down = down; return e;
}
}
int main() {
    Check(SDL_Init(SDL_INIT_GAMEPAD), SDL_GetError());
    using namespace Kirby;
    using Phase = Launcher::BindingCapture::Phase;
    const auto jump = static_cast<size_t>(GameAction::Jump), yarn = static_cast<size_t>(GameAction::Yarn);
    ControlBindings bindings;
    const auto original = bindings;
    Launcher::BindingCapture capture;
    bool dirty = false;
    capture.Begin(jump, BindingDevice::Keyboard, 100);
    Check(capture.phase == Phase::Listening, "Capture did not start");
    Check(capture.Handle(Key(SDL_SCANCODE_J, true, true), bindings, dirty) && !dirty, "Key repeat was bound");
    Check(capture.Handle(Key(SDL_SCANCODE_J, true), bindings, dirty), "Captured key was not consumed");
    Check(bindings.keyboard[jump] == "J" && dirty && capture.phase == Phase::WaitingRelease, "Keyboard binding was not applied");
    Check(capture.Blocks(BindingDevice::Keyboard), "New key could activate launcher navigation");
    capture.Handle(Key(SDL_SCANCODE_J, false), bindings, dirty);
    Check(!capture.Busy() && !capture.Blocks(BindingDevice::Keyboard), "Key release did not finish capture");

    bindings = original; dirty = false;
    capture.Begin(jump, BindingDevice::Gamepad, 100);
    capture.Handle(Button(SDL_GAMEPAD_BUTTON_EAST, true), bindings, dirty);
    Check(capture.phase == Phase::Conflict && capture.conflict == yarn && !dirty && bindings == original,
          "Conflicting input changed the bindings");
    Check(capture.WaitingForRelease(), "Conflict did not shield its pressed input");
    capture.Handle(Button(SDL_GAMEPAD_BUTTON_EAST, false), bindings, dirty);
    Check(capture.phase == Phase::Conflict && !capture.WaitingForRelease(), "Conflict vanished before a decision");
    capture.Swap(bindings, dirty);
    Check(!capture.Busy() && dirty && bindings.gamepad[jump] == "east" && bindings.gamepad[yarn] == "south", "Conflict swap failed");

    bindings = original; dirty = false;
    capture.Begin(jump, BindingDevice::Gamepad, 100);
    capture.Handle(Key(SDL_SCANCODE_ESCAPE, true), bindings, dirty);
    Check(!capture.Busy() && !dirty && bindings == original, "Escape did not cancel gamepad capture");
    capture.Begin(jump, BindingDevice::Keyboard, 100);
    capture.Handle(Key(SDL_SCANCODE_ESCAPE, true), bindings, dirty);
    Check(!capture.Busy() && !dirty && bindings == original, "Escape did not cancel keyboard capture");
    capture.Begin(static_cast<size_t>(GameAction::Map), BindingDevice::Keyboard, 100);
    capture.BindEscape(bindings, dirty);
    Check(capture.phase == Phase::Conflict && !capture.WaitingForRelease(), "Explicit Escape binding did not detect its conflict");
    capture.Swap(bindings, dirty);
    Check(bindings.keyboard[static_cast<size_t>(GameAction::Map)] == "Escape" &&
          bindings.keyboard[static_cast<size_t>(GameAction::Pause)] == "Tab", "Explicit Escape assignment failed");
    bindings = original; dirty = false;
    capture.Begin(jump, BindingDevice::Keyboard, 100);
    capture.Tick(15100, true);
    Check(!capture.Busy() && !dirty, "Capture did not time out");
    capture.Begin(jump, BindingDevice::Keyboard, 100);
    capture.Tick(101, false);
    Check(!capture.Busy() && !dirty, "Focus loss did not cancel capture");
    capture.Begin(jump, BindingDevice::Gamepad, 100);
    SDL_Event axis{}; axis.type = SDL_EVENT_GAMEPAD_AXIS_MOTION; axis.gaxis.which = 123;
    axis.gaxis.axis = SDL_GAMEPAD_AXIS_LEFTX; axis.gaxis.value = 4000;
    capture.Handle(axis, bindings, dirty);
    Check(capture.phase == Phase::Listening && !dirty, "Stick noise was captured");
    axis.gaxis.value = -30000; capture.Handle(axis, bindings, dirty);
    Check(bindings.gamepad[jump] == "left_stick_left" && capture.phase == Phase::WaitingRelease, "Signed stick direction was not bound");
    axis.gaxis.value = 0; capture.Handle(axis, bindings, dirty);
    Check(!capture.Busy(), "Centered stick did not finish capture");
    capture.Clear(bindings, jump, BindingDevice::Keyboard, dirty);
    Check(bindings.keyboard[jump] == "disabled", "Clear did not unbind the keyboard slot");
    capture.Clear(bindings, jump, BindingDevice::Gamepad, dirty);
    Check(bindings.gamepad[jump] == "left_stick_left", "Clear removed the final required binding");

    // Use SDL's real virtual-device state to verify the initial held-axis guard.
    SDL_VirtualJoystickDesc descriptor{}; SDL_INIT_INTERFACE(&descriptor);
    descriptor.type = SDL_JOYSTICK_TYPE_GAMEPAD; descriptor.name = "Kirby capture fixture";
    descriptor.naxes = SDL_GAMEPAD_AXIS_COUNT; descriptor.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    descriptor.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    descriptor.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    const auto id = SDL_AttachVirtualJoystick(&descriptor);
    Check(id != 0, SDL_GetError());
    auto* joystick = SDL_OpenJoystick(id);
    auto* pad = SDL_OpenGamepad(id);
    Check(joystick && pad, "Virtual capture fixture did not open");
    Check(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, -30000), SDL_GetError());
    SDL_UpdateJoysticks();
    bindings = original; dirty = false;
    capture.Begin(jump, BindingDevice::Gamepad, 100);
    axis.gaxis.which = id; axis.gaxis.value = -31000;
    capture.Handle(axis, bindings, dirty);
    Check(capture.phase == Phase::Listening && !dirty, "A stick held before capture was bound");
    axis.gaxis.value = 0; capture.Handle(axis, bindings, dirty);
    axis.gaxis.value = -30000; capture.Handle(axis, bindings, dirty);
    Check(capture.phase == Phase::WaitingRelease && dirty, "Centered then moved stick did not bind");
    SDL_Event removed{}; removed.type = SDL_EVENT_GAMEPAD_REMOVED; removed.gdevice.which = id;
    capture.Handle(removed, bindings, dirty);
    Check(!capture.Busy() && bindings.gamepad[jump] == "left_stick_left", "Disconnect lost an applied binding or left capture stuck");
    capture.Begin(yarn, BindingDevice::Gamepad, 100);
    capture.Handle(removed, bindings, dirty);
    Check(!capture.Busy(), "Disconnect did not cancel pending capture");
    SDL_CloseGamepad(pad); SDL_CloseJoystick(joystick);
    Check(SDL_DetachVirtualJoystick(id), SDL_GetError());
    SDL_Quit();
    std::cout << checks << " binding capture checks passed\n";
}
