// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace Kirby {
// Decrypted DATA partition, already selected by the native boot loader.
// Only extracted extents and their bounded DMA padding are readable.
class ExtractedDisc {
public:
    ExtractedDisc(const std::filesystem::path& root, uint64_t capacity);
    ~ExtractedDisc();
    ExtractedDisc(const ExtractedDisc&) = delete;
    ExtractedDisc& operator=(const ExtractedDisc&) = delete;
    void Read(uint64_t offset, std::span<uint8_t> output);
    int32_t Ioctl(uint32_t command, std::span<const uint8_t> input,
                  std::span<uint8_t> output, uint32_t outputAddress);
    size_t FileCount() const { return fileCount_; }
    uint64_t ReadCount() const { return reads_; }
    uint64_t BytesRead() const { return bytesRead_; }
private:
    struct Extent { uint64_t start, size, paddedEnd; std::filesystem::path path; };
    int rootFd_ = -1;
    std::vector<Extent> extents_;
    size_t fileCount_ = 0;
    uint64_t reads_ = 0, bytesRead_ = 0;
    uint64_t capacity_;
    uint32_t driveError_ = 0;
    void Add(uint64_t start, const std::filesystem::path& path, uint64_t expectedSize);
};
} // namespace Kirby
