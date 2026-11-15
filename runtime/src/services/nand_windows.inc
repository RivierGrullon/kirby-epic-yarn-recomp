// SPDX-License-Identifier: GPL-3.0-only
// Included by nand.cpp inside namespace Kirby; shares the console wire formats.
namespace {
using WindowsFile::Handle;
using WindowsFile::Kind;
HANDLE Native(intptr_t fd) { return reinterpret_cast<HANDLE>(fd); }
int32_t Error() {
    switch (GetLastError()) {
    case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: return -106;
    case ERROR_ACCESS_DENIED: case ERROR_CANT_ACCESS_FILE: case ERROR_DIRECTORY:
    case ERROR_REPARSE_TAG_INVALID: case ERROR_REPARSE_TAG_MISMATCH: return -102;
    case ERROR_INVALID_NAME: case ERROR_INVALID_PARAMETER: case ERROR_INVALID_HANDLE: return -101;
    case ERROR_DISK_FULL: case ERROR_HANDLE_DISK_FULL: return -108;
    case ERROR_ALREADY_EXISTS: case ERROR_FILE_EXISTS: return -105;
    default: return -103;
    }
}
constexpr ACCESS_MASK DirectoryAccess = FILE_LIST_DIRECTORY | FILE_TRAVERSE | FILE_READ_EA | FILE_WRITE_EA;
std::wstring Wide(std::string_view path) { return std::filesystem::path(std::u8string(path.begin(), path.end())).native(); }
// NTFS EAs stay attached to the object across atomic rename, and are accessed
// through its handle. Sidecars or path-based ADS opens would introduce races.
struct EaRecord {
    ULONG next = 0;
    UCHAR flags = 0, nameLength = 10;
    USHORT valueLength = 4;
    char bytes[15] = {'K','I','R','B','Y','.','I','S','F','S',0};
};
bool SetMetadata(HANDLE file, const std::array<uint8_t,4>& value) {
    EaRecord record;
    std::memcpy(record.bytes + 11, value.data(), 4);
    IO_STATUS_BLOCK result{};
    using Set = NTSTATUS (NTAPI*)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG);
    static const auto set = WindowsFile::Function<Set>("NtSetEaFile");
    return WindowsFile::Status(set(file, &result, &record, offsetof(EaRecord, bytes) + sizeof(record.bytes)));
}
bool GetMetadata(HANDLE file, std::array<uint8_t,4>& value, bool& missing) {
    struct EaName { ULONG next = 0; UCHAR length = 10; char name[11] = "KIRBY.ISFS"; } name;
    EaRecord record;
    IO_STATUS_BLOCK result{};
    using Query = NTSTATUS (NTAPI*)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, BOOLEAN, PVOID, ULONG, PULONG, BOOLEAN);
    static const auto query = WindowsFile::Function<Query>("NtQueryEaFile");
    const auto status = query(file, &result, &record, sizeof(record), TRUE, &name,
                             offsetof(EaName, name) + sizeof(name.name), nullptr, TRUE);
    missing = static_cast<ULONG>(status) == 0xC0000052 /* STATUS_NO_EAS_ON_FILE */ ||
              static_cast<ULONG>(status) == 0x80000015 /* STATUS_NONEXISTENT_EA_ENTRY */;
    if (missing) return false;
    if (!WindowsFile::Status(status)) return false;
    // A named query can return the requested name with an empty value.
    if (record.valueLength == 0) { missing = true; return false; }
    if (result.Information < offsetof(EaRecord, bytes) + 15 || record.nameLength != 10 ||
        record.valueLength != 4 || std::memcmp(record.bytes, "KIRBY.ISFS", 11)) {
        SetLastError(ERROR_INVALID_DATA); return false;
    }
    std::memcpy(value.data(), record.bytes + 11, 4);
    if (value[0] > 3 || value[1] > 3 || value[2] > 3) { SetLastError(ERROR_INVALID_DATA); return false; }
    return true;
}
void EnsureMetadata(HANDLE file) {
    std::array<uint8_t,4> value{};
    bool missing = false;
    if (GetMetadata(file, value, missing)) return;
    if (!missing || !SetMetadata(file, {3,3,3,0}))
        throw std::runtime_error("Cannot read or initialize NAND permission metadata; use a volume supporting extended attributes (NTFS)");
}
HANDLE Parent(HANDLE root, const std::string& path, bool create, std::wstring& leaf) {
    Handle directory(WindowsFile::Duplicate(root));
    if (!directory) return INVALID_HANDLE_VALUE;
    size_t start = 1;
    for (;;) {
        const auto end = path.find('/', start);
        if (end == path.npos) { leaf = Wide(std::string_view(path).substr(start)); return directory.release(); }
        Handle next(WindowsFile::Open(directory.value, Wide(std::string_view(path).substr(start, end-start)),
                                     DirectoryAccess, Kind::Directory, create ? 3 : 1));
        if (!next) return INVALID_HANDLE_VALUE;
        if (create) EnsureMetadata(next.value);
        CloseHandle(directory.value); directory.value = next.release();
        start = end + 1;
    }
}
bool Info(HANDLE file, BY_HANDLE_FILE_INFORMATION& info) {
    if (!WindowsFile::Inspect(file, info)) return false;
    // A host hard link could point outside the private NAND tree.
    if (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && info.nNumberOfLinks != 1) {
        SetLastError(ERROR_ACCESS_DENIED); return false;
    }
    return true;
}
void CreateDefault(HANDLE root, const std::string& path, std::span<const uint8_t> bytes) {
    std::wstring leaf;
    Handle parent(Parent(root, path, true, leaf));
    if (!parent) throw std::runtime_error("Cannot create NAND directory for " + path);
    Handle file(WindowsFile::Open(parent.value, leaf, FILE_WRITE_DATA | FILE_READ_EA | FILE_WRITE_EA | DELETE,
                                 Kind::File, 2));
    if (!file) {
        if (Error() != -105) throw std::runtime_error("Cannot create NAND default " + path);
        Handle existing(WindowsFile::Open(parent.value, leaf, FILE_READ_EA | FILE_WRITE_EA, Kind::File));
        BY_HANDLE_FILE_INFORMATION info{};
        if (!existing || !Info(existing.value, info)) throw std::runtime_error("Cannot inspect NAND default " + path);
        EnsureMetadata(existing.value); return;
    }
    try {
        EnsureMetadata(file.value);
        size_t done = 0;
        while (done < bytes.size()) {
            DWORD count = 0;
            if (!WriteFile(file.value, bytes.data() + done, static_cast<DWORD>(bytes.size() - done), &count, nullptr) || !count)
                throw std::runtime_error("Cannot write NAND default " + path);
            done += count;
        }
        if (!FlushFileBuffers(file.value)) throw std::runtime_error("Cannot flush NAND default " + path);
    } catch (...) { WindowsFile::Remove(file.value); throw; }
}
int32_t CountDirectory(HANDLE file, uint64_t& clusters, uint64_t& inodes, unsigned depth) {
    if (depth > 32 || ++inodes > UINT32_MAX) return -108;
    alignas(FILE_ID_BOTH_DIR_INFO) std::array<uint8_t,65536> buffer{};
    bool restart = true;
    for (;;) {
        if (!GetFileInformationByHandleEx(file, restart ? FileIdBothDirectoryRestartInfo : FileIdBothDirectoryInfo,
                                         buffer.data(), static_cast<DWORD>(buffer.size())))
            return GetLastError() == ERROR_NO_MORE_FILES ? 0 : Error();
        restart = false;
        auto* entry = reinterpret_cast<FILE_ID_BOTH_DIR_INFO*>(buffer.data());
        for (;;) {
            const std::wstring name(entry->FileName, entry->FileNameLength / sizeof(wchar_t));
            if (name != L"." && name != L"..") {
                Handle child(WindowsFile::Open(file, name, FILE_LIST_DIRECTORY, Kind::Any));
                if (!child) return Error();
                BY_HANDLE_FILE_INFORMATION info{};
                if (!Info(child.value, info)) return Error();
                if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    const auto result = CountDirectory(child.value, clusters, inodes, depth + 1);
                    if (result) return result;
                } else {
                    clusters += ((uint64_t(info.nFileSizeHigh) << 32 | info.nFileSizeLow) + 0x3FFF) / 0x4000;
                    if (++inodes > UINT32_MAX || clusters > UINT32_MAX) return -108;
                }
            }
            if (!entry->NextEntryOffset) break;
            entry = reinterpret_cast<FILE_ID_BOTH_DIR_INFO*>(reinterpret_cast<uint8_t*>(entry) + entry->NextEntryOffset);
        }
    }
}
} // namespace

NandStore::NandStore(const std::filesystem::path& root, uint64_t titleId) : titleId_(titleId) {
    char path[64];
    std::snprintf(path, sizeof(path), "/title/%08x/%08x/data", uint32_t(titleId >> 32), uint32_t(titleId));
    dataDirectory_ = path;
    std::filesystem::create_directories(root);
    Handle rootHandle(WindowsFile::Root(root, true));
    if (!rootHandle) throw std::runtime_error("Cannot open local NAND root");
    EnsureMetadata(rootHandle.value);
    std::wstring leaf;
    Handle directory(Parent(rootHandle.value, dataDirectory_ + "/placeholder", true, leaf));
    Handle temporary(Parent(rootHandle.value, "/tmp/placeholder", true, leaf));
    if (!directory || !temporary) throw std::runtime_error("Cannot initialize NAND data directories");
    CreateDefault(rootHandle.value, "/shared2/sys/SYSCONF", DefaultSysconf());
    CreateDefault(rootHandle.value, "/title/00000001/00000002/data/setting.txt", DefaultSettings());
    rootFd_ = reinterpret_cast<intptr_t>(rootHandle.release());
}
NandStore::~NandStore() { if (rootFd_ != -1) CloseHandle(Native(rootFd_)); }
std::pair<int32_t, std::shared_ptr<NandFile>> NandStore::Open(const std::string& path, uint32_t mode) const {
    if (!ValidPath(path) || mode < 1 || mode > 3) return {-101, {}};
    std::wstring leaf;
    Handle parent(Parent(Native(rootFd_), path, false, leaf));
    if (!parent) return {Error(), {}};
    Handle file(WindowsFile::Open(parent.value, leaf, ((mode & 1) ? FILE_READ_DATA : 0) |
                                 ((mode & 2) ? FILE_WRITE_DATA : 0), Kind::Any));
    if (!file) return {Error(), {}};
    BY_HANDLE_FILE_INFORMATION info{};
    if (!Info(file.value, info)) return {Error(), {}};
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || info.nFileSizeHigh || info.nFileSizeLow > INT32_MAX)
        return {-101, {}};
    auto result = std::make_shared<NandFile>(reinterpret_cast<intptr_t>(file.value), mode);
    file.release();
    std::erase_if(opened_, [](const auto& entry) { return entry.second.expired(); });
    opened_.emplace_back(path, result);
    return {0, std::move(result)};
}
int32_t NandStore::Usage(const std::string& path, uint32_t& clusters, uint32_t& inodes) const {
    if (path != "/" && !ValidPath(path)) return -101;
    std::wstring leaf;
    Handle parent(path == "/" ? WindowsFile::Duplicate(Native(rootFd_)) : Parent(Native(rootFd_), path, false, leaf));
    if (!parent) return Error();
    Handle directory(path == "/" ? WindowsFile::Duplicate(parent.value) :
                     WindowsFile::Open(parent.value, leaf, FILE_LIST_DIRECTORY, Kind::Directory));
    if (!directory) return Error();
    uint64_t blocks = 0, nodes = 0;
    const auto result = CountDirectory(directory.value, blocks, nodes, 0);
    if (!result) { clusters = static_cast<uint32_t>(blocks); inodes = static_cast<uint32_t>(nodes); }
    return result;
}
int32_t NandStore::CreateFile(const std::string& path, std::array<uint8_t,4> metadata) const { return Create(path, metadata, false); }
int32_t NandStore::CreateDirectory(const std::string& path, std::array<uint8_t,4> metadata) const { return Create(path, metadata, true); }
int32_t NandStore::Create(const std::string& path, std::array<uint8_t,4> metadata, bool directory) const {
    if (!ValidPath(path) || metadata[0] > 3 || metadata[1] > 3 || metadata[2] > 3) return -101;
    std::wstring leaf;
    Handle parent(Parent(Native(rootFd_), path, false, leaf));
    if (!parent) return Error();
    if (path.size() - path.rfind('/') - 1 > 12) return -101;
    Handle node(WindowsFile::Open(parent.value, leaf, FILE_WRITE_EA | DELETE,
                                 directory ? Kind::Directory : Kind::File, 2));
    if (!node) return Error();
    if (!SetMetadata(node.value, metadata)) { const auto result = Error(); WindowsFile::Remove(node.value); return result; }
    return 0;
}
int32_t NandStore::Attributes(const std::string& path, std::array<uint8_t,4>& metadata) const {
    if (path != "/" && !ValidPath(path)) return -101;
    std::wstring leaf;
    Handle parent(path == "/" ? WindowsFile::Duplicate(Native(rootFd_)) : Parent(Native(rootFd_), path, false, leaf));
    if (!parent) return Error();
    Handle node(path == "/" ? WindowsFile::Duplicate(parent.value) : WindowsFile::Open(parent.value, leaf, FILE_READ_EA));
    if (!node) return Error();
    BY_HANDLE_FILE_INFORMATION info{};
    if (!Info(node.value, info)) return Error();
    std::array<uint8_t,4> value{};
    bool missing = false;
    if (!GetMetadata(node.value, value, missing)) return -103;
    metadata = value; return 0;
}
int32_t NandStore::Rename(const std::string& source, const std::string& destination) const {
    if (!ValidPath(source) || !ValidPath(destination)) return -101;
    std::wstring oldLeaf, newLeaf;
    Handle oldParent(Parent(Native(rootFd_), source, false, oldLeaf));
    if (!oldParent) return Error();
    Handle newParent(Parent(Native(rootFd_), destination, false, newLeaf));
    if (!newParent) return Error();
    if (destination.size() - destination.rfind('/') - 1 > 12) return -101;
    Handle old(WindowsFile::Open(oldParent.value, oldLeaf, DELETE));
    if (!old) return Error();
    BY_HANDLE_FILE_INFORMATION oldInfo{};
    if (!Info(old.value, oldInfo)) return Error();
    const bool directory = oldInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
    if (!directory && oldLeaf != newLeaf) return -101;
    for (const auto& [path, weak] : opened_) if (const auto file = weak.lock()) {
        BY_HANDLE_FILE_INFORMATION info{};
        if (!Info(Native(file->fd_), info)) return Error();
        if ((info.dwVolumeSerialNumber == oldInfo.dwVolumeSerialNumber && info.nFileIndexHigh == oldInfo.nFileIndexHigh &&
             info.nFileIndexLow == oldInfo.nFileIndexLow) || (directory && path.starts_with(source + "/"))) return -111;
    }
    Handle target(WindowsFile::Open(newParent.value, newLeaf, FILE_READ_ATTRIBUTES));
    if (target) {
        BY_HANDLE_FILE_INFORMATION info{};
        if (!Info(target.value, info)) return Error();
        if (directory != bool(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return -101;
    } else if (Error() != -106) return Error();
    if (WindowsFile::Rename(old.value, newParent.value, newLeaf, true)) return 0;
    if (GetLastError() == ERROR_DIR_NOT_EMPTY)
        throw std::runtime_error("NAND rename replacement of a nonempty directory requires recursive deletion");
    return Error();
}
int32_t NandStore::Delete(const std::string& path) const {
    if (!ValidPath(path)) return -101;
    std::wstring leaf;
    Handle parent(Parent(Native(rootFd_), path, false, leaf));
    if (!parent) return Error();
    Handle file(WindowsFile::Open(parent.value, leaf, DELETE));
    if (!file) return Error();
    BY_HANDLE_FILE_INFORMATION info{};
    if (!Info(file.value, info)) return Error();
    if (WindowsFile::Remove(file.value)) return 0;
    if (GetLastError() == ERROR_DIR_NOT_EMPTY) throw std::runtime_error("Recursive NAND directory deletion is not implemented: " + path);
    return Error();
}
NandFile::~NandFile() { CloseHandle(Native(fd_)); }
int32_t NandFile::Read(std::span<uint8_t> output) {
    if (!(mode_ & 1)) return -102;
    if (output.size() > INT32_MAX) return -101;
    DWORD count = 0;
    if (!ReadFile(Native(fd_), output.data(), static_cast<DWORD>(output.size()), &count, nullptr)) return Error();
    return static_cast<int32_t>(count);
}
int32_t NandFile::Write(std::span<const uint8_t> input) {
    if (!(mode_ & 2)) return -102;
    uint32_t length, offset;
    const auto status = Stat(length, offset);
    if (status) return status;
    if (uint64_t(offset) + input.size() > INT32_MAX) return -101;
    DWORD count = 0;
    if (!WriteFile(Native(fd_), input.data(), static_cast<DWORD>(input.size()), &count, nullptr)) return Error();
    return static_cast<int32_t>(count);
}
int32_t NandFile::Stat(uint32_t& length, uint32_t& offset) const {
    LARGE_INTEGER size{}, position{}, zero{};
    if (!GetFileSizeEx(Native(fd_), &size) || !SetFilePointerEx(Native(fd_), zero, &position, FILE_CURRENT)) return Error();
    if (size.QuadPart > INT32_MAX || position.QuadPart > INT32_MAX) return -101;
    length = static_cast<uint32_t>(size.QuadPart); offset = static_cast<uint32_t>(position.QuadPart); return 0;
}
int32_t NandFile::Seek(int32_t offset, uint32_t origin) {
    uint32_t length, current;
    const auto status = Stat(length, current);
    if (status) return status;
    if (origin > 2) return -101;
    const int64_t target = int64_t(offset) + (origin == 0 ? 0 : origin == 1 ? current : length);
    if (target < 0 || target > length) return -101;
    LARGE_INTEGER position{}; position.QuadPart = target;
    if (!SetFilePointerEx(Native(fd_), position, nullptr, FILE_BEGIN)) return Error();
    return static_cast<int32_t>(target);
}
