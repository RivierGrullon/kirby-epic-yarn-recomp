// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <filesystem>
#include <string>
#include <string_view>
namespace Launcher {
#ifdef _WIN32
inline constexpr char GameExecutable[]="WiiCompiled.exe";
inline constexpr char PythonExecutable[]="python";
#else
inline constexpr char GameExecutable[]="WiiCompiled";
inline constexpr char PythonExecutable[]="python3";
#endif
inline std::string Utf8(const std::filesystem::path& path) {
    const auto text=path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()),text.size());
}
inline std::filesystem::path FromUtf8(std::string_view text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()),text.size()));
}
} // namespace Launcher
