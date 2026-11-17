// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace Launcher::Setup {
std::vector<std::string> ReadSetupCommand(const std::filesystem::path& root);
}
