// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Launcher::Setup {
struct Inspection {
    std::filesystem::path source, dataRoot;
    std::string sourceKind;
    uint64_t assetFiles=0,assetBytes=0;
    bool cached=false;
};
struct Event {
    std::string stage,state,error;
    std::filesystem::path logPath;
    std::optional<uint64_t> completed,total;
    std::optional<Inspection> result;
};
Event ParseEvent(std::string_view line,std::string_view identifier,uint64_t sequence);
std::string InspectRequest(std::string_view identifier,const std::filesystem::path& source);
std::string CancelRequest(std::string_view identifier);
} // namespace Launcher::Setup
