// SPDX-License-Identifier: GPL-3.0-only
// DI command semantics reference Dolphin IOS/DI and Swiftshine/key's DVD SDK.
#include "kirby/services/disc.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <set>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace Kirby {
namespace {
struct Fd {
    int value;
    explicit Fd(int fd) : value(fd) {}
    ~Fd() { if (value >= 0) close(value); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
};
uint32_t Be(const uint8_t* p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
void Put(uint8_t* p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = value >> (24 - i * 8);
}
int Open(int root, const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute()) throw std::runtime_error("Invalid extracted disc path");
    Fd directory(dup(root));
    if (directory.value < 0) throw std::runtime_error("Cannot duplicate disc directory descriptor");
    for (auto it = path.begin(); it != path.end(); ++it) {
        const auto name = it->string();
        if (name.empty() || name == "." || name == "..")
            throw std::runtime_error("Invalid extracted disc path component");
        const bool last = std::next(it) == path.end();
        const auto fd = openat(directory.value, name.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW |
                               (last ? O_NONBLOCK : O_DIRECTORY));
        if (fd < 0) throw std::runtime_error("Cannot open extracted disc file: " + path.string());
        if (last) return fd;
        close(directory.value);
        directory.value = fd;
    }
    throw std::runtime_error("Empty extracted disc path");
}
uint64_t Size(int fd) {
    struct stat info{};
    if (fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_size < 0)
        throw std::runtime_error("Extracted disc entry is not a regular file");
    return info.st_size;
}
void ReadFile(int fd, uint64_t offset, std::span<uint8_t> output) {
    size_t done = 0;
    while (done < output.size()) {
        const auto count = pread(fd, output.data() + done, output.size() - done, offset + done);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) throw std::runtime_error("Extracted disc file read failed or became truncated");
        done += count;
    }
}
std::vector<uint8_t> Load(int root, const char* path, size_t limit) {
    Fd fd(Open(root, path));
    const auto size = Size(fd.value);
    if (size > limit) throw std::runtime_error("Oversized disc metadata");
    std::vector<uint8_t> bytes(size);
    ReadFile(fd.value, 0, bytes);
    return bytes;
}
}

void ExtractedDisc::Add(uint64_t start, const std::filesystem::path& path, uint64_t expectedSize) {
    Fd fd(Open(rootFd_, path));
    const auto size = Size(fd.value);
    if (size != expectedSize) throw std::runtime_error("Extracted disc size mismatch: " + path.string());
    if (start > capacity_ || size > capacity_ - start)
        throw std::runtime_error("Extracted disc extent exceeds configured disc capacity");
    if (size) extents_.push_back({start, size, (start + size + 31) & ~uint64_t(31), path});
}

ExtractedDisc::ExtractedDisc(const std::filesystem::path& root, uint64_t capacity) : capacity_(capacity) {
    if (!capacity) throw std::runtime_error("Disc capacity must be supplied by the verified game profile");
    rootFd_ = open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (rootFd_ < 0) throw std::runtime_error("Cannot open extracted DATA directory");
    try {
        const auto boot = Load(rootFd_, "sys/boot.bin", 0x440);
        const auto fst = Load(rootFd_, "sys/fst.bin", 32 * 1024 * 1024);
        if (boot.size() != 0x440 || fst.size() < 12 || Be(boot.data()+0x18) != 0x5D1C9EA3 ||
            uint64_t(Be(boot.data()+0x428))*4 != fst.size())
            throw std::runtime_error("Invalid Wii disc header or FST size");
        Add(0, "sys/boot.bin", 0x440);
        Add(0x440, "sys/bi2.bin", 0x2000);
        for (const auto& [start, path] : std::vector<std::pair<uint64_t, const char*>>{
                 {0x2440, "sys/apploader.img"}, {uint64_t(Be(boot.data()+0x420))*4, "sys/main.dol"}}) {
            Fd fd(Open(rootFd_, path));
            Add(start, path, Size(fd.value));
        }
        Add(uint64_t(Be(boot.data()+0x424))*4, "sys/fst.bin", fst.size());
        const auto nodes = Be(fst.data()+8);
        if (!nodes || uint64_t(nodes)*12 > fst.size() || Be(fst.data()) != 0x01000000 || Be(fst.data()+4))
            throw std::runtime_error("Invalid FST root");
        struct Directory { uint32_t index, end; std::filesystem::path path; };
        std::vector<Directory> stack{{0, nodes, "files"}};
        std::set<std::filesystem::path> names;
        for (uint32_t i = 1; i < nodes; ++i) {
            while (i == stack.back().end) stack.pop_back();
            const auto* node = fst.data() + i*12;
            const uint64_t nameOffset = uint64_t(nodes)*12 + (Be(node) & 0xFFFFFF);
            if (node[0] > 1 || nameOffset >= fst.size()) throw std::runtime_error("Invalid FST entry");
            const auto* name = reinterpret_cast<const char*>(fst.data()+nameOffset);
            const auto* end = static_cast<const char*>(std::memchr(name, 0, fst.size()-nameOffset));
            if (!end) throw std::runtime_error("Unterminated FST name");
            const std::string component(name, end);
            if (component.empty() || component == "." || component == ".." || component.find('/') != std::string::npos)
                throw std::runtime_error("Invalid FST name");
            const auto path = stack.back().path / component;
            if (!names.insert(path).second) throw std::runtime_error("Duplicate FST path");
            if (node[0]) {
                const auto parent = Be(node+4), last = Be(node+8);
                if (parent != stack.back().index || last <= i || last > stack.back().end)
                    throw std::runtime_error("Invalid FST directory bounds");
                stack.push_back({i, last, path});
            } else {
                Add(uint64_t(Be(node+4))*4, path, Be(node+8));
                ++fileCount_;
            }
        }
        std::sort(extents_.begin(), extents_.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
        for (size_t i = 1; i < extents_.size(); ++i) {
            auto& previous = extents_[i-1];
            if (previous.start + previous.size > extents_[i].start)
                throw std::runtime_error("Overlapping extracted disc extents");
            previous.paddedEnd = std::min(previous.paddedEnd, extents_[i].start);
        }
    } catch (...) { close(rootFd_); rootFd_ = -1; throw; }
}
ExtractedDisc::~ExtractedDisc() { if (rootFd_ >= 0) close(rootFd_); }

void ExtractedDisc::Read(uint64_t offset, std::span<uint8_t> output) {
    if (output.empty()) return;
    if (offset > UINT64_MAX - output.size()) throw std::runtime_error("Disc read offset overflow");
    // Stage the result so a hole, changed file, or short read cannot partially
    // overwrite guest memory. Padding is limited to the next DMA boundary.
    std::vector<uint8_t> bytes(output.size());
    size_t done = 0;
    while (done < bytes.size()) {
        const auto position = offset + done;
        auto it = std::upper_bound(extents_.begin(), extents_.end(), position,
                                  [](uint64_t at, const Extent& extent) { return at < extent.start; });
        if (it == extents_.begin() || position >= std::prev(it)->paddedEnd) {
            std::ostringstream error;
            error << "Unbacked extracted disc read at byte 0x" << std::hex << position;
            throw std::runtime_error(error.str());
        }
        --it;
        const auto length = std::min<uint64_t>(bytes.size()-done, it->paddedEnd-position);
        if (position < it->start + it->size) {
            Fd fd(Open(rootFd_, it->path));
            if (Size(fd.value) != it->size) throw std::runtime_error("Extracted disc file size changed");
            const auto backed = std::min(length, it->start + it->size - position);
            ReadFile(fd.value, position-it->start, std::span(bytes).subspan(done, backed));
        }
        done += length;
    }
    std::copy(bytes.begin(), bytes.end(), output.begin());
    ++reads_;
    bytesRead_ += output.size();
}

int32_t ExtractedDisc::Ioctl(uint32_t command, std::span<const uint8_t> input,
                           std::span<uint8_t> output, uint32_t outputAddress) {
    if (input.size() != 32) return 32; // DI security error.
    // IOS uses the ioctl number, even if the command byte disagrees.
    if (command == 0x12 || command == 0x70) {
        if (output.size() < 32) return 16;
        if (outputAddress & 31) return 128;
        driveError_ = 0;
        if (command == 0x70) Read(0, output.first(32));
        else {
            std::fill_n(output.begin(), 32, 0);
            Put(output.data(), 2); Put(output.data()+4, 0x20060526); Put(output.data()+8, 0x41000000);
        }
        return 1; // Transfer complete, not IPC success (0).
    }
    if (command == 0x71) {
        const auto length = Be(input.data()+4);
        if (output.size() < length) return 32;
        if ((outputAddress | length) & 31) return 128;
        driveError_ = 0;
        Read(uint64_t(Be(input.data()+8))*4, output.first(length));
        return 1;
    }
    if (command == 0x8D) {
        const auto length = Be(input.data()+4), words = Be(input.data()+8);
        const uint64_t end = uint64_t(words) + (length >> 2);
        const bool permitted = end <= 0x14000 ||
            (words >= 0x460A0000 && end <= 0x460A0008) ||
            (words >= 0x7ED40000 && end <= 0x7ED40008);
        if (!permitted) return 32; // IOS56 raw-read whitelist, independent of partition data.
        if (output.size() < length) return 16;
        if ((outputAddress | length) & 31) return 128;
        driveError_ = 0;
        if (uint64_t(words)*4 + length > capacity_) {
            driveError_ = 0x52100; // Logical block address out of bounds.
            return 2;
        }
        // In-bounds raw reads need raw disc extents, never the DATA FST.
    }
    if (command == 0xA4) {
        if (output.size() < 32) return 16;
        if (outputAddress & 31) return 128;
        driveError_ = 0x52000; // REPORT KEY is invalid on the retail Wii drive.
        return 2;
    }
    if (command == 0xE0) {
        if (output.size() >= 4) Put(output.data(), driveError_);
        driveError_ = 0;
        return 1;
    }
    if (command == 0x7A || command == 0x88) {
        if (output.size() < 4) return 1; // IOS WriteIfFits semantics.
        Put(output.data(), command == 0x88 ? 2 : 0); // Mounted, cover closed, no cover interrupt.
        return 1;
    }
    if (command == 0x86) return 1; // Acknowledge an already-clear cover interrupt.
    std::ostringstream error;
    error << "Unimplemented DI ioctl 0x" << std::hex << command
          << " arg1=0x" << Be(input.data()+4) << " arg2=0x" << Be(input.data()+8)
          << " output=0x" << outputAddress << " size=0x" << output.size();
    throw std::runtime_error(error.str());
}
} // namespace Kirby
