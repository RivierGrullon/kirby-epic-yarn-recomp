// SPDX-License-Identifier: GPL-3.0-only
#include "pages.h"
#include <SDL3/SDL.h>
#include <imgui.h>

namespace Launcher {
Action DrawControls(State&) {
    int count = 0;
    SDL_JoystickID* controllers = SDL_GetGamepads(&count);
    ImGui::Text("Controllers detected: %d", count);
    for (int i = 0; i < count; ++i) {
        const char* name = SDL_GetGamepadNameForID(controllers[i]);
        ImGui::TextWrapped("%s", name ? name : "Unnamed controller");
    }
    SDL_free(controllers);
    if (count == 0) ImGui::TextWrapped("Connect a controller to see it here.");
    ImGui::Spacing();
    ImGui::SeparatorText("Current gamepad bindings");
    if (ImGui::BeginTable("Bindings", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Gamepad"); ImGui::TableSetupColumn("Action"); ImGui::TableHeadersRow();
        constexpr std::array bindings{
            std::pair{"D-pad", "Move"}, std::pair{"South / Cross", "Confirm / Jump"},
            std::pair{"East / Circle", "Yarn"}, std::pair{"Start / Options", "Plus / Menu"},
            std::pair{"Back / Share", "Minus / Map"}};
        for (const auto& [button, action] : bindings) {
            ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextWrapped("%s", button);
            ImGui::TableNextColumn(); ImGui::TextWrapped("%s", action);
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    ImGui::TextWrapped("Custom bindings, keyboard configuration and motion settings are not available yet.");
    return Action::None;
}
} // namespace Launcher
