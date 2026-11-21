// SPDX-License-Identifier: GPL-3.0-only
#include "pages.h"
#include <imgui.h>

namespace Launcher {
Action DrawDisplay(State& state) {
    ImGui::BeginDisabled(state.selfTest || !state.settingsError.empty());
    constexpr std::array ids{Kirby::Settings::English, Kirby::Settings::French, Kirby::Settings::Spanish};
    const char* languages[]{"English", "Français", "Español"};
    int language = static_cast<int>(std::ranges::find(ids, state.config.gameLanguage.value_or(Kirby::Settings::English)) - ids.begin());
    if (ImGui::Combo(Tr("Game language"), &language, languages, IM_ARRAYSIZE(languages))) {
        state.config.gameLanguage = ids[language];
        state.settingsDirty = true;
    }
    state.settingsDirty |= ImGui::Checkbox(Tr("Start in fullscreen"), &state.config.fullscreen);
    ImGui::TextWrapped("%s", Tr("Open the game in borderless fullscreen on the next launch. Press Alt+Enter while playing to switch between fullscreen and windowed mode for that session."));
    ImGui::Spacing();
    state.settingsDirty |= ImGui::Checkbox(Tr("VSync"), &state.config.vsync);
    ImGui::TextWrapped("%s", Tr("Synchronize presentation with the display. Turning this off can reduce stalls on some drivers."));
    ImGui::Spacing();
    state.settingsDirty |= ImGui::Checkbox(Tr("Experimental performance mode"), &state.config.gpuResidentCopies);
    ImGui::TextWrapped("%s", Tr("Reduces GPU copy waits and repeated texture processing. Compatibility testing is ongoing."));
    ImGui::Spacing();
    ImGui::SeparatorText(Tr("Image quality"));
    int scale = int(std::ranges::find(Kirby::ResolutionScales, state.config.resolutionScale) - Kirby::ResolutionScales.begin());
    const char* scales[]{Tr("1x (native)"), "2x", "3x", "4x", "5x", "6x", "8x"};
    if (ImGui::Combo(Tr("Internal resolution"), &scale, scales, IM_ARRAYSIZE(scales))) {
        state.config.resolutionScale = Kirby::ResolutionScales[scale];
        state.settingsDirty = true;
    }
    ImGui::TextWrapped("%s", Tr("Renders geometry at a higher resolution for sharper edges. Higher multipliers use more GPU time and memory; original texture detail is unchanged."));
    int aa = (state.config.msaaSamples == 4 ? 2 : 0) + (state.config.fxaa ? 1 : 0);
    const char* modes[]{Tr("Off"), "FXAA", "4x MSAA", "4x MSAA + FXAA"};
    if (ImGui::Combo(Tr("Anti-aliasing"), &aa, modes, IM_ARRAYSIZE(modes))) {
        state.config.msaaSamples = aa >= 2 ? 4 : 1;
        state.config.fxaa = (aa & 1) != 0;
        state.settingsDirty = true;
    }
    ImGui::TextWrapped("%s", Tr("Smooths polygon edges. Can be combined with internal resolution scaling. Changes apply on the next game launch."));
    ImGui::TextWrapped("%s", Tr("FXAA smooths edges in the final image at a low GPU cost, but can soften fine details. MSAA smooths geometry edges; the combined mode applies both."));
    ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::SeparatorText(Tr("Resolution and frame rate"));
    ImGui::TextWrapped("%s", Tr("16:9 widescreen. Windowed mode opens at 960 x 540. Both modes keep widescreen proportions, adding black bars as needed."));
    ImGui::TextWrapped("%s", Tr("Window size does not change the selected internal resolution. A validated 30/60 FPS selector is not available yet."));
    return Action::None;
}
} // namespace Launcher
