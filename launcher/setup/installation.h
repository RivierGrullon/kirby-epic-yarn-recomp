// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <filesystem>
#include <optional>
#include <string>

namespace Launcher::Setup {
struct SelectedInstallation {
    std::string identifier;
    std::optional<std::string> previous;
    std::filesystem::path executable,dataRoot;
};
// Missing pointer means a developer build. Corrupt selection throws.
std::optional<SelectedInstallation> ReadInstallation(const std::filesystem::path& root);
} // namespace Launcher::Setup
