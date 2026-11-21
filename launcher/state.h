// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "kirby/settings.h"
#include "binding_capture.h"
#include "kirby/app/build_info.h"
#include "setup/job.h"
#include "motion_preview.h"
#include "fabric.h"
#include <filesystem>
#include <optional>
#include <string>

namespace Launcher {
enum class Action {
    None, Play, ChooseData, VerifyData, SaveSettings, ReloadSettings,
    OpenLogs, OpenSaves, OpenSettings, BuildGuide, ShowInstall, RefreshBuildInfo,
    ChooseImage, CancelSetup, OpenSetupLogs, OpenSetup, BuildGame,
    ImportDolphinSave, ImportSaveBin, ExportSaveFolder, ExportSaveBin,
    RestoreBackup, DeleteBackup,
};

// UI state only. The application owns the game process and folder dialog.
struct State {
    std::filesystem::path root, executable, settingsFile;
    Kirby::Settings config;
    bool selfTest = false;
    bool portable = false;
    bool dataVerified = false;
    bool settingsDirty = false;
    bool gameRunning = false;
    std::string dataStatus = "Select your extracted DATA folder";
    std::string gameStatus;
    std::string settingsError;
    BindingCapture bindingCapture;
    Kirby::BuildInfo launcherBuild;
    std::optional<Kirby::BuildInfo> runtimeBuild;
    bool checkingBuild = false;
    std::string buildStatus = "Native build has not been checked";
    Setup::View setup;
    bool choosingSource=false;
    bool closing=false;
    std::string installationId,installationError;
    std::string saveImportStatus;
    std::string pendingBackup;         // Backup directory name for restore/delete.
    bool confirmReplace = false;       // Show the "replace current save?" confirmation.
    Action confirmAction = Action::None;
    MotionPreview motionPreview;
    Fabric fabric;
    bool languageSaveError = false;
};
} // namespace Launcher
