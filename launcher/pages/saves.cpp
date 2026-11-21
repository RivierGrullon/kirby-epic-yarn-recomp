// SPDX-License-Identifier: GPL-3.0-only
#include "pages.h"
#include "../platform.h"
#include "../save_manager.h"
#include <imgui.h>

namespace Launcher {
Action DrawSaves(State& state) {
    const auto folder = SaveDataDirectory(state.root);
    std::error_code ec;
    const bool found = std::filesystem::is_regular_file(folder / "FLF.bin", ec);
    ImGui::TextUnformatted(found ? Tr("Game save found") : Tr("No game save yet"));
    ImGui::TextWrapped("%s", Utf8(folder).c_str());
    ImGui::Spacing();

    Action result = Action::None;
    if (ImGui::Button(Tr("Open Saves"))) result = Action::OpenSaves;
    ImGui::SameLine();
    if (ImGui::Button(Tr("Import Dolphin save"))) { state.confirmReplace=true; state.confirmAction=Action::ImportDolphinSave; }
    ImGui::SameLine();
    if (ImGui::Button(Tr("Import save (.bin)"))) { state.confirmReplace=true; state.confirmAction=Action::ImportSaveBin; }
    ImGui::Spacing();
    if (ImGui::Button(Tr("Export save (.bin)"))) result = Action::ExportSaveBin;
    ImGui::SameLine();
    if (ImGui::Button(Tr("Export save folder"))) result = Action::ExportSaveFolder;

    ImGui::Spacing();
    ImGui::TextWrapped("%s", Tr("The game manages its own saves. Choosing another DATA folder does not move them."));
    ImGui::TextWrapped("%s", Tr("Select the Dolphin Wii folder, the RK5E01 title data folder, or a Dolphin data.bin save. The current save is backed up before replacement."));
    if (!state.saveImportStatus.empty()) ImGui::TextWrapped("%s", state.saveImportStatus.c_str());

    ImGui::Spacing();
    ImGui::SeparatorText(Tr("Backups"));
    const auto backups = ListBackups(state.root);
    if (backups.empty()) {
        ImGui::TextWrapped("%s", Tr("No backups yet. Imports and restores keep a copy of the previous save here."));
    } else {
        for (const auto& backup : backups) {
            ImGui::TextUnformatted(backup.timestamp.c_str());
            ImGui::SameLine();
            ImGui::PushID(backup.name.c_str());
            if (ImGui::SmallButton(Tr("Restore"))) {
                state.pendingBackup = backup.name;
                state.confirmReplace = true;
                state.confirmAction = Action::RestoreBackup;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(Tr("Delete"))) {
                state.pendingBackup = backup.name;
                result = Action::DeleteBackup;
            }
            ImGui::PopID();
        }
    }

    if (state.confirmReplace) ImGui::OpenPopup("Confirm replace save");
    if (ImGui::BeginPopupModal("Confirm replace save", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", Tr("This will replace your current save. A backup of the current save will be made first."));
        ImGui::Spacing();
        if (ImGui::Button(Tr("Replace"))) {
            result = state.confirmAction;
            state.confirmReplace = false;
            state.confirmAction = Action::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(Tr("Cancel"))) {
            state.confirmReplace = false;
            state.confirmAction = Action::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    return result;
}
} // namespace Launcher
