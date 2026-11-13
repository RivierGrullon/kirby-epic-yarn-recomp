// SPDX-License-Identifier: GPL-3.0-only
// Register masks and FIFO behavior checked against pinned Dolphin CP/PI sources;
// see THIRD_PARTY.md. This does not reuse game-specific WiiCompiled GX globals.
#include "kirby/graphics/gx_fifo.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace Kirby {
namespace { constexpr uint32_t Cp = 0xCC000000, Pi = 0xCC00300C, Mask = 0x1FFFFFE0; }
bool PixelEngine::Read16(uint32_t address, uint16_t& value) const {
    if (address >= 0xCC001000 && address <= 0xCC001008 && !(address & 1))
        value = config_[(address - 0xCC001000) / 2];
    else if (address == 0xCC00100A) value = control_;
    else if (address == 0xCC00100E) value = token_;
    else return false;
    return true;
}
bool PixelEngine::Write16(uint32_t address, uint16_t value) {
    if (address >= 0xCC001000 && address <= 0xCC001008 && !(address & 1))
        config_[(address - 0xCC001000) / 2] = value;
    else if (address == 0xCC00100A) {
        if (value & 4) tokenPending_ = false;
        if (value & 8) finishPending_ = false;
        control_ = value & 3;
    } else return false;
    return true;
}
void PixelEngine::SetToken(uint16_t token, bool interrupt) {
    token_ = token;
    tokenPending_ |= interrupt;
}
PixelEngine& Pixels() { static PixelEngine engine; return engine; }
void GxFifo::Reset() { *this = GxFifo{}; }
void GxFifo::Configure(Resolver resolve, Consumer consume) {
    resolve_ = resolve;
    consume_ = std::move(consume);
}
void GxFifo::SetWritePipeAddress(uint32_t value) {
    if ((value & ~1u) != 0x0C008000)
        throw std::runtime_error("Unsupported GX write-pipe address in SPR 921");
    writePipeAddress_ = value & ~1u;
}
bool GxFifo::AtBreakpoint() const { return (control_ & 2) && regs_[6] == regs_[7]; }
uint16_t GxFifo::Status() const {
    return (regs_[4] > regs_[2] ? 1 : 0) | (regs_[4] < regs_[3] ? 2 : 0) |
           (!regs_[4] || regs_[5] == regs_[6] ? 4 : 0) |
           (!regs_[4] || AtBreakpoint() || !(control_ & 1) ? 8 : 0) |
           (AtBreakpoint() ? 16 : 0);
}
bool GxFifo::Interrupt() const {
    const auto status = Status();
    return (control_ & 1) && (((status & 1) && (control_ & 4)) ||
        ((status & 2) && (control_ & 8)) || ((status & 16) && (control_ & 32)));
}
bool GxFifo::Read16(uint32_t address, uint16_t& value) const {
    if (address == Cp) value = Status();
    else if (address == Cp + 2) value = control_;
    else if (address == Cp + 4) value = 0; // Write-only clear strobe reads zero.
    else if (address == Cp + 6) value = perf_;
    else if (address == Cp + 10) value = token_;
    else if (address >= Cp + 0x20 && address <= Cp + 0x3E && !(address & 1))
        value = regs_[(address - Cp - 0x20) / 4] >> ((address & 2) ? 16 : 0);
    else return false;
    return true;
}
bool GxFifo::Write16(uint32_t address, uint16_t value) {
    if (address == Cp + 2) { control_ = value & 0x3F; Drain(); }
    else if (address == Cp + 4) {
        // Watermark levels are recomputed from distance, not sticky events.
        // Performance counter reset has no modeled counters to clear.
    } else if (address == Cp + 6) perf_ = value & 7;
    else if (address == Cp + 10) token_ = value & 0xFF;
    else if (address >= Cp + 0x20 && address <= Cp + 0x3E && !(address & 1)) {
        auto& word = regs_[(address - Cp - 0x20) / 4];
        if (address & 2) word = (word & 0xFFFF) | (uint32_t(value & 0x1FFF) << 16);
        else word = (word & 0xFFFF0000) | (value & 0xFFE0);
        // SDK writes low and high halves separately; do not process a half-
        // updated distance until its high half has been committed.
        if (address == Cp + 0x32) Drain();
    } else return false;
    return true;
}
bool GxFifo::Read32(uint32_t address, uint32_t& value) const {
    if (address < Pi || address > Pi + 8 || (address & 3)) return false;
    value = pi_[(address - Pi) / 4];
    return true;
}
bool GxFifo::Write32(uint32_t address, uint32_t value) {
    if (address < Pi || address > Pi + 8 || (address & 3)) return false;
    pi_[(address - Pi) / 4] = value & Mask;
    return true;
}
void GxFifo::CheckRing(uint32_t base, uint32_t end, uint32_t pointer) {
    if (base > end || pointer < base || pointer > end || ((base | end | pointer) & 31))
        throw std::runtime_error("GX FIFO ring or pointer is invalid");
}
void GxFifo::Drain() {
    if (!(control_ & 1) || !regs_[4] || AtBreakpoint()) return;
    CheckRing(regs_[0], regs_[1], regs_[6]);
    if (regs_[4] > uint64_t(regs_[1]) - regs_[0] + 32)
        throw std::runtime_error("GX FIFO distance exceeds ring capacity");
    if (!consume_) throw std::runtime_error("GX FIFO has queued commands but no Kirby command consumer is connected");
    if (!resolve_) throw std::runtime_error("GX FIFO memory resolver is missing");
    while (regs_[4] && !AtBreakpoint()) {
        auto* bytes = resolve_(regs_[6], 32);
        if (!bytes) throw std::runtime_error("GX FIFO read lies outside guest RAM");
        consume_({bytes, 32});
        regs_[6] = regs_[6] == regs_[1] ? regs_[0] : regs_[6] + 32;
        regs_[4] -= 32;
        ++consumed_;
    }
}
void GxFifo::Gather(std::span<const uint8_t> bytes) {
    if (writePipeAddress_ != 0x0C008000)
        throw std::runtime_error("GX gather write before configuring SPR 921");
    while (!bytes.empty()) {
        if (used_ == 32) throw std::runtime_error("GX gather pipe retains a failed burst");
        const auto count = std::min<size_t>(32 - used_, bytes.size());
        std::copy_n(bytes.begin(), count, gather_.begin() + used_);
        used_ += count;
        bytes = bytes.subspan(count);
        if (used_ != 32) continue;
        CheckRing(pi_[0], pi_[1], pi_[2]);
        if (!resolve_) throw std::runtime_error("GX FIFO memory resolver is missing");
        auto* destination = resolve_(pi_[2], 32);
        if (!destination) throw std::runtime_error("GX FIFO write lies outside guest RAM");
        const bool linked = control_ & 16;
        if (linked) {
            if (pi_[0] != regs_[0] || pi_[1] != regs_[1] || pi_[2] != regs_[5])
                throw std::runtime_error("Linked GX CPU and GPU FIFO configuration differs");
            if (uint64_t(regs_[4]) + 32 > uint64_t(regs_[1]) - regs_[0] + 32)
                throw std::runtime_error("GX FIFO overflow would overwrite unread commands");
        }
        std::memcpy(destination, gather_.data(), 32);
        pi_[2] = pi_[2] == pi_[1] ? pi_[0] : pi_[2] + 32;
        used_ = 0;
        ++bursts_;
        if (linked) { regs_[5] = pi_[2]; regs_[4] += 32; }
        Drain();
    }
}
GxFifo& GraphicsFifo() { static GxFifo fifo; return fifo; }
}
