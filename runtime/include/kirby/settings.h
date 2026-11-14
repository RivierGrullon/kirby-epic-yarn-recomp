// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "kirby/input/bindings.h"
#include <filesystem>
#include <iosfwd>
#include <string>

namespace Kirby {
struct Settings {
    std::filesystem::path gameData = "disc/DATA";
    float masterVolume = 100;
    bool vsync = true;
    bool gpuResidentCopies = false;
    ControlBindings controls{};
};

// Errors throw without modifying the source file. Relative DATA paths are
// resolved against the TOML file's directory; defaults belong to the caller.
Settings LoadSettings(const std::filesystem::path& file, Settings defaults = {});
std::string FormatSettings(const Settings& settings);
void SaveSettings(const std::filesystem::path& file, const Settings& settings);

// The previous launcher stored paths relative to its working directory.
// Migration is only attempted when the TOML file does not exist. The legacy
// file and every save file are left untouched.
Settings LoadOrMigrateSettings(const std::filesystem::path& file,
                              const std::filesystem::path& legacy,
                              const std::filesystem::path& legacyWorkingDirectory,
                              Settings defaults = {});
int InspectSettings(const std::filesystem::path& file, std::ostream& output, std::ostream& errors);
} // namespace Kirby
