// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <string>
#include <string_view>

namespace Kirby {
enum class BuildProgram { Runtime, Launcher };
struct BuildInfo {
    std::string program, sourceRevision, sourceSha256, translatorRevision, auroraRevision;
    std::string compiler, buildType, compilerOptions, gameId, dolSha1;
    bool sourceModified = false;
    int gameRevision = 0;
    bool operator==(const BuildInfo&) const = default;
};
std::string BuildInformation(BuildProgram program);
BuildInfo ParseBuildInformation(std::string_view document);
bool SameBuildInputs(const BuildInfo& first, const BuildInfo& second);
} // namespace Kirby
