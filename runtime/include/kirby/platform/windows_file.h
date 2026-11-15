// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winternl.h>
#undef CreateFile
#undef CreateDirectory
#include <algorithm>
#include <cstddef>
#include <climits>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Kirby::WindowsFile {
// NT relative opens are required here: checking a pathname and then opening it
// with CreateFile leaves an ancestor junction swap between those operations.
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE handle = INVALID_HANDLE_VALUE) : value(handle) {}
    ~Handle() { const auto error = GetLastError(); if (value != INVALID_HANDLE_VALUE) CloseHandle(value); SetLastError(error); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value(other.release()) {}
    HANDLE release() { const auto result = value; value = INVALID_HANDLE_VALUE; return result; }
    explicit operator bool() const { return value != INVALID_HANDLE_VALUE; }
};
template<class T> inline T Function(const char* name) {
    const auto address = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), name);
    if (!address) throw std::runtime_error(std::string("Windows filesystem API unavailable: ") + name);
    return reinterpret_cast<T>(address);
}
inline bool Status(NTSTATUS status) {
    if (status >= 0) return true;
    using Convert = ULONG (NTAPI*)(NTSTATUS);
    static const auto convert = Function<Convert>("RtlNtStatusToDosError");
    SetLastError(convert(status));
    return false;
}
inline bool Component(std::wstring_view name) {
    // Also reject alternate streams, DOS aliases, and trailing-dot/space aliases.
    if (name.empty() || name == L"." || name == L".." || name.back() == L'.' || name.back() == L' ')
        return false;
    for (const auto c : name) if (c < 32 || std::wstring_view(L"\\/:*?\"<>|").find(c) != std::wstring_view::npos)
        return false;
    auto base = std::wstring(name.substr(0, name.find(L'.')));
    for (auto& c : base) if (c >= L'a' && c <= L'z') c -= L'a' - L'A';
    return base != L"CON" && base != L"PRN" && base != L"AUX" && base != L"NUL" &&
        !(base.size() == 4 && (base.starts_with(L"COM") || base.starts_with(L"LPT")) &&
          (base[3] >= L'1' && base[3] <= L'9'));
}
inline HANDLE Duplicate(HANDLE original) {
    HANDLE copy = INVALID_HANDLE_VALUE;
    if (!DuplicateHandle(GetCurrentProcess(), original, GetCurrentProcess(), &copy, 0, FALSE, DUPLICATE_SAME_ACCESS))
        return INVALID_HANDLE_VALUE;
    return copy;
}
inline bool Inspect(HANDLE file, BY_HANDLE_FILE_INFORMATION& info) {
    if (!GetFileInformationByHandle(file, &info)) return false;
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || GetFileType(file) != FILE_TYPE_DISK) {
        SetLastError(ERROR_ACCESS_DENIED); return false;
    }
    return true;
}
inline HANDLE Root(const std::filesystem::path& path, bool write = false) {
    Handle root(CreateFileW(path.c_str(), FILE_LIST_DIRECTORY | FILE_TRAVERSE | FILE_READ_ATTRIBUTES |
                           FILE_READ_EA | (write ? FILE_WRITE_EA : 0),
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    if (!root || !Inspect(root.value, info)) return INVALID_HANDLE_VALUE;
    if (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        SetLastError(ERROR_DIRECTORY); return INVALID_HANDLE_VALUE;
    }
    return root.release();
}
enum class Kind { Any, File, Directory };
// disposition: 1 = FILE_OPEN, 2 = FILE_CREATE, 3 = FILE_OPEN_IF.
inline HANDLE Open(HANDLE parent, std::wstring_view name, ACCESS_MASK access,
                   Kind kind = Kind::Any, ULONG disposition = 1) {
    if (!Component(name) || name.size() > (USHRT_MAX / sizeof(wchar_t))) {
        SetLastError(ERROR_INVALID_NAME); return INVALID_HANDLE_VALUE;
    }
    UNICODE_STRING string{};
    string.Buffer = const_cast<wchar_t*>(name.data());
    string.Length = string.MaximumLength = static_cast<USHORT>(name.size() * sizeof(wchar_t));
    OBJECT_ATTRIBUTES attributes{};
    attributes.Length = sizeof(attributes);
    attributes.RootDirectory = parent;
    attributes.ObjectName = &string;
    attributes.Attributes = 0x40; // OBJ_CASE_INSENSITIVE; FILE_OPEN_REPARSE_POINT below opens the link itself.
    IO_STATUS_BLOCK result{};
    HANDLE file = INVALID_HANDLE_VALUE;
    using Create = NTSTATUS (NTAPI*)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK,
        PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);
    static const auto create = Function<Create>("NtCreateFile");
    const ULONG options = 0x00200000 /* FILE_OPEN_REPARSE_POINT */ | 0x20 /* FILE_SYNCHRONOUS_IO_NONALERT */ |
        (kind == Kind::Directory ? 1 : kind == Kind::File ? 0x40 : 0);
    if (!Status(create(&file, access | SYNCHRONIZE | FILE_READ_ATTRIBUTES, &attributes, &result,
                       nullptr, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       disposition, options, nullptr, 0))) return INVALID_HANDLE_VALUE;
    Handle owned(file);
    BY_HANDLE_FILE_INFORMATION info{};
    if (!Inspect(file, info)) return INVALID_HANDLE_VALUE;
    return owned.release();
}
inline HANDLE Walk(HANDLE root, const std::filesystem::path& path, ACCESS_MASK access) {
    if (path.empty() || path.has_root_path()) { SetLastError(ERROR_INVALID_NAME); return INVALID_HANDLE_VALUE; }
    Handle directory(Duplicate(root));
    if (!directory) return INVALID_HANDLE_VALUE;
    for (auto it = path.begin(); it != path.end(); ++it) {
        const bool last = std::next(it) == path.end();
        Handle next(Open(directory.value, it->native(), last ? access : FILE_LIST_DIRECTORY | FILE_TRAVERSE,
                         last ? Kind::File : Kind::Directory));
        if (!next) return INVALID_HANDLE_VALUE;
        if (last) return next.release();
        CloseHandle(directory.value); directory.value = next.release();
    }
    SetLastError(ERROR_INVALID_NAME); return INVALID_HANDLE_VALUE;
}
inline uint64_t Size(HANDLE file) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!Inspect(file, info) || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        throw std::runtime_error("Expected a regular disk file");
    return uint64_t(info.nFileSizeHigh) << 32 | info.nFileSizeLow;
}
inline void ReadAt(HANDLE file, uint64_t offset, std::span<uint8_t> bytes) {
    if (offset > INT64_MAX) throw std::runtime_error("File offset exceeds Windows file range");
    LARGE_INTEGER position{}; position.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(file, position, nullptr, FILE_BEGIN)) throw std::runtime_error("Cannot seek file");
    size_t done = 0;
    while (done < bytes.size()) {
        DWORD count = 0;
        if (!ReadFile(file, bytes.data() + done, static_cast<DWORD>(std::min<size_t>(bytes.size() - done, MAXDWORD)),
                      &count, nullptr) || !count) throw std::runtime_error("File read failed or became truncated");
        done += count;
    }
}
inline bool Remove(HANDLE file) {
    FILE_DISPOSITION_INFO info{TRUE};
    return SetFileInformationByHandle(file, FileDispositionInfo, &info, sizeof(info));
}
inline bool Rename(HANDLE file, HANDLE parent, std::wstring_view name, bool replace) {
    if (!Component(name)) { SetLastError(ERROR_INVALID_NAME); return false; }
    // Native FILE_RENAME_INFORMATION accepts a directory handle. The Win32
    // wrapper requires an absolute path and cannot provide this guarantee.
    struct RenameInfo { BOOLEAN replace; HANDLE root; ULONG length; WCHAR name[1]; };
    std::vector<uint8_t> storage(offsetof(RenameInfo, name) + name.size() * sizeof(wchar_t));
    auto* info = reinterpret_cast<RenameInfo*>(storage.data());
    info->replace = replace; info->root = parent; info->length = static_cast<ULONG>(name.size() * sizeof(wchar_t));
    std::copy(name.begin(), name.end(), info->name);
    IO_STATUS_BLOCK result{};
    using Set = NTSTATUS (NTAPI*)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS);
    static const auto set = Function<Set>("NtSetInformationFile");
    return Status(set(file, &result, info, static_cast<ULONG>(storage.size()), static_cast<FILE_INFORMATION_CLASS>(10)));
}
} // namespace Kirby::WindowsFile
#endif
