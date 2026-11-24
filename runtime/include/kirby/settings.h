// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "kirby/input/bindings.h"
#include "kirby/input/motion.h"
#include "kirby/graphics/quality.h"
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>

namespace Kirby {
enum class ButtonIconStyle { Auto, Xbox, PlayStation, Switch, Keyboard };
inline constexpr std::array<std::string_view,5> ButtonIconStyleNames{
    "auto", "xbox", "playstation", "switch", "keyboard"
};
struct ButtonPromptSettings {
    bool enabled = false;
    ButtonIconStyle style = ButtonIconStyle::Auto;
};
struct Settings {
    std::filesystem::path gameData = "disc/DATA";
    float masterVolume = 100;
    static constexpr bool DefaultVsync = false;
    static constexpr bool DefaultGpuResidentCopies = true;
    bool vsync = DefaultVsync;
    bool gpuResidentCopies = DefaultGpuResidentCopies;
    ControlBindings controls{};
    MotionSettings motion{};
    int resolutionScale = 1;
    int msaaSamples = 1;
    bool fxaa = false;
    bool fullscreen = false;
    // Wii SYSCONF language IDs supported by the USA disc.
    static constexpr int English = 1, French = 3, Spanish = 4;
    std::optional<int> gameLanguage; // An unset preference keeps the existing Wii language.
    ButtonPromptSettings buttonPrompts{};
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
