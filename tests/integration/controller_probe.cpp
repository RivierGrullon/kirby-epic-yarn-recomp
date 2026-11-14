// SPDX-License-Identifier: GPL-3.0-only
// SDL virtual-device coverage for the native controller event path.
#include "abi_bridge.h"
#include "generated/KirbyBootConfig.h"
#include "kirby/input/wpad.h"
#include "kirby/input/bindings.h"
#include "controller_button_names.h"
#include <aurora/wpad.h>
#include <aurora/input.hpp>
#include <aurora/pad.hpp>
#include <aurora/binding.hpp>
#include <dolphin/pad.h>
#include "gamepad.hpp"
#include "input/sdl_input.hpp"
#include "internal.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <chrono>
#include <array>
#include <optional>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>
#include <set>

namespace Kirby {
void ProbeSdlController(CpuContext& cpu, bool monitorPhysical, const ControlBindings* configured) {
    // Run from a disposable working directory: Aurora persists port assignments.
    auto check = [](bool value, const char* reason) { if (!value) throw std::runtime_error(reason); };
    const auto userPath = std::filesystem::absolute("local/user/aurora").string();
    std::filesystem::create_directories(userPath);
    struct ConfigRestore {
        AuroraConfig previous = aurora::g_config;
        ~ConfigRestore() { aurora::g_config = previous; }
    } restore;
    // Mapping defaults are loaded only when Aurora has a user path, as in --video.
    aurora::g_config.userPath = userPath.c_str();
    InvokeIndirectCpu(KirbyBootConfig::InitRegisters, &cpu);
    InvokeIndirectCpu(KirbyBootConfig::WPADInit, &cpu);
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    aurora::gamepad::initialize();
    PADClearAllVirtualStatus();
    cpu.msr |= 0x8000;
    // Use the same SDL dispatch and hotplug handlers as Aurora's window pump.
    // No window/GPU is initialized, so unrelated UI/window events are ignored.
    auto pump = [&] {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
                const auto id = aurora::gamepad::add_controller(event.gdevice.which);
                if (id && id != SDL_JoystickID(-1)) aurora::input::sdl::gamepad_added(id);
            } else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
                aurora::input::sdl::gamepad_removed(event.gdevice.which);
                aurora::gamepad::remove_controller(event.gdevice.which);
            } else if (aurora::input::sdl::is_routed_event(event)) {
                aurora::input::sdl::dispatch(event);
            }
        }
        PollWpad(cpu, true);
    };
    pump();
    if (monitorPhysical) {
        struct State {
            uint32_t raw = 0;
            uint16_t pad = 0, guest = 0;
            int8_t error = 0;
            bool blocked = false, captured = false;
            bool operator==(const State&) const = default;
        };
        std::array<std::optional<State>, PAD_CHANMAX> previous;
        unsigned transitions = 0;
        std::cout << "[kirby] Physical controller monitor: 30 seconds; no game window or virtual input\n"
                  << "[kirby] Press and release left, right, up, down, south and east in order\n" << std::flush;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (std::chrono::steady_clock::now() < deadline) {
            pump();
            PADStatus pads[PAD_CHANMAX]{};
            PADRead(pads);
            for (uint32_t port = 0; port < PAD_CHANMAX; ++port) {
                constexpr uint32_t output = 0x81000000;
                cpu.gpr[3] = port; cpu.gpr[4] = output;
                InvokeIndirectCpu(KirbyBootConfig::WPADRead, &cpu);
                State state{.pad = pads[port].button, .guest = Memory::Read16(output),
                    .error = int8_t(Memory::Read8(output+0x29)), .blocked = bool(PADIsInputBlocked())};
                const auto index = PADGetIndexForPort(port);
                if (auto* gamepad = index < 0 ? nullptr : PADGetSDLGamepadForIndex(index)) {
                    for (unsigned button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; ++button)
                        if (SDL_GetGamepadButton(gamepad, SDL_GamepadButton(button))) state.raw |= 1u << button;
                    state.captured = aurora::input::captured_above(
                        aurora::input::source_for_gamepad(SDL_GetGamepadID(gamepad)), aurora::input::kGameLayerPriority);
                }
                if (previous[port] && *previous[port] == state) continue;
                if (previous[port] && (previous[port]->raw != state.raw || previous[port]->guest != state.guest))
                    ++transitions;
                previous[port] = state;
                std::cout << "[kirby] physical input port=" << port << " SDL=0x" << std::hex << state.raw
                          << " PAD=0x" << state.pad << " guest=0x" << state.guest << std::dec
                          << " error=" << int(state.error) << " blocked=" << state.blocked
                          << " captured=" << state.captured << '\n' << std::flush;
            }
            SDL_Delay(5);
        }
        std::cout << "[kirby] Physical monitor finished; button transitions=" << transitions
                  << "; physical gameplay verification remains pending\n";
        aurora::gamepad::shutdown();
        return;
    }
    struct Profile {
        const char* name;
        uint16_t vendor, product;
        SDL_GamepadType type;
    };
    for (const auto& profile : std::array{
            Profile{"Kirby standard probe", 0, 0, SDL_GAMEPAD_TYPE_STANDARD},
            Profile{"Kirby PS5 probe", 0x054c, 0x0ce6, SDL_GAMEPAD_TYPE_PS5},
            Profile{"Kirby Xbox 360 probe", 0x045e, 0x028e, SDL_GAMEPAD_TYPE_XBOX360},
            Profile{"Kirby Xbox One probe", 0x045e, 0x02d1, SDL_GAMEPAD_TYPE_XBOXONE}}) {
        SDL_VirtualJoystickDesc descriptor{};
        SDL_INIT_INTERFACE(&descriptor);
        descriptor.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        descriptor.naxes = SDL_GAMEPAD_AXIS_COUNT;
        descriptor.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        descriptor.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
        descriptor.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
        descriptor.name = profile.name;
        descriptor.vendor_id = profile.vendor;
        descriptor.product_id = profile.product;
        struct Device {
            SDL_JoystickID id = 0;
            SDL_Joystick* joystick = nullptr;
            ~Device() {
                if (joystick) SDL_CloseJoystick(joystick);
                if (id) SDL_DetachVirtualJoystick(id);
            }
        } device;
        device.id = SDL_AttachVirtualJoystick(&descriptor);
        check(device.id != 0, SDL_GetError());
        device.joystick = SDL_OpenJoystick(device.id);
        check(device.joystick != nullptr, SDL_GetError());
        pump();
        check(SDL_GetGamepadTypeForID(device.id) == profile.type,
              "SDL virtual controller did not select the requested device preset");
        std::cout << "[kirby] SDL preset: " << profile.name << " type=" << profile.type << '\n';
        bool assigned = false;
        for (uint32_t i = 0; i < PADCount(); ++i) {
            if (SDL_GetGamepadID(PADGetSDLGamepadForIndex(i)) == device.id) {
                PADSetPortForIndex(i, 0);
                assigned = true;
                break;
            }
        }
        check(assigned, "SDL virtual controller did not enter the Aurora device list");
        constexpr uint32_t output = 0x81000000;
        auto report = [&] {
            pump();
            cpu.gpr[3] = 0; cpu.gpr[4] = output;
            InvokeIndirectCpu(KirbyBootConfig::WPADRead, &cpu);
            check(Memory::Read8(output+0x29) == 0, "SDL controller report is disconnected");
            return Memory::Read16(output);
        };
        check(report() == 0, "SDL virtual controller did not start neutral");
        if (configured) {
            for (unsigned index = 0; index < SDL_GAMEPAD_BUTTON_COUNT; ++index) {
                const auto button = static_cast<SDL_GamepadButton>(index);
                check(SDL_SetJoystickVirtualButton(device.joystick, index, true), SDL_GetError());
                const auto value = report();
                std::cout << "[kirby:binding] " << profile.name << "|gamepad|"
                          << ControllerNames::NativeButtonForValue(button).configName << '|' << std::dec << value << '\n';
                check(SDL_SetJoystickVirtualButton(device.joystick, index, false), SDL_GetError());
                // SDL intentionally keeps Guide pressed for at least 250 ms.
                const auto deadline = SDL_GetTicks() + 500;
                auto released = report();
                while (released && SDL_GetTicks() < deadline) { SDL_Delay(5); released = report(); }
                check(released == 0, "Configured gamepad binding did not release");
            }
            for (unsigned axis = 0; axis < SDL_GAMEPAD_AXIS_COUNT; ++axis) {
                const bool trigger = axis >= SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
                for (const bool negative : {true, false}) {
                    if (trigger && negative) continue;
                    check(SDL_SetJoystickVirtualAxis(device.joystick, axis, negative ? -30000 : 30000), SDL_GetError());
                    const auto value = report();
                    std::cout << "[kirby:binding] " << profile.name << "|axis|"
                              << ControllerNames::NativeButtonForValue(PADEncodeAxisButton(axis, negative)).configName
                              << '|' << std::dec << value << '\n';
                    // Virtual joystick triggers span -32768..32767; SDL's
                    // gamepad layer exposes their resting value as zero.
                    check(SDL_SetJoystickVirtualAxis(device.joystick, axis, trigger ? -32768 : 0), SDL_GetError());
                    check(report() == 0, "Configured axis binding did not release");
                }
            }
            std::set<std::string> keys{"Space", "J"};
            for (const auto& key : configured->keyboard) if (key != "disabled") keys.insert(key);
            for (const auto& key : keys) {
                SDL_Event event{};
                event.type = SDL_EVENT_KEY_DOWN;
                event.key.scancode = SDL_GetScancodeFromName(key.c_str()); event.key.down = true;
                check(SDL_PushEvent(&event), SDL_GetError());
                const auto value = report();
                std::cout << "[kirby:binding] " << profile.name << "|keyboard|" << key << '|' << std::dec << value << '\n';
                event.type = SDL_EVENT_KEY_UP; event.key.down = false;
                check(SDL_PushEvent(&event), SDL_GetError());
                check(report() == 0, "Configured keyboard binding did not release");
            }
            continue;
        }
        for (auto [button, remote] : {
                std::pair{SDL_GAMEPAD_BUTTON_DPAD_LEFT, 0x0008},
                {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, 0x0004},
                {SDL_GAMEPAD_BUTTON_DPAD_UP, 0x0002},
                {SDL_GAMEPAD_BUTTON_DPAD_DOWN, 0x0001},
                {SDL_GAMEPAD_BUTTON_SOUTH, 0x0100}, {SDL_GAMEPAD_BUTTON_EAST, 0x0200},
                {SDL_GAMEPAD_BUTTON_WEST, 0x0400}, {SDL_GAMEPAD_BUTTON_NORTH, 0x0800},
                {SDL_GAMEPAD_BUTTON_START, 0x0010}, {SDL_GAMEPAD_BUTTON_BACK, 0x1000}}) {
            check(SDL_SetJoystickVirtualButton(device.joystick, button, true), SDL_GetError());
            const auto actual = report();
            if (actual != remote) {
                PADStatus pads[PAD_CHANMAX]{};
                PADRead(pads);
                std::cerr << "[kirby] SDL button=" << SDL_GetGamepadStringForButton(button)
                          << " raw=" << SDL_GetJoystickButton(device.joystick, button)
                          << " PAD=0x" << std::hex << pads[0].button
                          << " guest=0x" << actual << " expected=0x" << remote << std::dec << '\n';
                const auto index = PADGetIndexForPort(0);
                auto* gamepad = PADGetSDLGamepadForIndex(index);
                std::cerr << "[kirby] gamepad=" << SDL_GetGamepadButton(gamepad, button)
                          << " source=" << aurora::input::source_for_gamepad(device.id)
                          << " routed=" << aurora::input::raw_button_pressed(
                                 aurora::input::source_for_gamepad(device.id), button)
                          << " blocked=" << PADIsInputBlocked() << '\n';
                const auto bindings = aurora::pad::binding_set(0);
                std::cerr << "[kirby] bindings=" << bindings->bindings.size()
                          << " references=" << bindings->references(aurora::input::source_for_gamepad(device.id))
                          << " captured=" << aurora::input::captured_above(
                                 aurora::input::source_for_gamepad(device.id), aurora::input::kGameLayerPriority) << '\n';
                throw std::runtime_error("SDL button event differs from the horizontal guest layout");
            }
            check(SDL_SetJoystickVirtualButton(device.joystick, button, false), SDL_GetError());
            check(report() == 0, "SDL button release left a held guest button");
        }
        // Replace original physical bindings, including the extended Back key.
        // Adding an alternative alone would leave both old and new actions on.
        using namespace aurora::binding;
        const auto& controls = aurora::pad::controls();
        const auto back = aurora::pad::control_for_ext_button(PAD_BUTTON_BACK);
        std::vector<Binding> remapped;
        for (auto [button, target] : {
                std::pair{SDL_GAMEPAD_BUTTON_EAST, controls.a},
                {SDL_GAMEPAD_BUTTON_SOUTH, controls.b},
                {SDL_GAMEPAD_BUTTON_BACK, controls.start},
                {SDL_GAMEPAD_BUTTON_START, back}}) {
            Binding binding;
            binding.input.control = PhysicalInput::GamepadButton{button};
            binding.target = target;
            remapped.push_back(binding);
        }
        Binding keyboardJump;
        keyboardJump.input.control = PhysicalInput::Key{SDL_SCANCODE_SPACE};
        keyboardJump.target = controls.a;
        remapped.push_back(keyboardJump);
        aurora::pad::set_action_bindings(0, remapped, {controls.a, controls.b, controls.start, back}, true);
        check(report() == 0, "Binding replacement introduced a held button");
        for (auto [button, expected] : {
                std::pair{SDL_GAMEPAD_BUTTON_EAST, 0x0100},
                {SDL_GAMEPAD_BUTTON_SOUTH, 0x0200},
                {SDL_GAMEPAD_BUTTON_BACK, 0x0010},
                {SDL_GAMEPAD_BUTTON_START, 0x1000}}) {
            check(SDL_SetJoystickVirtualButton(device.joystick, button, true), SDL_GetError());
            const auto actual = report();
            if (actual != expected)
                std::cerr << "[kirby] remap mismatch: button=" << SDL_GetGamepadStringForButton(button)
                          << " guest=0x" << std::hex << actual << " expected=0x" << expected << std::dec << '\n';
            check(actual == expected, "Remapped button retained an original physical action");
            check(SDL_SetJoystickVirtualButton(device.joystick, button, false), SDL_GetError());
            check(report() == 0, "Remapped button remained held after release");
        }
        auto key = [&](bool pressed) {
            SDL_Event event{};
            event.type = pressed ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            event.key.scancode = SDL_SCANCODE_SPACE;
            event.key.key = SDLK_SPACE;
            event.key.down = pressed;
            check(SDL_PushEvent(&event), SDL_GetError());
        };
        key(true);
        check(report() == 0x0100, "Remapped keyboard input differs from the guest jump bit");
        check(SDL_SetJoystickVirtualButton(device.joystick, SDL_GAMEPAD_BUTTON_SOUTH, true), SDL_GetError());
        check(report() == 0x0300, "Keyboard and controller cannot contribute simultaneously");
        key(false);
        check(report() == 0x0200, "Releasing keyboard input also released the controller");
        check(SDL_SetJoystickVirtualButton(device.joystick, SDL_GAMEPAD_BUTTON_SOUTH, false), SDL_GetError());
        check(report() == 0, "Mixed keyboard/controller input did not release");
        PADStatus virtualPad{};
        virtualPad.button = PAD_BUTTON_A;
        PADSetVirtualStatus(0, &virtualPad);
        check(report() == 0x0100, "Replacing physical bindings discarded virtual input");
        PADClearVirtualStatus(0);
        check(report() == 0, "Virtual producer was not released");
        check(SDL_SetJoystickVirtualButton(device.joystick, SDL_GAMEPAD_BUTTON_EAST, true), SDL_GetError());
        check(report() == 0x0100, "Remapped input was not held before blocking");
        PADBlockInput(true);
        check(report() == 0, "Blocked remapped input reached the guest");
        PADBlockInput(false);
        check(report() == 0, "Unblocking replayed held remapped input");
        check(SDL_SetJoystickVirtualButton(device.joystick, SDL_GAMEPAD_BUTTON_EAST, false), SDL_GetError());
        check(report() == 0, "Blocked input was not released");
        check(SDL_SetJoystickVirtualButton(device.joystick, SDL_GAMEPAD_BUTTON_EAST, true), SDL_GetError());
        check(report() == 0x0100, "Fresh remapped input failed after unblocking");
        check(SDL_SetJoystickVirtualButton(device.joystick, SDL_GAMEPAD_BUTTON_EAST, false), SDL_GetError());
        check(report() == 0, "Fresh remapped input did not release");
        aurora::pad::set_action_bindings(0, {});
        check(report() == 0, "Restoring defaults introduced input");
        key(true);
        check(report() == 0, "Restoring defaults retained custom keyboard input");
        key(false);
        check(report() == 0, "Removed keyboard binding did not remain neutral");
        check(SDL_SetJoystickVirtualButton(device.joystick, SDL_GAMEPAD_BUTTON_EAST, true), SDL_GetError());
        check(report() == 0x0200, "Restoring base bindings left the override active");
        check(SDL_SetJoystickVirtualButton(device.joystick, SDL_GAMEPAD_BUTTON_EAST, false), SDL_GetError());
        check(report() == 0, "Restored binding did not release");
        std::cout << "[kirby] SDL binding replacement verified: " << profile.name << '\n';
        check(SDL_SetJoystickVirtualButton(device.joystick, SDL_GAMEPAD_BUTTON_SOUTH, true), SDL_GetError());
        check(report() == 0x0100, "SDL held button was not sampled before disconnect");
        SDL_CloseJoystick(std::exchange(device.joystick, nullptr));
        check(SDL_DetachVirtualJoystick(device.id), SDL_GetError());
        device.id = 0;
        pump();
        cpu.gpr[3] = 0; cpu.gpr[4] = output;
        InvokeIndirectCpu(KirbyBootConfig::WPADRead, &cpu);
        check(Memory::Read16(output) == 0 && Memory::Read8(output+0x29) == 255,
              "SDL disconnect left stale buttons or success status");
        aurora::pad::set_action_bindings(0, remapped, {controls.a, controls.b, controls.start, back}, true);
        check(report() == 0, "Keyboard-enabled port was not connected without a controller");
        key(true);
        check(report() == 0x0100, "Keyboard-only input failed without a physical controller");
        key(false);
        check(report() == 0, "Keyboard-only input did not release");
        // A new SDL instance must resolve the same per-port bindings, rather
        // than retaining source IDs from the disconnected controller.
        device.id = SDL_AttachVirtualJoystick(&descriptor);
        check(device.id != 0, SDL_GetError());
        device.joystick = SDL_OpenJoystick(device.id);
        check(device.joystick != nullptr, SDL_GetError());
        pump();
        assigned = false;
        for (uint32_t i = 0; i < PADCount(); ++i) {
            if (SDL_GetGamepadID(PADGetSDLGamepadForIndex(i)) == device.id) {
                PADSetPortForIndex(i, 0); assigned = true; break;
            }
        }
        check(assigned, "Reconnected device did not enter the controller list");
        check(report() == 0, "Reconnect replayed old input");
        check(SDL_SetJoystickVirtualButton(device.joystick, SDL_GAMEPAD_BUTTON_SOUTH, true), SDL_GetError());
        check(report() == 0x0200, "Reconnected controller lost its binding replacement");
        SDL_CloseJoystick(std::exchange(device.joystick, nullptr));
        check(SDL_DetachVirtualJoystick(device.id), SDL_GetError());
        device.id = 0;
        check(report() == 0, "Disconnect left a remapped button held on the keyboard-enabled port");
        aurora::pad::set_action_bindings(0, {});
        pump();
        cpu.gpr[3] = 0; cpu.gpr[4] = output;
        InvokeIndirectCpu(KirbyBootConfig::WPADRead, &cpu);
        check(Memory::Read8(output+0x29) == 255, "Reset did not restore disconnected port status");
        std::cout << "[kirby] SDL mixed input and reconnect verified: " << profile.name << '\n';
        std::cout << "[kirby] SDL preset verified: " << profile.name << '\n';
    }
    aurora::gamepad::shutdown();
    std::cout << (configured ? "[kirby] configured controls sampled through guest WPAD reports\n" :
                              "[kirby] SDL controller events: directions, face buttons, releases and disconnect verified\n");
}
}
