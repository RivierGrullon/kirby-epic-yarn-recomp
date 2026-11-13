// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <vector>

namespace Kirby {
inline constexpr uint32_t Mem1Size = 0x01800000;
inline constexpr uint32_t Mem2Size = 0x04000000;
inline constexpr uint32_t BusClock = 243000000;
inline constexpr uint32_t CpuClock = 729000000;
struct BootData { std::vector<uint8_t> boot, bi2, fst, tmd, apploader; };
struct BootLayout { uint32_t arenaHi, bi2, fst; uint64_t titleId; };
using BootWriter = std::function<void(uint32_t, std::span<const uint8_t>)>;
BootData ReadBootData(const std::filesystem::path& root);
// Native loader policy: reserve aligned FST and BI2 at the top of MEM1.
// The guest allocator must not cover these buffers or the verified program/stack.
BootLayout InstallBootData(const BootData&, uint32_t protectedEnd, const BootWriter&);
}
