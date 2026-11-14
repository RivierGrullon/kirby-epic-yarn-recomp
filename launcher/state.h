// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "kirby/settings.h"
#include <filesystem>
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
};

enum class Action {
    None, Play, ChooseData, VerifyData, SaveSettings, ReloadSettings,
    OpenLogs, OpenSaves, OpenSettings, BuildGuide, ShowInstall,
};
} // namespace Launcher
