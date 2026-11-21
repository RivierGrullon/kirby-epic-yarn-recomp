// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "kirby/settings.h"
#include "binding_capture.h"
#include "kirby/app/build_info.h"
#include "motion_preview.h"
#include <filesystem>
#include <optional>
#include <string>

namespace Launcher {
struct State {
    std::filesystem::path root, executable, settingsFile;
    Kirby::Settings config;
    bool dataVerified = false;
    bool settingsDirty = false;
    bool gameRunning = false;
    std::string dataStatus = "Select your extracted DATA folder";
    std::string gameStatus;
    std::string settingsError;
    BindingCapture bindingCapture;
    Kirby::BuildInfo launcherBuild;
    std::optional<Kirby::BuildInfo> runtimeBuild;
    std::string installationId,installationError;
    std::string saveImportStatus;
    MotionPreview motionPreview;
};
} // namespace Launcher
