// SPDX-License-Identifier: GPL-3.0-only
#include "pages.h"
#include <imgui.h>

namespace Launcher {
Action DrawDisplay(State& state) {
    ImGui::BeginDisabled(state.selfTest || !state.settingsError.empty());
    state.settingsDirty |= ImGui::Checkbox("VSync", &state.config.vsync);
    ImGui::TextWrapped("Synchronize presentation with the display. Turning this off can reduce stalls on some drivers.");
    ImGui::Spacing();
    state.settingsDirty |= ImGui::Checkbox("Experimental performance mode", &state.config.gpuResidentCopies);
    ImGui::TextWrapped("Reduces GPU copy waits and repeated texture processing. Compatibility testing is ongoing.");
    ImGui::Spacing();
    ImGui::SeparatorText("Image quality");
    int scale = int(std::ranges::find(Kirby::ResolutionScales, state.config.resolutionScale) - Kirby::ResolutionScales.begin());
    if (ImGui::Combo("Internal resolution", &scale,
                     "1x (native)\0" "2x\0" "3x\0" "4x\0" "5x\0" "6x\0" "8x\0")) {
        state.config.resolutionScale = Kirby::ResolutionScales[scale];
        state.settingsDirty = true;
    }
    ImGui::TextWrapped("Renders geometry at a higher resolution for sharper edges. Higher multipliers use more GPU time and memory; original texture detail is unchanged.");
    int aa = (state.config.msaaSamples == 4 ? 2 : 0) + (state.config.fxaa ? 1 : 0);
    if (ImGui::Combo("Anti-aliasing", &aa, "Off\0" "FXAA\0" "4x MSAA\0" "4x MSAA + FXAA\0")) {
        state.config.msaaSamples = aa >= 2 ? 4 : 1;
        state.config.fxaa = (aa & 1) != 0;
        state.settingsDirty = true;
    }
    ImGui::TextWrapped("Smooths polygon edges. Can be combined with internal resolution scaling. Changes apply on the next game launch.");
    ImGui::TextWrapped("FXAA smooths edges in the final image at a low GPU cost, but can soften fine details. MSAA smooths geometry edges; the combined mode applies both.");
    ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::SeparatorText("Resolution and frame rate");
    ImGui::TextWrapped("16:9 widescreen. The game opens at 960 x 540 and keeps its widescreen proportions when resized, adding black bars as needed.");
    ImGui::TextWrapped("Window size does not change the selected internal resolution. A validated 30/60 FPS selector is not available yet.");
    return Action::None;
}
} // namespace Launcher
