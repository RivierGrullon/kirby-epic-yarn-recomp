// SPDX-License-Identifier: GPL-3.0-only
// Native local NAND storage. SYSCONF/settings wire formats reference Dolphin;
// see THIRD_PARTY.md. Default values are synthetic, not a console dump.
#include "kirby/services/nand.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <fcntl.h>
#include <dirent.h>
#include <stdexcept>
#include <string_view>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>
#include <vector>

namespace Kirby {
namespace {
struct Fd {
    int value;
    ~Fd() { if (value >= 0) close(value); }
};
int32_t Error() {
    switch (errno) {
    case ENOENT: return -106;
    case ELOOP: case ENOTDIR: case EACCES: case EPERM: return -102;
    case EINVAL: case EBADF: return -101;
    case ENOSPC: return -108;
    default: return -103;
    }
}
bool ValidPath(std::string_view path) {
    if (path.size() < 2 || path.size() >= 64 || path[0] != '/' || path.back() == '/') return false;
    size_t start = 1;
    while (start < path.size()) {
        const auto end = path.find('/', start);
        const auto part = path.substr(start, end == path.npos ? path.size() - start : end - start);
        if (part.empty() || part == "." || part == ".." || part.find('\0') != part.npos) return false;
        if (end == path.npos) break;
        start = end + 1;
    }
    return true;
}
void EnsureMetadata(int fd) {
    std::array<uint8_t,4> metadata{};
    const auto size=fgetxattr(fd,"user.kirby.isfs",metadata.data(),metadata.size());
    if (size>=0) {
        if (size!=4 || metadata[0]>3 || metadata[1]>3 || metadata[2]>3)
            throw std::runtime_error("Invalid local NAND permission metadata");
        return;
    }
    if (errno!=ENODATA) throw std::runtime_error("Cannot read local NAND permission metadata");
    // The private single-title filesystem runs IOS requests as logical UID/GID 0.
    const std::array<uint8_t,4> defaults{3,3,3,0};
    if (fsetxattr(fd,"user.kirby.isfs",defaults.data(),defaults.size(),XATTR_CREATE)<0 && errno!=EEXIST)
        throw std::runtime_error("Cannot initialize local NAND permission metadata");
}
// Walk relative to an owned directory descriptor. No component may follow a
// host symlink, including files created while initializing console settings.
int Parent(int root, const std::string& path, bool create, std::string& leaf) {
    int dir = dup(root);
    if (dir < 0) return -1;
    size_t start = 1;
    for (;;) {
        const auto end = path.find('/', start);
        if (end == path.npos) { leaf = path.substr(start); return dir; }
        const auto part = path.substr(start, end-start);
        if (create && mkdirat(dir, part.c_str(), 0700) < 0 && errno != EEXIST) {
            const auto error = errno; close(dir); errno = error; return -1;
        }
        const auto next = openat(dir, part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        const auto error = errno;
        close(dir);
        if (next < 0) { errno = error; return -1; }
        dir = next;
        if (create) {
            try { EnsureMetadata(dir); } catch (...) { close(dir); throw; }
        }
        start = end + 1;
    }
}
int32_t CountDirectory(int fd, uint64_t& clusters, uint64_t& inodes, unsigned depth) {
    if (depth > 32 || ++inodes > UINT32_MAX) return -108;
    // A fresh open description gives each query an independent directory cursor.
    const int copy = openat(fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (copy < 0) return Error();
    std::unique_ptr<DIR, decltype(&closedir)> dir(fdopendir(copy), closedir);
    if (!dir) { const auto error = Error(); close(copy); return error; }
    for (;;) {
        errno = 0;
        const auto* entry = readdir(dir.get());
        if (!entry) return errno ? Error() : 0;
        if (!std::strcmp(entry->d_name, ".") || !std::strcmp(entry->d_name, "..")) continue;
        struct stat st{};
        if (fstatat(fd, entry->d_name, &st, AT_SYMLINK_NOFOLLOW) < 0) return Error();
        if (S_ISDIR(st.st_mode)) {
            Fd child{openat(fd, entry->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
            if (child.value < 0) return Error();
            const auto result = CountDirectory(child.value, clusters, inodes, depth+1);
            if (result) return result;
        } else if (S_ISREG(st.st_mode)) {
            clusters += (uint64_t(st.st_size) + 0x3FFF) / 0x4000;
            if (++inodes > UINT32_MAX || clusters > UINT32_MAX) return -108;
        } else return -102;
    }
}
void CreateDefault(int root, const std::string& path, std::span<const uint8_t> bytes) {
    std::string leaf;
    Fd parent{Parent(root, path, true, leaf)};
    if (parent.value < 0) throw std::runtime_error("Cannot create NAND directory for " + path);
    Fd file{openat(parent.value, leaf.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600)};
    if (file.value < 0) {
        struct stat st{};
        if (errno == EEXIST && fstatat(parent.value, leaf.c_str(), &st, AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(st.st_mode)) {
            Fd existing{openat(parent.value,leaf.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC)};
            if (existing.value<0) throw std::runtime_error("Cannot inspect existing NAND configuration");
            EnsureMetadata(existing.value);
            return; // Preserve configuration bytes and existing permission metadata.
        }
        throw std::runtime_error("Cannot create NAND default " + path);
    }
    try { EnsureMetadata(file.value); } catch (...) {
        unlinkat(parent.value,leaf.c_str(),0); throw;
    }
    size_t done = 0;
    while (done < bytes.size()) {
        const auto n = write(file.value, bytes.data()+done, bytes.size()-done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            unlinkat(parent.value, leaf.c_str(), 0);
            throw std::runtime_error("Cannot write NAND default " + path);
        }
        done += n;
    }
}
std::vector<uint8_t> DefaultSysconf() {
    struct Entry { const char* name; uint8_t type; std::vector<uint8_t> value; };
    const std::array entries{
        Entry{"IPL.LNG",3,{1}}, Entry{"IPL.AR",3,{1}}, Entry{"IPL.PGS",3,{1}},
        Entry{"IPL.E60",3,{0}}, Entry{"IPL.SND",3,{1}}, Entry{"IPL.SSV",3,{0}},
        Entry{"IPL.CB",5,{0,0,0,0}}, Entry{"BT.SENS",5,{0,0,0,3}},
        Entry{"BT.BAR",3,{1}}, Entry{"BT.SPKV",3,{0x58}}, Entry{"BT.MOT",3,{1}}
    };
    std::vector<uint8_t> out(0x4000);
    std::memcpy(out.data(), "SCv0", 4);
    auto put16 = [&](size_t pos, uint16_t value) { out[pos]=value>>8; out[pos+1]=value; };
    put16(4, entries.size());
    size_t pos = 6 + 2*(entries.size()+1), index = 0;
    for (const auto& entry : entries) {
        put16(6+2*index++, pos);
        const auto length = std::strlen(entry.name);
        out[pos++] = entry.type << 5 | (length-1);
        std::memcpy(out.data()+pos, entry.name, length); pos += length;
        for (auto byte : entry.value) out[pos++] = byte;
    }
    put16(6+2*entries.size(), pos);
    std::memcpy(out.data()+out.size()-4, "SCed", 4);
    return out;
}
std::array<uint8_t,256> DefaultSettings() {
    std::array<uint8_t,256> out{};
    uint32_t key = 0x73B5DBFA;
    size_t pos = 0;
    const auto encode = [&](std::string_view text, uint32_t& state, std::vector<uint8_t>& bytes) {
        for (const auto c : text) {
            bytes.push_back(uint8_t(c) ^ uint8_t(state));
            state = (state << 1) | (state >> 31);
        }
    };
    for (const auto line : {"AREA=USA\r\n", "MODEL=RVL-001(USA)\r\n", "DVD=0\r\n",
                           "MPCH=0x7FFE\r\n", "CODE=LU\r\n", "SERNO=123456789\r\n",
                           "VIDEO=NTSC\r\n", "GAME=US\r\n"}) {
        // The SDK stops at the first encrypted NUL. Shift a line by LF until
        // every encoded character is nonzero, as in the console format.
        for (;;) {
            auto next = key;
            std::vector<uint8_t> bytes;
            encode(line, next, bytes);
            if (std::find(bytes.begin(), bytes.end(), 0) == bytes.end()) {
                if (pos + bytes.size() >= out.size()) throw std::runtime_error("NAND product settings overflow");
                for (auto byte : bytes) out[pos++] = byte;
                key = next;
                break;
            }
            if (pos >= out.size()-1) throw std::runtime_error("NAND product settings overflow");
            out[pos++] = '\n' ^ uint8_t(key);
            key = (key << 1) | (key >> 31);
        }
    }
    return out;
}
}

NandStore::NandStore(const std::filesystem::path& root, uint64_t titleId) : titleId_(titleId) {
    char path[64];
    std::snprintf(path, sizeof(path), "/title/%08x/%08x/data", uint32_t(titleId >> 32), uint32_t(titleId));
    dataDirectory_ = path;
    std::filesystem::create_directories(root);
    Fd rootFd{open(root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
    if (rootFd.value < 0) throw std::runtime_error("Cannot open local NAND root");
    EnsureMetadata(rootFd.value);
    std::string leaf;
    Fd directory{Parent(rootFd.value, dataDirectory_ + "/placeholder", true, leaf)};
    if (directory.value < 0) throw std::runtime_error("Cannot create NAND title data directory");
    Fd temporary{Parent(rootFd.value,"/tmp/placeholder",true,leaf)};
    if (temporary.value<0) throw std::runtime_error("Cannot initialize NAND temporary directory");
    CreateDefault(rootFd.value, "/shared2/sys/SYSCONF", DefaultSysconf());
    CreateDefault(rootFd.value, "/title/00000001/00000002/data/setting.txt", DefaultSettings());
    rootFd_ = rootFd.value;
    rootFd.value = -1;
}
NandStore::~NandStore() { if (rootFd_ >= 0) close(rootFd_); }

std::pair<int32_t, std::shared_ptr<NandFile>> NandStore::Open(const std::string& path, uint32_t mode) const {
    if (!ValidPath(path) || mode < 1 || mode > 3) return {-101, {}};
    std::string leaf;
    Fd parent{Parent(rootFd_, path, false, leaf)};
    if (parent.value < 0) return {Error(), {}};
    const int flags = mode == 1 ? O_RDONLY : mode == 2 ? O_WRONLY : O_RDWR;
    Fd file{openat(parent.value, leaf.c_str(), flags | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK)};
    if (file.value < 0) return {Error(), {}};
    struct stat st{};
    if (fstat(file.value, &st) < 0) return {Error(), {}};
    if (!S_ISREG(st.st_mode) || st.st_size > INT32_MAX) return {-101, {}};
    auto result = std::make_shared<NandFile>(file.value, mode);
    file.value = -1;
    std::erase_if(opened_,[](const auto& entry) { return entry.second.expired(); });
    opened_.emplace_back(path,result);
    return {0, std::move(result)};
}
int32_t NandStore::Usage(const std::string& path, uint32_t& clusters, uint32_t& inodes) const {
    if (path != "/" && !ValidPath(path)) return -101;
    std::string leaf;
    Fd parent{path == "/" ? dup(rootFd_) : Parent(rootFd_, path, false, leaf)};
    if (parent.value < 0) return Error();
    Fd directory{path == "/" ? dup(parent.value) :
        openat(parent.value, leaf.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
    if (directory.value < 0) return Error();
    uint64_t blocks = 0, nodes = 0;
    const auto result = CountDirectory(directory.value, blocks, nodes, 0);
    if (!result) { clusters = blocks; inodes = nodes; }
    return result;
}
int32_t NandStore::CreateFile(const std::string& path, std::array<uint8_t,4> metadata) const {
    return Create(path,metadata,false);
}
int32_t NandStore::CreateDirectory(const std::string& path, std::array<uint8_t,4> metadata) const {
    return Create(path,metadata,true);
}
int32_t NandStore::Create(const std::string& path, std::array<uint8_t,4> metadata, bool directory) const {
    if (!ValidPath(path) || metadata[0]>3 || metadata[1]>3 || metadata[2]>3) return -101;
    std::string leaf;
    Fd parent{Parent(rootFd_,path,false,leaf)};
    if (parent.value<0) return Error();
    if (leaf.size()>12) return -101;
    if (directory && mkdirat(parent.value,leaf.c_str(),0700)<0) return errno==EEXIST ? -105 : Error();
    Fd node{openat(parent.value,leaf.c_str(), directory ? O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC :
        O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600)};
    if (node.value<0 && !directory) return errno==EEXIST ? -105 : Error();
    if (node.value<0 || fsetxattr(node.value,"user.kirby.isfs",metadata.data(),metadata.size(),0)<0) {
        const auto result=Error(); unlinkat(parent.value,leaf.c_str(),directory ? AT_REMOVEDIR : 0); return result;
    }
    return 0;
}
int32_t NandStore::Attributes(const std::string& path, std::array<uint8_t,4>& metadata) const {
    if (path!="/" && !ValidPath(path)) return -101;
    std::string leaf;
    Fd parent{path=="/" ? dup(rootFd_) : Parent(rootFd_,path,false,leaf)};
    if (parent.value<0) return Error();
    Fd node{path=="/" ? dup(parent.value) :
        openat(parent.value,leaf.c_str(),O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC)};
    if (node.value<0) return Error();
    struct stat st{};
    if (fstat(node.value,&st)<0) return Error();
    if (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode)) return -102;
    std::array<uint8_t,4> value{};
    const auto size=fgetxattr(node.value,"user.kirby.isfs",value.data(),value.size());
    if (size!=4 || value[0]>3 || value[1]>3 || value[2]>3) return -103;
    metadata=value;
    return 0;
}
int32_t NandStore::Rename(const std::string& source, const std::string& destination) const {
    if (!ValidPath(source) || !ValidPath(destination)) return -101;
    std::string oldLeaf,newLeaf;
    Fd oldParent{Parent(rootFd_,source,false,oldLeaf)};
    if (oldParent.value<0) return Error();
    Fd newParent{Parent(rootFd_,destination,false,newLeaf)};
    if (newParent.value<0) return Error();
    if (newLeaf.size()>12) return -101;
    struct stat oldStat{},newStat{};
    if (fstatat(oldParent.value,oldLeaf.c_str(),&oldStat,AT_SYMLINK_NOFOLLOW)<0) return Error();
    if (!S_ISREG(oldStat.st_mode) && !S_ISDIR(oldStat.st_mode)) return -102;
    // IOS moves files between directories but does not change their basename.
    if (S_ISREG(oldStat.st_mode) && oldLeaf!=newLeaf) return -101;
    for (const auto& [path,weak] : opened_) if (const auto file=weak.lock()) {
        struct stat st{};
        if (fstat(file->fd_,&st)<0) return Error();
        if ((st.st_dev==oldStat.st_dev && st.st_ino==oldStat.st_ino) ||
            (S_ISDIR(oldStat.st_mode) && path.starts_with(source+"/"))) return -111;
    }
    if (fstatat(newParent.value,newLeaf.c_str(),&newStat,AT_SYMLINK_NOFOLLOW)==0) {
        if (!S_ISREG(newStat.st_mode) && !S_ISDIR(newStat.st_mode)) return -102;
        if (S_ISDIR(oldStat.st_mode)!=S_ISDIR(newStat.st_mode)) return -101;
    } else if (errno!=ENOENT) return Error();
    if (renameat(oldParent.value,oldLeaf.c_str(),newParent.value,newLeaf.c_str())==0) return 0;
    if (errno==ENOTEMPTY || errno==EEXIST)
        throw std::runtime_error("NAND rename replacement of a nonempty directory requires recursive deletion");
    return Error();
}
int32_t NandStore::Delete(const std::string& path) const {
    if (!ValidPath(path)) return -101;
    std::string leaf;
    Fd parent{Parent(rootFd_, path, false, leaf)};
    if (parent.value < 0) return Error();
    struct stat st{};
    if (fstatat(parent.value, leaf.c_str(), &st, AT_SYMLINK_NOFOLLOW) < 0) return Error();
    if (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode)) return -102;
    if (unlinkat(parent.value, leaf.c_str(), S_ISDIR(st.st_mode) ? AT_REMOVEDIR : 0) == 0) return 0;
    if (errno == ENOTEMPTY)
        throw std::runtime_error("Recursive NAND directory deletion is not implemented: " + path);
    return Error();
}
NandFile::~NandFile() { close(fd_); }
int32_t NandFile::Read(std::span<uint8_t> output) {
    if (!(mode_ & 1)) return -102;
    if (output.size() > INT32_MAX) return -101;
    ssize_t n;
    do { n = read(fd_, output.data(), output.size()); } while (n < 0 && errno == EINTR);
    return n < 0 ? Error() : int32_t(n);
}
int32_t NandFile::Write(std::span<const uint8_t> input) {
    if (!(mode_ & 2)) return -102;
    uint32_t length, offset;
    const auto status = Stat(length, offset);
    if (status) return status;
    if (uint64_t(offset) + input.size() > INT32_MAX) return -101;
    ssize_t n;
    do { n = write(fd_, input.data(), input.size()); } while (n < 0 && errno == EINTR);
    return n < 0 ? Error() : int32_t(n);
}
int32_t NandFile::Stat(uint32_t& length, uint32_t& offset) const {
    struct stat st{};
    if (fstat(fd_, &st) < 0) return Error();
    const auto current = lseek(fd_, 0, SEEK_CUR);
    if (current < 0) return Error();
    if (st.st_size > INT32_MAX || current > INT32_MAX) return -101;
    length = st.st_size; offset = current;
    return 0;
}
int32_t NandFile::Seek(int32_t offset, uint32_t origin) {
    uint32_t length, current;
    const auto status = Stat(length, current);
    if (status) return status;
    if (origin > 2) return -101;
    const int64_t target = int64_t(offset) + (origin == 0 ? 0 : origin == 1 ? current : length);
    if (target < 0 || target > length) return -101;
    const auto result = lseek(fd_, target, SEEK_SET);
    return result < 0 ? Error() : int32_t(result);
}
} // namespace Kirby
