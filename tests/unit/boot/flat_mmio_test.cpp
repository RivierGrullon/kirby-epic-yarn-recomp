// SPDX-License-Identifier: GPL-3.0-only
// Exercise the actual runtime header without the game or a signal handler.
#include "guest_abi/memory_access.h"
#include <iostream>
#include <stdexcept>
#include <sys/mman.h>

namespace {
unsigned slowReads = 0;
uint64_t DeviceRead(uint32_t address, unsigned width) {
    ++slowReads;
    if (address != 0xCD006800) throw std::runtime_error("unsupported device");
    return width == 4 ? 0x3F800000u : 1;
}
}
namespace MemoryInline {
uint8_t Read8Slow(uint32_t a) { return DeviceRead(a, 1); }
uint16_t Read16Slow(uint32_t a) { return DeviceRead(a, 2); }
uint32_t Read32Slow(uint32_t a) { return DeviceRead(a, 4); }
uint64_t Read64Slow(uint32_t a) { return DeviceRead(a, 8); }
}
int main() {
    // A dynamic address reproduces EXISelect's indexed channel-register read.
    volatile uint32_t address = 0xCD006800;
    if (MemoryInline::FlatRead32(address) != 0x3F800000 ||
        MemoryInline::FlatRead16(address) != 1 || MemoryInline::FlatRead8(address) != 1 ||
        MemoryInline::FlatReadFloat32(address) != 1.0f ||
        MemoryInline::FlatLoad<uint64_t>(address) != 1 || slowReads != 5) return 1;
    bool rejected = false;
    try { MemoryInline::FlatRead32(address + 4); }
    catch (const std::runtime_error&) { rejected = true; }
    if (!rejected || slowReads != 6) return 2;
    auto* expected = MKW_FLAT_GUEST_BASE + 0x80000000;
    void* page = mmap(expected, 4096, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (page == MAP_FAILED) return 3;
    MemoryInline::FlatStore<uint32_t>(0x80000000, 0x12345678);
    const bool ramCorrect = MemoryInline::FlatRead32(0x80000000) == 0x12345678 && slowReads == 6 &&
                            expected[0] == 0x12 && expected[1] == 0x34 && expected[2] == 0x56 && expected[3] == 0x78;
    munmap(page, 4096);
    if (!ramCorrect) return 4;
    std::cout << "Flat MMIO reads reach device handlers; RAM stays direct and big endian\n";
}
