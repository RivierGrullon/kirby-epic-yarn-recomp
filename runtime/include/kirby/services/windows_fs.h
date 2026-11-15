// SPDX-License-Identifier: GPL-3.0-only
// Windows implementation of the descriptor operations used by Kirby's storage.
// Directory handles deny rename/delete while resolving a single child component.
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <climits>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <fcntl.h>
#undef CreateFile
#undef CreateDirectory

#define O_DIRECTORY 0x100000
#define O_NOFOLLOW 0x200000
#define O_CLOEXEC 0x400000
#define O_NONBLOCK 0x800000
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_REMOVEDIR 0x200
#define XATTR_CREATE 1

namespace Kirby::WinFs {
struct Stat { uint32_t st_mode{}, st_dev{}; uint64_t st_ino{}; int64_t st_size{}; };
inline constexpr uint32_t DirectoryMode = 0040000, FileMode = 0100000;
inline std::mutex mutex;
inline std::unordered_map<int, HANDLE> handles;
inline int nextFd = 100000;
inline int Fail(DWORD error = GetLastError()) {
    switch (error) {
    case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: errno = ENOENT; break;
    case ERROR_ALREADY_EXISTS: case ERROR_FILE_EXISTS: errno = EEXIST; break;
    case ERROR_ACCESS_DENIED: case ERROR_SHARING_VIOLATION: errno = EACCES; break;
    case ERROR_DIRECTORY: errno = ENOTDIR; break;
    case ERROR_DIR_NOT_EMPTY: errno = ENOTEMPTY; break;
    case ERROR_DISK_FULL: case ERROR_HANDLE_DISK_FULL: errno = ENOSPC; break;
    case ERROR_INVALID_HANDLE: errno = EBADF; break;
    case ERROR_INVALID_NAME: case ERROR_INVALID_PARAMETER: errno = EINVAL; break;
    default: errno = EIO; break;
    }
    return -1;
}
inline HANDLE Get(int fd) {
    std::lock_guard lock(mutex);
    const auto it = handles.find(fd);
    if (it == handles.end()) { errno = EBADF; return INVALID_HANDLE_VALUE; }
    return it->second;
}
inline int Own(HANDLE handle) {
    if (handle == INVALID_HANDLE_VALUE) return Fail();
    std::lock_guard lock(mutex);
    if (nextFd == INT_MAX) { CloseHandle(handle); errno = EMFILE; return -1; }
    const int fd = nextFd++;
    handles.emplace(fd, handle);
    return fd;
}
inline int Close(int fd) {
    std::lock_guard lock(mutex);
    const auto it = handles.find(fd);
    if (it == handles.end()) { errno = EBADF; return -1; }
    const auto h = it->second; handles.erase(it);
    return CloseHandle(h) ? 0 : Fail();
}
inline int Dup(int fd) {
    HANDLE copy;
    if (!DuplicateHandle(GetCurrentProcess(), Get(fd), GetCurrentProcess(), &copy, 0, FALSE, DUPLICATE_SAME_ACCESS)) return Fail();
    return Own(copy);
}
inline std::filesystem::path Path(int fd) {
    std::wstring text(32768, L'\0');
    const auto count = GetFinalPathNameByHandleW(Get(fd), text.data(), DWORD(text.size()), FILE_NAME_NORMALIZED);
    if (!count || count >= text.size()) { Fail(); return {}; }
    text.resize(count); return text;
}
inline bool Component(std::string_view name) {
    if (name.empty() || name == "." || name == ".." || name.back() == '.' || name.back() == ' ') return false;
    for (const unsigned char c : name) if (c < 32 || std::strchr("\\/:*?\"<>|", c)) return false;
    std::string stem(name.substr(0, name.find('.')));
    std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char c) { return char(std::toupper(c)); });
    return stem != "CON" && stem != "PRN" && stem != "AUX" && stem != "NUL" &&
        !(stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) && stem[3] >= '0' && stem[3] <= '9');
}
inline std::filesystem::path Child(int dir, const char* name) {
    if (!Component(name)) { errno = EINVAL; return {}; }
    auto parent = Path(dir);
    std::u8string utf8;
    for (const unsigned char c : std::string_view(name)) utf8.push_back(char8_t(c));
    return parent.empty() ? parent : parent / std::filesystem::path(utf8);
}
inline int Open(const std::filesystem::path& path, int flags, int = 0) {
    DWORD access = FILE_READ_ATTRIBUTES;
    switch (flags & O_ACCMODE) {
    case O_RDONLY: access |= GENERIC_READ; break;
    case O_WRONLY: access |= GENERIC_WRITE; break;
    case O_RDWR: access |= GENERIC_READ | GENERIC_WRITE; break;
    default: errno = EINVAL; return -1;
    }
    const auto disposition = flags & O_CREAT ? (flags & O_EXCL ? CREATE_NEW : OPEN_ALWAYS) : OPEN_EXISTING;
    auto h = CreateFileW(path.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, disposition,
                        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return Fail();
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(h, &info)) { auto e = GetLastError(); CloseHandle(h); return Fail(e); }
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || GetFileType(h) != FILE_TYPE_DISK) {
        CloseHandle(h); errno = ELOOP; return -1;
    }
    if ((flags & O_DIRECTORY) && !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        CloseHandle(h); errno = ENOTDIR; return -1;
    }
    return Own(h);
}
inline int OpenAt(int dir, const char* name, int flags, int mode = 0) {
    if (std::strcmp(name, ".") == 0) return Dup(dir);
    const auto path = Child(dir, name);
    return path.empty() ? -1 : Open(path, flags, mode);
}
inline int Fstat(int fd, Stat* out) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(Get(fd), &info)) return Fail();
    if (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) { errno = ELOOP; return -1; }
    out->st_mode = info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ? DirectoryMode : FileMode;
    out->st_dev = info.dwVolumeSerialNumber;
    out->st_ino = (uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
    out->st_size = int64_t((uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow);
    return 0;
}
inline int FstatAt(int dir, const char* name, Stat* out, int) {
    int fd = OpenAt(dir, name, O_RDONLY);
    if (fd < 0) return -1;
    int result = Fstat(fd, out); int error = errno; Close(fd); errno = error; return result;
}
inline int64_t Read(int fd, void* data, size_t size) {
    if (size > MAXDWORD) { errno = EINVAL; return -1; }
    DWORD n{}; return ReadFile(Get(fd), data, DWORD(size), &n, nullptr) ? int64_t(n) : Fail();
}
inline int64_t Write(int fd, const void* data, size_t size) {
    if (size > MAXDWORD) { errno = EINVAL; return -1; }
    DWORD n{}; return WriteFile(Get(fd), data, DWORD(size), &n, nullptr) ? int64_t(n) : Fail();
}
inline int64_t Seek(int fd, int64_t offset, int origin) {
    if (origin < 0 || origin > 2) { errno = EINVAL; return -1; }
    LARGE_INTEGER at{}, result{}; at.QuadPart = offset;
    return SetFilePointerEx(Get(fd), at, &result, DWORD(origin)) ? result.QuadPart : Fail();
}
inline int64_t Pread(int fd, void* data, size_t size, uint64_t offset) {
    if (offset > INT64_MAX) { errno = EINVAL; return -1; }
    const auto original = Seek(fd, 0, SEEK_CUR);
    if (original < 0 || Seek(fd, int64_t(offset), SEEK_SET) < 0) return -1;
    const auto count = Read(fd, data, size); const auto error = errno;
    if (Seek(fd, original, SEEK_SET) < 0) return -1;
    errno = error; return count;
}
inline int MkdirAt(int dir, const char* name, int) {
    const auto path = Child(dir, name);
    return path.empty() ? -1 : CreateDirectoryW(path.c_str(), nullptr) ? 0 : Fail();
}
inline int UnlinkAt(int dir, const char* name, int flags) {
    const auto path = Child(dir, name);
    if (path.empty()) return -1;
    const auto attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return Fail();
    if (attr & FILE_ATTRIBUTE_REPARSE_POINT) { errno = ELOOP; return -1; }
    return (flags & AT_REMOVEDIR ? RemoveDirectoryW(path.c_str()) : DeleteFileW(path.c_str())) ? 0 : Fail();
}
inline int RenameAt(int oldDir, const char* oldName, int newDir, const char* newName) {
    const auto from = Child(oldDir, oldName), to = Child(newDir, newName);
    if (from.empty() || to.empty()) return -1;
    return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? 0 : Fail();
}
// NTFS named streams retain these four authored attribute bytes across renames,
// and do not add guest-visible directory entries or usage clusters.
inline int64_t GetAttr(int fd, const char* key, void* output, size_t size) {
    if (std::strcmp(key, "user.kirby.isfs")) { errno = EINVAL; return -1; }
    const auto base = Path(fd); if (base.empty()) return -1;
    auto h = CreateFileW((base.wstring() + L":kirby.isfs").c_str(), GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) { errno = ENODATA; return -1; }
        return Fail();
    }
    LARGE_INTEGER length{}; DWORD count{};
    if (!GetFileSizeEx(h, &length) || length.QuadPart < 0 || uint64_t(length.QuadPart) > size) {
        CloseHandle(h); errno = ERANGE; return -1;
    }
    const bool ok = ReadFile(h, output, DWORD(length.QuadPart), &count, nullptr);
    const auto error = GetLastError(); CloseHandle(h);
    return ok ? int64_t(count) : Fail(error);
}
inline int SetAttr(int fd, const char* key, const void* data, size_t size, int flags) {
    if (std::strcmp(key, "user.kirby.isfs") || size != 4) { errno = EINVAL; return -1; }
    const auto base = Path(fd); if (base.empty()) return -1;
    auto h = CreateFileW((base.wstring() + L":kirby.isfs").c_str(), GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, flags & XATTR_CREATE ? CREATE_NEW : CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return Fail();
    DWORD written{};
    const bool ok = WriteFile(h, data, DWORD(size), &written, nullptr) && written == size && FlushFileBuffers(h);
    const auto error = GetLastError(); CloseHandle(h); return ok ? 0 : Fail(error);
}
struct Entry { char d_name[1024]{}; };
struct Directory { int fd; HANDLE search = INVALID_HANDLE_VALUE; WIN32_FIND_DATAW data{}; bool first = true; Entry entry{}; };
inline Directory* Fdopendir(int fd) {
    const auto path = Path(fd); if (path.empty()) return nullptr;
    auto* dir = new Directory{fd};
    dir->search = FindFirstFileW((path / L"*").c_str(), &dir->data);
    if (dir->search == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_NOT_FOUND) { Fail(); delete dir; return nullptr; }
    return dir;
}
inline Entry* Readdir(Directory* dir) {
    if (dir->search == INVALID_HANDLE_VALUE) { errno = 0; return nullptr; }
    if (!dir->first && !FindNextFileW(dir->search, &dir->data)) {
        if (GetLastError() == ERROR_NO_MORE_FILES) errno = 0; else Fail();
        return nullptr;
    }
    dir->first = false;
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, dir->data.cFileName, -1,
                            dir->entry.d_name, sizeof(dir->entry.d_name), nullptr, nullptr)) { Fail(); return nullptr; }
    return &dir->entry;
}
inline int Closedir(Directory* dir) {
    if (dir->search != INVALID_HANDLE_VALUE) FindClose(dir->search);
    const int result = Close(dir->fd); delete dir; return result;
}
}

// Local translation-unit aliases, included after all system/library headers.
#define open Kirby::WinFs::Open
#define openat Kirby::WinFs::OpenAt
#define close Kirby::WinFs::Close
#define dup Kirby::WinFs::Dup
#define fstat Kirby::WinFs::Fstat
#define fstatat Kirby::WinFs::FstatAt
#define stat Kirby::WinFs::Stat
#define read Kirby::WinFs::Read
#define write Kirby::WinFs::Write
#define lseek Kirby::WinFs::Seek
#define pread Kirby::WinFs::Pread
#define mkdirat Kirby::WinFs::MkdirAt
#define unlinkat Kirby::WinFs::UnlinkAt
#define renameat Kirby::WinFs::RenameAt
#define fgetxattr Kirby::WinFs::GetAttr
#define fsetxattr Kirby::WinFs::SetAttr
#define DIR Kirby::WinFs::Directory
#define fdopendir Kirby::WinFs::Fdopendir
#define readdir Kirby::WinFs::Readdir
#define closedir Kirby::WinFs::Closedir
#undef S_ISREG
#undef S_ISDIR
#define S_ISREG(mode) ((mode) == Kirby::WinFs::FileMode)
#define S_ISDIR(mode) ((mode) == Kirby::WinFs::DirectoryMode)
