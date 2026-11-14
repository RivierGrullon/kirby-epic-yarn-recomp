// SPDX-License-Identifier: GPL-3.0-only
#include "pages.h"
#include <imgui.h>

namespace Launcher {
Action DrawSaves(State& state) {
    const auto folder = state.root / "local/user/nand/title/00010000/524b3545/data";
    std::error_code ec;
    const bool found = std::filesystem::is_regular_file(folder / "FLF.bin", ec);
    ImGui::TextUnformatted(found ? "Game save found" : "No game save yet");
    ImGui::TextWrapped("%s", folder.c_str());
    ImGui::Spacing();
    const bool open = ImGui::Button("Open Saves");
    ImGui::Spacing();
    ImGui::TextWrapped("The game manages its own saves. Choosing another DATA folder does not move them.");
    ImGui::TextWrapped("Backup, restore and save import from this window are not available yet.");
    return open ? Action::OpenSaves : Action::None;
}
} // namespace Launcher
