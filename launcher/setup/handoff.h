// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>
#include "../platform.h"

namespace Launcher::Setup {
inline std::vector<std::string> ReadSetupCommand(const std::filesystem::path& root) {
    std::ifstream stream(root/"local/setup-command.json",std::ios::binary);
    std::string text(16385,'\0');stream.read(text.data(),text.size());text.resize(stream.gcount());
    if (text.empty() || text.size()>16384) throw std::runtime_error("Open your Kirby Setup application to update this installation.");
    const auto record=nlohmann::json::parse(text);
    if (!record.is_object() || record.value("version",0)!=1 || !record.contains("arguments") ||
        !record.at("arguments").is_array() || record.at("arguments").empty() || record.at("arguments").size()>8)
        throw std::runtime_error("Invalid Kirby Setup location");
    auto command=record.at("arguments").get<std::vector<std::string>>();
    for (const auto& argument:command)
        if (argument.empty() || argument.size()>8192 || argument.find('\0')!=std::string::npos)
            throw std::runtime_error("Invalid Kirby Setup argument");
    if (!FromUtf8(command[0]).is_absolute()) throw std::runtime_error("Kirby Setup requires an absolute executable path");
    return command;
}
}
