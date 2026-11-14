// SPDX-License-Identifier: GPL-3.0-only
#include "input_focus.h"
#include <imgui.h>
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks;
ImVec2 button;
bool focusLostDuringFrame;
int textCountDuringFrame;
void Check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
bool Frame(bool focused) {
    Launcher::SynchronizeInputFocus(focused);
    ImGui::NewFrame();
    focusLostDuringFrame = ImGui::GetIO().AppFocusLost;
    textCountDuringFrame = ImGui::GetIO().InputQueueCharacters.Size;
    ImGui::SetNextWindowPos({0, 0}); ImGui::SetNextWindowSize({400, 300});
    ImGui::Begin("Input test", nullptr, ImGuiWindowFlags_NoDecoration);
    const bool activated = ImGui::Button("Play", {150, 40});
    const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    button = {(a.x+b.x)/2, (a.y+b.y)/2};
    ImGui::End(); ImGui::Render();
    return activated;
}
}

int main() {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {640, 520}; io.DeltaTime = 1.f/60;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    unsigned char* pixels; int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    Frame(true); Frame(true);

    // A window can start without focus, with no focus-loss event to consume.
    // The backend still polls global gamepad state in that case.
    io.AddKeyEvent(ImGuiKey_GamepadFaceDown, true);
    io.AddKeyEvent(ImGuiKey_GamepadDpadDown, true);
    io.AddKeyEvent(ImGuiKey_Enter, true);
    io.AddInputCharacter('x');
    io.AddMousePosEvent(button.x, button.y); io.AddMouseButtonEvent(0, true);
    Check(!Frame(false), "Background input activated a button");
    Check(!ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown), "Background gamepad activation reached the UI");
    Check(!ImGui::IsKeyDown(ImGuiKey_GamepadDpadDown), "Background gamepad navigation reached the UI");
    Check(!ImGui::IsKeyDown(ImGuiKey_Enter), "Background keyboard activation reached the UI");
    Check(!io.MouseDown[0] && textCountDuringFrame == 0, "Background pointer/text input reached the UI");
    Check(focusLostDuringFrame, "Background focus state was not reported");
    for (int i = 0; i < 3; ++i) {
        io.AddKeyEvent(ImGuiKey_GamepadFaceDown, true);
        Check(!Frame(false) && !ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown),
              "Repeated background polling reached the UI");
    }

    Check(!Frame(true), "Input was replayed on focus regain");
    Check(!focusLostDuringFrame && !ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown), "Focus regain retained a stale key");

    io.AddKeyEvent(ImGuiKey_GamepadDpadDown, true);
    Frame(true);
    Check(ImGui::IsKeyDown(ImGuiKey_GamepadDpadDown), "Foreground gamepad navigation was discarded");
    Frame(false);
    Check(!ImGui::IsKeyDown(ImGuiKey_GamepadDpadDown), "Held navigation survived focus loss");

    // A press interrupted by focus loss must not become a click on return.
    Frame(true);
    io.AddMousePosEvent(button.x, button.y); io.AddMouseButtonEvent(0, true);
    Check(!Frame(true), "Button activated before release");
    Check(!Frame(false), "Focus loss synthesized a click");
    io.AddMouseButtonEvent(0, false);
    Check(!Frame(true), "Old mouse release activated a button after focus regain");

    // Clearing background input must still permit an ordinary subsequent click.
    io.AddMousePosEvent(button.x, button.y); Frame(true);
    io.AddMouseButtonEvent(0, true); Frame(true);
    io.AddMouseButtonEvent(0, false);
    Check(Frame(true), "Foreground click no longer works");
    ImGui::DestroyContext();
    std::cout << checks << " launcher input-focus checks passed\n";
}
