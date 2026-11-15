// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <array>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>
#ifdef _WIN32
// Keep the IOS method names independent of Win32's A/W selection macros.
#undef CreateFile
#undef CreateDirectory
#endif

namespace Kirby {
class NandFile {
public:
#ifdef _WIN32
    NandFile(intptr_t fd, uint32_t mode) : fd_(fd), mode_(mode) {}
#else
    NandFile(int fd, uint32_t mode) : fd_(fd), mode_(mode) {}
#endif
    ~NandFile();
    NandFile(const NandFile&) = delete;
    NandFile& operator=(const NandFile&) = delete;
    int32_t Read(std::span<uint8_t> output);
    int32_t Write(std::span<const uint8_t> input);
    int32_t Seek(int32_t offset, uint32_t origin);
    int32_t Stat(uint32_t& length, uint32_t& offset) const;
private:
    friend class NandStore;
#ifdef _WIN32
    intptr_t fd_;
#else
    int fd_;
#endif
    uint32_t mode_;
};

class NandStore {
public:
    NandStore(const std::filesystem::path& root, uint64_t titleId);
    ~NandStore();
    NandStore(const NandStore&) = delete;
    NandStore& operator=(const NandStore&) = delete;
    std::pair<int32_t, std::shared_ptr<NandFile>> Open(const std::string& path, uint32_t mode) const;
    int32_t Usage(const std::string& path, uint32_t& clusters, uint32_t& inodes) const;
    int32_t CreateFile(const std::string& path, std::array<uint8_t,4> metadata) const;
    int32_t CreateDirectory(const std::string& path, std::array<uint8_t,4> metadata) const;
    int32_t Attributes(const std::string& path, std::array<uint8_t,4>& metadata) const;
    int32_t Rename(const std::string& source, const std::string& destination) const;
    int32_t Delete(const std::string& path) const;
    const std::string& DataDirectory() const { return dataDirectory_; }
    uint64_t TitleId() const { return titleId_; }
private:
    int32_t Create(const std::string& path, std::array<uint8_t,4> metadata, bool directory) const;
    mutable std::vector<std::pair<std::string,std::weak_ptr<NandFile>>> opened_;
#ifdef _WIN32
    intptr_t rootFd_ = -1;
#else
    int rootFd_ = -1;
#endif
    uint64_t titleId_;
    std::string dataDirectory_;
};
} // namespace Kirby
