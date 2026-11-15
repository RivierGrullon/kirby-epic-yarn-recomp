// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#include <filesystem>
#include <stdexcept>
#include <vector>
#include <chrono>
#include <cstring>
#undef CreateFile
#undef CreateDirectory
namespace WindowsTest {
inline std::filesystem::path TemporaryDirectory() {
    const auto parent = std::filesystem::current_path() / "local/windows-tests";
    std::filesystem::create_directories(parent);
    for (unsigned i = 0; i < 128; ++i) {
        auto path = parent / (std::to_string(GetCurrentProcessId()) + "-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(i));
        if (std::filesystem::create_directory(path)) return path;
    }
    throw std::runtime_error("Cannot create private test directory");
}
inline void Junction(std::filesystem::path target, const std::filesystem::path& link) {
    if (!std::filesystem::is_directory(target)) target = link.parent_path();
    const auto path = std::filesystem::absolute(target).wstring();
    const std::wstring substitute = L"\\??\\" + path;
    const auto subBytes = substitute.size() * 2, printBytes = path.size() * 2;
    std::vector<unsigned char> bytes(16 + subBytes + 2 + printBytes + 2);
    auto put16 = [&](size_t at, unsigned value) { bytes[at] = value; bytes[at+1] = value >> 8; };
    const DWORD tag = IO_REPARSE_TAG_MOUNT_POINT;
    std::memcpy(bytes.data(), &tag, 4);
    put16(4, unsigned(bytes.size() - 8));
    put16(8, 0); put16(10, unsigned(subBytes)); put16(12, unsigned(subBytes + 2)); put16(14, unsigned(printBytes));
    std::memcpy(bytes.data()+16, substitute.data(), subBytes);
    std::memcpy(bytes.data()+16+subBytes+2, path.data(), printBytes);
    std::filesystem::create_directory(link);
    HANDLE handle = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open junction fixture");
    DWORD returned;
    const bool ok = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, bytes.data(), DWORD(bytes.size()), nullptr, 0, &returned, nullptr);
    CloseHandle(handle);
    if (!ok) throw std::runtime_error("Cannot create junction fixture");
}
inline void FileLink(const std::filesystem::path& target, const std::filesystem::path& link) {
    // A junction also exercises rejection of a reparse point in the final
    // file component without requiring the symbolic-link creation privilege.
    Junction(target.parent_path(), link);
}
inline int SetAttr(const wchar_t* path, const char*, const void* value, size_t size, int) {
    auto h = CreateFileW((std::wstring(path) + L":kirby.isfs").c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return -1;
    DWORD written{}; bool ok = WriteFile(h, value, DWORD(size), &written, nullptr) && written == size;
    CloseHandle(h); return ok ? 0 : -1;
}
inline int GetAttr(const wchar_t* path, const char*, void* value, size_t size) {
    auto h = CreateFileW((std::wstring(path) + L":kirby.isfs").c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return -1;
    DWORD read{}; bool ok = ReadFile(h, value, DWORD(size), &read, nullptr);
    CloseHandle(h); return ok ? int(read) : -1;
}
}
#endif
