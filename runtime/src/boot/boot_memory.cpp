// SPDX-License-Identifier: GPL-3.0-only
// IOS56 layout: Dolphin IOS/VersionInfo.cpp and IOS.cpp (see THIRD_PARTY.md).
// Boot fields: Swiftshine/key revolution/OS/OSHardware.h.
#include "kirby/boot/boot_memory.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
#include <string>

namespace Kirby {
namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
uint32_t Word(const std::vector<uint8_t>& b, size_t p) {
    Require(p <= b.size() && b.size() - p >= 4, "Truncated boot metadata word");
    return (uint32_t(b[p]) << 24) | (uint32_t(b[p+1]) << 16) | (uint32_t(b[p+2]) << 8) | b[p+3];
}
std::vector<uint8_t> Read(const std::filesystem::path& path, size_t limit) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto size = file.tellg();
    if (!file || size < 0 || uint64_t(size) > limit)
        throw std::runtime_error("Cannot read bounded boot metadata: " + path.string());
    std::vector<uint8_t> result(static_cast<size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(result.data()), result.size()))
        throw std::runtime_error("Truncated boot metadata: " + path.string());
    return result;
}
void ValidateFst(const std::vector<uint8_t>& fst) {
    Require(fst.size() >= 12 && Word(fst, 0) == 0x01000000 && Word(fst, 4) == 0,
            "Invalid FST root");
    const uint32_t count = Word(fst, 8);
    Require(count && uint64_t(count) * 12 < fst.size(), "FST node table exceeds file");
    const size_t strings = size_t(count) * 12;
    std::vector<std::pair<uint32_t, uint32_t>> directories{{0, count}};
    for (uint32_t i = 1; i < count; ++i) {
        while (i == directories.back().second) directories.pop_back();
        const uint32_t name = Word(fst, i * 12), type = name >> 24;
        const size_t start = strings + (name & 0xFFFFFF);
        Require(type <= 1 && start < fst.size() &&
                std::find(fst.begin() + start, fst.end(), 0) != fst.end(), "Invalid FST name/type");
        if (type) {
            const uint32_t parent = Word(fst, i * 12 + 4), end = Word(fst, i * 12 + 8);
            Require(parent == directories.back().first && end > i && end <= directories.back().second,
                    "Invalid FST directory bounds");
            directories.emplace_back(i, end);
        }
    }
}
}
BootData ReadBootData(const std::filesystem::path& root) {
    return {Read(root / "sys/boot.bin", 0x440), Read(root / "sys/bi2.bin", 0x2000),
            Read(root / "sys/fst.bin", 0x100000), Read(root / "tmd.bin", 0x100000),
            Read(root / "sys/apploader.img", 0x100000)};
}
BootLayout InstallBootData(const BootData& b, uint32_t protectedEnd, const BootWriter& write) {
    Require(b.boot.size() == 0x440 && std::equal(b.boot.begin(), b.boot.begin() + 6, "RK5E01") &&
            b.boot[6] == 0 && b.boot[7] == 0 && Word(b.boot, 0x18) == 0x5D1C9EA3,
            "Boot metadata is not Wii RK5E01 Rev 0, disc 0");
    Require(b.bi2.size() == 0x2000, "BI2 must contain exactly 8192 bytes");
    // The supported disc uses an RSA-2048 TMD header. This checks format and
    // compatibility, not cryptographic authenticity. No ticket keys are read.
    Require(b.tmd.size() >= 0x1E4 && Word(b.tmd, 0) == 0x10001 &&
            Word(b.tmd, 0x184) == 1 && Word(b.tmd, 0x188) == 56 &&
            Word(b.tmd, 0x18C) == 0x10000 && Word(b.tmd, 0x190) == 0x524B3545,
            "TMD must request IOS56 for RK5E");
    // The apploader supplies MINIMUM_IOS_VERSION, which OSInit compares with
    // IOS_VERSION before continuing. Read its actual initialization sequence;
    // do not weaken the game's check or infer a minimum revision from the TMD.
    Require(b.apploader.size() >= 32, "Missing apploader header");
    const auto codeSize = Word(b.apploader, 20), trailerSize = Word(b.apploader, 24);
    const auto entry = Word(b.apploader, 16);
    Require(!(codeSize & 3) && uint64_t(codeSize) + trailerSize + 32 == b.apploader.size() &&
            entry >= 0x81200000 && uint64_t(entry) < 0x81200000ull + codeSize,
            "Invalid apploader section sizes or entry");
    uint32_t minimumIos = 0, matches = 0;
    for (uint32_t offset = 32; uint64_t(offset) + 16 <= uint64_t(codeSize) + 32; offset += 4) {
        const auto upper = Word(b.apploader, offset), lower = Word(b.apploader, offset+8);
        // lis r0,hi; lis r3,0x8000; ori r0,r0,lo; stw r0,0x3188(r3).
        if ((upper & 0xFFFF0000) == 0x3C000000 && Word(b.apploader, offset+4) == 0x3C608000 &&
            (lower & 0xFFFF0000) == 0x60000000 && Word(b.apploader, offset+12) == 0x90033188) {
            minimumIos = ((upper & 0xFFFF) << 16) | (lower & 0xFFFF);
            ++matches;
        }
    }
    Require(matches == 1 && (minimumIos >> 16) == Word(b.tmd, 0x188) && minimumIos <= 0x0038161E,
            "Apploader IOS requirement is missing, ambiguous or unsupported by the IOS56 profile");
    const uint64_t fstSize = uint64_t(Word(b.boot, 0x428)) * 4;
    const uint64_t fstCapacity = uint64_t(Word(b.boot, 0x42C)) * 4;
    Require(fstSize == b.fst.size() && fstCapacity >= fstSize && fstCapacity <= 0x100000,
            "FST length/capacity disagrees with the Wii disc header");
    ValidateFst(b.fst);
    const auto fst = uint32_t((0x81800000ull - fstCapacity) & ~31ull);
    const auto bi2 = fst - 0x2000;
    Require(protectedEnd >= 0x80004000 && protectedEnd <= bi2,
            "Boot metadata would overlap the verified program or reserved stack");

    // All validation precedes guest writes. Never zero DOL sections or BSS here.
    const std::array<uint8_t, 0x4000> zero{};
    write(0x80000000, zero);
    write(0x80000000, std::span(b.boot).first(32));
    write(bi2, b.bi2); write(fst, b.fst);
    const auto put = [&](uint32_t offset, uint32_t value) {
        std::array<uint8_t, 4> bytes{};
        for (unsigned i = 0; i < 4; ++i) bytes[i] = value >> (24 - i * 8);
        write(0x80000000 + offset, bytes);
    };
    put(0x20, 0x0D15EA5E); put(0x24, 1); put(0x28, Mem1Size); put(0x2C, 0x23);
    put(0x34, bi2); put(0x38, fst); put(0x3C, uint32_t(fstCapacity));
    put(0xF0, Mem1Size); put(0xF4, bi2); put(0xF8, BusClock); put(0xFC, CpuClock);
    put(0x3100, Mem1Size); put(0x3104, Mem1Size); put(0x3108, 0x81800000);
    // Leave arena lo zero so Kirby chooses its own linker-defined start.
    put(0x3110, bi2);
    put(0x3118, Mem2Size); put(0x311C, Mem2Size); put(0x3120, 0x93600000);
    put(0x3124, 0x90000800); put(0x3128, 0x935E0000);
    put(0x3130, 0x935E0000); put(0x3134, 0x93600000);
    put(0x3138, 0x11); put(0x3140, 0x0038161E); put(0x3144, 0x030110);
    put(0x3148, 0x93600000); put(0x314C, 0x93620000); put(0x3158, 0xFF01);
    put(0x315C, 0x80000113); put(0x3184, 0x80000000);
    put(0x3188, minimumIos);
    return {bi2, bi2, fst, uint64_t(Word(b.tmd, 0x18C)) << 32 | Word(b.tmd, 0x190)};
}
}
