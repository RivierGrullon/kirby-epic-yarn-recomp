// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "platform.h"
#include <chrono>
#include <filesystem>
#include <stdexcept>
inline std::filesystem::path TemporaryLauncherDirectory(const char* prefix) {
    const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    for (unsigned attempt=0;attempt<100;++attempt) {
        const auto path=std::filesystem::temp_directory_path()/(std::string(prefix)+std::to_string(stamp)+"-"+std::to_string(attempt));
        if (std::filesystem::create_directory(path)) return path;
    }
    throw std::runtime_error("Cannot create launcher test directory");
}
inline std::string AbsoluteFixture(const char* name) {
    return Launcher::Utf8(std::filesystem::absolute(Launcher::FromUtf8(name)));
}
