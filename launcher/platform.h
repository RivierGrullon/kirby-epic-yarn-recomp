// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <stdexcept>
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
struct SetupWorker {
    std::filesystem::path file;
    std::string interpreter;
};
inline SetupWorker FindSetupWorker(const std::filesystem::path& root) {
#ifdef _WIN32
    const auto native=root/"tools/runtime/kirby-setup-worker.exe";
    const auto python=root/"tools/runtime/python/python.exe";
#else
    const auto native=root/"tools/runtime/kirby-setup-worker";
    const auto python=root/"tools/runtime/python/bin/python3";
#endif
    if (std::filesystem::is_regular_file(native)) return {native,{}};
    if (std::filesystem::is_regular_file(python)) return {root/"tools/install.py",Utf8(python)};
    if (std::filesystem::exists(root/"tools/runtime/toolchain.json"))
        throw std::runtime_error("The setup tools are incomplete. Extract the complete application package again.");
    return {root/"tools/install.py",PythonExecutable};
}
} // namespace Launcher
