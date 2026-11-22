// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/graphics/button_prompts.h"
#include "kirby/app/events.h"
#include "controller_button_names.h"
#include "button_prompt_art.h"
#include <dolphin/pad.h>
#include <SDL3/SDL_keyboard.h>
#include <algorithm>
#include <span>
#include <utility>
#include <iostream>

namespace Kirby {
namespace {
SDL_Gamepad* PlayerOnePad() {
    const int index = PADGetIndexForPort(0);
    return index >= 0 ? PADGetSDLGamepadForIndex(index) : nullptr;
}

std::optional<ButtonIconStyle> PadStyle(SDL_Gamepad* pad) {
    switch (SDL_GetGamepadType(pad)) {
    case SDL_GAMEPAD_TYPE_XBOX360: case SDL_GAMEPAD_TYPE_XBOXONE: return ButtonIconStyle::Xbox;
    case SDL_GAMEPAD_TYPE_PS3: case SDL_GAMEPAD_TYPE_PS4: case SDL_GAMEPAD_TYPE_PS5: return ButtonIconStyle::PlayStation;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO: case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT: case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
        return ButtonIconStyle::Switch;
    default:
        switch (SDL_GetGamepadButtonLabel(pad, SDL_GAMEPAD_BUTTON_SOUTH)) {
        case SDL_GAMEPAD_BUTTON_LABEL_CROSS: return ButtonIconStyle::PlayStation;
        case SDL_GAMEPAD_BUTTON_LABEL_B: return ButtonIconStyle::Switch;
        default: return ButtonIconStyle::Xbox;
        }
    }
}

std::string KeyIcon(std::string_view binding) {
    const auto code = SDL_GetScancodeFromName(std::string(binding).c_str());
    if (code == SDL_SCANCODE_UNKNOWN) return {};
    const SDL_Keycode key = SDL_GetKeyFromScancode(code, SDL_KMOD_NONE, false);
    switch (key) {
    case SDLK_LCTRL: case SDLK_RCTRL: return "ctrl";
    case SDLK_LSHIFT: case SDLK_RSHIFT: return "shift";
    case SDLK_LALT: case SDLK_RALT: return "alt";
    case SDLK_RETURN: return "enter";
    case SDLK_PAGEUP: return "page_up";
    case SDLK_PAGEDOWN: return "page_down";
    case SDLK_LEFTBRACKET: return "left_bracket";
    case SDLK_RIGHTBRACKET: return "right_bracket";
    case SDLK_APOSTROPHE: return "apostrophe";
    case SDLK_GRAVE: return "grave";
    case SDLK_MINUS: return "minus";
    case SDLK_EQUALS: return "equals";
    case SDLK_BACKSLASH: return "backslash";
    case SDLK_SLASH: return "slash";
    case SDLK_COMMA: return "comma";
    case SDLK_PERIOD: return "period";
    case SDLK_SEMICOLON: return "semicolon";
    default: {
        std::string name = SDL_GetKeyName(key);
        for (char& ch : name) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
        return name;
    }
    }
}

struct Prompts {
    ControlBindings bindings;
    ButtonIconStyle preference;
    std::optional<ButtonIconStyle> active;
    SDL_JoystickID activePad{};
    bool receivedInput{};
    std::array<int, SDL_GAMEPAD_AXIS_COUNT> axisDirection{};
    std::array<SDL_Scancode, GameActions.size()> keys{};
    std::array<uint32_t, GameActions.size()> buttons{};
    std::array<std::string, GameActions.size()> appliedNames{};

    Prompts(const ControlBindings& controls, ButtonIconStyle style) : bindings(controls), preference(style) {
        for (size_t i = 0; i < GameActions.size(); ++i) {
            keys[i] = SDL_GetScancodeFromName(bindings.keyboard[i].c_str());
            buttons[i] = ControllerNames::FindNativeButton(bindings.gamepad[i])->nativeButton;
        }
    }
    void UsePad(SDL_Gamepad* pad) {
        active = PadStyle(pad);
        activePad = SDL_GetGamepadID(pad);
    }
    void DefaultDevice() {
        if (auto* pad = PlayerOnePad()) UsePad(pad);
        else { active = ButtonIconStyle::Keyboard; activePad = 0; }
    }
    void Note(const SDL_Event& event) {
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
            std::ranges::find(keys, event.key.scancode) != keys.end() && event.key.scancode != SDL_SCANCODE_UNKNOWN) {
            active = ButtonIconStyle::Keyboard;
            activePad = 0;
            receivedInput = true;
            return;
        }
        if (event.type != SDL_EVENT_GAMEPAD_BUTTON_DOWN && event.type != SDL_EVENT_GAMEPAD_AXIS_MOTION) return;
        auto* pad = PlayerOnePad();
        const bool axis = event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION;
        if (!pad || SDL_GetGamepadID(pad) != (axis ? event.gaxis.which : event.gbutton.which)) return;
        uint32_t button{};
        if (axis) {
            const unsigned id = event.gaxis.axis;
            if (id >= axisDirection.size()) return;
            const auto threshold = id >= SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? bindings.triggerActivation : bindings.stickActivation;
            const float value = event.gaxis.value / (event.gaxis.value < 0 ? 32768.f : 32767.f);
            const int direction = value >= threshold ? 1 : value <= -threshold ? -1 : 0;
            if (std::exchange(axisDirection[id], direction) == direction || direction == 0) return;
            button = PADEncodeAxisButton(id, direction < 0);
        } else button = event.gbutton.button;
        if (std::ranges::find(buttons, button) == buttons.end()) return;
        UsePad(pad);
        receivedInput = true;
    }

    void Apply() {
        const auto style = preference == ButtonIconStyle::Auto ? active : preference;
        std::array<std::string, GameActions.size()> names{};
        std::array<std::string, GameActions.size()> labels{};
        std::string family;
        bool standardDirections = true;
        if (style) {
            family = ButtonIconStyleNames[static_cast<size_t>(*style)];
            for (size_t i = 0; i < names.size(); ++i) {
                const bool keyboard = *style == ButtonIconStyle::Keyboard;
                const auto& binding = keyboard ? bindings.keyboard[i] : bindings.gamepad[i];
                const auto token = keyboard ? KeyIcon(binding) : binding;
                if (i <= static_cast<size_t>(GameAction::Right))
                    standardDirections &= binding == (keyboard ? GameActions[i].defaultKeyboard : GameActions[i].defaultGamepad);
                names[i] = family + "/" + token;
                labels[i] = keyboard ? (token.empty() ? binding : token) : token;
                if (binding == "disabled") labels[i] = "--";
                if (*style == ButtonIconStyle::PlayStation && token == "guide") labels[i] = "PS";
            }
        }
        if (names == appliedNames) return;
        PromptArt::Glyphs glyphs;
        for (size_t i = 0; i < names.size(); ++i)
            if (!names[i].empty()) glyphs[i] = PromptArt::Load(names[i], labels[i]);
        PromptArt::Image dpad;
        if (style && standardDirections)
            dpad = PromptArt::Load(family + (*style == ButtonIconStyle::Keyboard ? "/arrows" : "/dpad"), {});
        PromptArt::Apply(glyphs, dpad);
        appliedNames = std::move(names);
        std::cout << "[kirby] advance prompt: " << appliedNames[static_cast<size_t>(GameAction::Jump)]
                  << ", skip prompt: " << appliedNames[static_cast<size_t>(GameAction::Pause)] << '\n';
    }
    void Events(const AuroraEvent* events) {
        const auto previous = active;
        bool layoutChanged = false;
        for (auto* ev = events; ev && ev->type != AURORA_NONE; ++ev) {
            if (ev->type == AURORA_SDL_EVENT) {
                if (preference == ButtonIconStyle::Auto) Note(ev->sdl);
                layoutChanged |= ev->sdl.type == SDL_EVENT_KEYMAP_CHANGED;
            }
            if (ev->type == AURORA_CONTROLLER_REMOVED) {
                axisDirection.fill(0);
                if (ev->controller == activePad) receivedInput = false;
            }
        }
        if (preference == ButtonIconStyle::Auto && !receivedInput) DefaultDevice();
        if (active != previous || layoutChanged) Apply();
    }
};
std::optional<Prompts> prompts;
}

void StartButtonPrompts(const ControlBindings& bindings, ButtonIconStyle style) {
    PromptArt::Start();
    prompts.emplace(bindings, style);
    prompts->DefaultDevice();
    prompts->Apply();
    hostButtonPromptEvents = [](const AuroraEvent* events) { prompts->Events(events); };
}
void StopButtonPrompts() {
    hostButtonPromptEvents = nullptr;
    if (!prompts) return;
    PromptArt::Stop();
    prompts.reset();
}
}
