// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "kirby/settings.h"
#include "binding_capture.h"
#include "kirby/app/build_info.h"
#include "setup/job.h"
#include <filesystem>
#include <optional>
#include <string>

namespace Launcher {
// UI state only. The application owns the game process and folder dialog.
struct State {
    std::filesystem::path root, executable, settingsFile;
    Kirby::Settings config;
    bool selfTest = false;
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
};

enum class Action {
    None, Play, ChooseData, VerifyData, SaveSettings, ReloadSettings,
    OpenLogs, OpenSaves, OpenSettings, BuildGuide, ShowInstall, RefreshBuildInfo,
    ChooseImage, CancelSetup, OpenSetupLogs, BuildGame,
};
} // namespace Launcher
