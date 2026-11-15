// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <atomic>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#include "kirby/platform/windows_file.h"
#include <winioctl.h>
#endif

inline std::filesystem::path TestDirectory(const char* prefix) {
    static std::atomic<unsigned> counter{0};
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        auto path = std::filesystem::temp_directory_path() /
            (std::string(prefix) + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
             "-" + std::to_string(counter.fetch_add(1)));
        if (std::filesystem::create_directory(path)) return path;
    }
    throw std::runtime_error("Cannot create temporary test directory");
}
#ifdef _WIN32
// Junction creation does not require the symlink privilege or Developer Mode.
// It therefore exercises reparse containment on ordinary Windows CI accounts.
inline void TestJunction(const std::filesystem::path& target, const std::filesystem::path& link) {
    std::filesystem::create_directory(link);
    Kirby::WindowsFile::Handle directory(CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!directory) throw std::runtime_error("Cannot open test junction");
    const auto destination = L"\\??\\" + std::filesystem::absolute(target).native();
    struct Junction {
        DWORD tag = IO_REPARSE_TAG_MOUNT_POINT;
        WORD length = 0, reserved = 0;
        WORD substituteOffset = 0, substituteLength = 0, printOffset = 0, printLength = 0;
        WCHAR path[16384]{};
    } data;
    if (destination.size() + 2 >= std::size(data.path)) throw std::runtime_error("Junction target too long");
    std::copy(destination.begin(), destination.end(), data.path);
    data.substituteLength = static_cast<WORD>(destination.size() * sizeof(WCHAR));
    data.printOffset = data.substituteLength + sizeof(WCHAR);
    data.length = 8 + data.printOffset + sizeof(WCHAR);
    DWORD returned = 0;
    if (!DeviceIoControl(directory.value, FSCTL_SET_REPARSE_POINT, &data, data.length + 8, nullptr, 0, &returned, nullptr))
        throw std::runtime_error("Cannot create test junction: " + std::to_string(GetLastError()));
}
#endif
