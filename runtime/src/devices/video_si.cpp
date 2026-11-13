// SPDX-License-Identifier: GPL-3.0-only
// Protocol references: Kirby SDK VI/SI headers and Dolphin at the revision
// recorded in THIRD_PARTY.md. No device code or data is taken from the game.
#include "kirby/devices/video_si.h"
#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace {
constexpr uint32_t ViBase = 0xCC002000, SiBase = 0xCD006400;
bool ViAddress(uint32_t address) {
    return address >= ViBase && address < ViBase + 0x76 && !(address & 1);
}
bool ViWritable(uint32_t address) {
    return ViAddress(address) && address != ViBase + 0x2C && address != ViBase + 0x2E;
}
auto lastPoll = std::chrono::steady_clock::now();
}
namespace Kirby {
void AveGpio::Reset() {
    direction_ = 0xC000; output_ = 0;
    active_ = selected_ = slaveLow_ = ack_ = ackClock_ = false;
    phase_ = bits_ = 0; shift_ = reg_ = 0; writes_ = acks_ = 0;
    pending_.clear(); for (auto& payload : registers_) payload.clear();
}
bool AveGpio::ClockHigh() const { return !(direction_ & 0x4000) || (output_ & 0x4000); }
bool AveGpio::MasterDataHigh() const { return !(direction_ & 0x8000) || (output_ & 0x8000); }
bool AveGpio::Read32(uint32_t address, uint32_t& value) const {
    switch (address) {
    case 0xCD8000C0: value = output_; return true;
    case 0xCD8000C4: value = direction_; return true;
    case 0xCD8000C8:
        // Mounted disc: SLOT_IN high. Released I2C lines have pull-ups.
        value = 0x80 | (ClockHigh() ? 0x4000 : 0) | (MasterDataHigh() && !slaveLow_ ? 0x8000 : 0);
        return true;
    default: return false;
    }
}
bool AveGpio::Write32(uint32_t address, uint32_t value) {
    if (address != 0xCD8000C0 && address != 0xCD8000C4) return false;
    if (value & ~0xC000u) return false; // Other GPIO consumers require separate review.
    const bool clock = ClockHigh(), data = MasterDataHigh();
    if (address == 0xCD8000C0) output_ = value;
    else direction_ = value;
    Edge(clock, data);
    return true;
}
void AveGpio::Finish() {
    if (selected_ && phase_ == 2 && !pending_.empty()) {
        registers_[reg_] = pending_;
        ++writes_;
    }
    active_ = selected_ = slaveLow_ = ack_ = ackClock_ = false;
    phase_ = bits_ = 0; shift_ = 0; pending_.clear();
}
bool AveGpio::AcceptByte(uint8_t byte) {
    if (!phase_) {
        if (byte == 0xE1) throw std::runtime_error("AV encoder I2C reads are not implemented");
        selected_ = byte == 0xE0; // Seven-bit slave address 0x70, write direction.
        phase_ = 1;
    } else if (selected_) {
        if (phase_ == 1) { reg_ = byte; phase_ = 2; }
        else {
            if (pending_.size() == 256) throw std::runtime_error("AV encoder write exceeds supported payload size");
            pending_.push_back(byte);
        }
    }
    return selected_;
}
void AveGpio::Edge(bool oldClock, bool oldData) {
    const bool clock = ClockHigh(), data = MasterDataHigh();
    if (oldClock && clock && oldData != data && !slaveLow_) {
        Finish();
        if (!data) active_ = true; // START; high SDA transition is STOP.
        return;
    }
    if (!active_) return;
    if (!oldClock && clock) {
        if (bits_ < 8) {
            shift_ = uint8_t((shift_ << 1) | data);
            if (++bits_ == 8) ack_ = AcceptByte(shift_);
        } else {
            ackClock_ = true;
            if (ack_) ++acks_;
        }
    } else if (oldClock && !clock && bits_ == 8) {
        if (ackClock_) { bits_ = 0; shift_ = 0; slaveLow_ = ackClock_ = false; }
        else slaveLow_ = ack_;
    }
}

void VideoSi::Reset() {
    fields_ = 0;
    observer_ = {};
    ave_.Reset();
    vi_ = {}; channels_ = {}; buffer_ = {};
    auto word = [&](unsigned offset, uint32_t value) {
        vi_[offset / 2] = value >> 16; vi_[offset / 2 + 1] = value;
    };
    // Dolphin's NTSC boot preset, no component cable and no framebuffer.
    vi_[0] = 6; vi_[1] = 1;
    word(4, (71u << 24) | (105u << 16) | 429);
    word(8, (373u << 17) | (162u << 7) | 64);
    word(0xC, (5u << 16) | 502); word(0x10, (4u << 16) | 503);
    word(0x14, (519u << 21) | (13u << 16) | (519u << 5) | 13);
    word(0x18, (520u << 21) | (12u << 16) | (520u << 5) | 12);
    word(0x30, 0x110701AE); word(0x34, 0x10010001);
    vi_[0x48 / 2] = (40 << 8) | 40;
    vi_[0x6C / 2] = 1;
    poll_ = 492u << 16;
    control_ = status_ = exiClock_ = halfLine_ = 0;
    nextPoll_ = 15;
    remainder_ = transfers_ = polls_ = 0;
}
uint32_t VideoSi::FieldHalfLines(unsigned offset) const {
    return 3 * (vi_[0] & 15) + 2 * ((vi_[0] >> 4) & 1023) +
           (vi_[offset / 2] & 1023) + (vi_[offset / 2 + 1] & 1023);
}
uint32_t VideoSi::HalfLineCycles() const {
    return (vi_[3] & 1023) * ((vi_[0x6C / 2] & 1) ? 2 : 4);
}
void VideoSi::Advance(uint64_t cycles54MHz) {
    if (!(vi_[1] & 1) || !HalfLineCycles()) return;
    const auto oldField = fields_;
    remainder_ += cycles54MHz;
    while (remainder_ >= HalfLineCycles()) {
        remainder_ -= HalfLineCycles();
        HalfLine();
    }
    if (fields_ != oldField && observer_) observer_(Output());
}
VideoOutput VideoSi::Output() const {
    const uint32_t top = (uint32_t(vi_[0x1C / 2]) << 16) | vi_[0x1E / 2];
    const bool bottom = halfLine_ >= FieldHalfLines(0xC);
    const uint32_t base = bottom ? (uint32_t(vi_[0x24 / 2]) << 16) | vi_[0x26 / 2] : top;
    const uint32_t address = (base & 0xFFFFFF) << ((top & 0x10000000) ? 5 : 0);
    const auto picture = vi_[0x48 / 2];
    return {address, uint32_t((picture >> 8) & 0x7F) * 16, uint32_t((vi_[0] >> 4) & 0x3FF),
            uint32_t(picture & 0xFF) * 32, (base >> 24) & 15, fields_, bool(vi_[1] & 1)};
}
void VideoSi::HalfLine() {
    const auto odd = FieldHalfLines(0xC), total = odd + FieldHalfLines(0x10);
    if (!total) return;
    if (halfLine_ == nextPoll_) {
        // Disconnected ports respond with an error, never fabricated input.
        ++polls_;
        status_ |= 0x08080808;
        for (unsigned i = 0; i < 4; ++i) channels_[3 * i + 1] |= 0xC0000000;
        nextPoll_ += 2 * ((poll_ >> 16) & 1023);
    }
    if (halfLine_ == 0 || halfLine_ == odd) nextPoll_ = halfLine_ + 15;
    halfLine_ = (halfLine_ + 1) % total;
    if (halfLine_ == 0 || halfLine_ == odd) ++fields_;
    for (unsigned i = 0x30 / 2; i <= 0x3C / 2; i += 2) {
        const auto targetHalf = (vi_[i + 1] & 2047) > (vi_[3] & 1023) ? 1u : 0u;
        if ((1 + halfLine_ / 2) == (vi_[i] & 2047) && (halfLine_ & 1) == targetHalf)
            vi_[i] |= 0x8000;
    }
}
bool VideoSi::Read16(uint32_t address, uint16_t& value) {
    if (ViAddress(address)) {
        const auto offset = address - ViBase;
        if (offset == 0x2C) value = 1 + halfLine_ / 2;
        else if (offset == 0x2E) value = 1 + (halfLine_ & 1) * (vi_[3] & 1023) +
            remainder_ / ((vi_[0x6C / 2] & 1) ? 2 : 4);
        else value = vi_[offset / 2];
        return true;
    }
    if (address >= SiBase + 0x80 && address <= SiBase + 0xFE && !(address & 1)) {
        const auto offset = address - SiBase - 0x80;
        value = (uint16_t(buffer_[offset]) << 8) | buffer_[offset + 1];
        return true;
    }
    return false;
}
bool VideoSi::Write16(uint32_t address, uint16_t value) {
    if (ViWritable(address)) {
        const auto offset = address - ViBase;
        if (offset == 2) {
            vi_[1] = value & 0x3FD;
            if (value & 2) std::fill(vi_.begin() + 0x30 / 2, vi_.begin() + 0x40 / 2, 0);
        } else {
            vi_[offset / 2] = value;
            if ((offset == 0x1C || offset == 0x20 || offset == 0x24 || offset == 0x28) && (value & 0xE000))
                vi_[offset / 2] &= ~0x1000; // Clear framebuffer page offset.
        }
        return true;
    }
    if (address >= SiBase + 0x80 && address <= SiBase + 0xFE && !(address & 1)) {
        const auto offset = address - SiBase - 0x80;
        buffer_[offset] = value >> 8; buffer_[offset + 1] = value;
        return true;
    }
    return false;
}
void VideoSi::RefreshSerialStatus() {
    control_ = (control_ & ~0x10000000u) | ((status_ & 0x20202020) ? 0x10000000u : 0);
}
bool VideoSi::Read32(uint32_t address, uint32_t& value) {
    if (ave_.Read32(address, value)) return true;
    if (address & 3) return false;
    if ((ViAddress(address) && ViAddress(address + 2)) || (address >= SiBase + 0x80 && address <= SiBase + 0xFC)) {
        uint16_t hi, lo;
        if (!Read16(address, hi) || !Read16(address + 2, lo)) return false;
        value = (uint32_t(hi) << 16) | lo; return true;
    }
    if (address < SiBase || address > SiBase + 0x3C) return false;
    const auto offset = address - SiBase;
    if (offset < 0x30) {
        value = channels_[offset / 4];
        if ((offset / 4) % 3) status_ &= ~(0x20000000u >> ((offset / 12) * 8));
        RefreshSerialStatus();
    } else if (offset == 0x30) value = poll_;
    else if (offset == 0x34) value = control_;
    else if (offset == 0x38) value = status_;
    else value = exiClock_;
    return true;
}
bool VideoSi::Write32(uint32_t address, uint32_t value) {
    if (ave_.Write32(address, value)) return true;
    if (address & 3) return false;
    if ((ViWritable(address) && ViWritable(address + 2)) || (address >= SiBase + 0x80 && address <= SiBase + 0xFC)) {
        Write16(address, value >> 16); Write16(address + 2, value); return true;
    }
    if (address < SiBase || address > SiBase + 0x3C) return false;
    const auto offset = address - SiBase;
    if (offset < 0x30) channels_[offset / 4] = value;
    else if (offset == 0x30) poll_ = value;
    else if (offset == 0x34) {
        constexpr uint32_t fields = 0x487F7F06, acknowledge = 0x90000000;
        control_ = (control_ & ~fields & ~(value & acknowledge)) | (value & fields);
        if (value & 1) {
            // All four ports are explicitly disconnected. As in Dolphin's
            // Null device, terminate with NOREP/COMERR and leave RAM unchanged.
            status_ |= 0x08000000u >> (((value >> 1) & 3) * 8);
            control_ = (control_ & ~1u) | 0xA0000000;
            ++transfers_;
        }
    } else if (offset == 0x38) {
        status_ &= ~(value & 0x0F0F0F0F);
        if (value & 0x80000000) status_ &= ~0x90909090u;
    } else exiClock_ = value;
    RefreshSerialStatus();
    return true;
}
bool VideoSi::VideoInterrupt() const {
    for (unsigned i = 0x30 / 2; i <= 0x3C / 2; i += 2) if ((vi_[i] & 0x9000) == 0x9000) return true;
    return false;
}
bool VideoSi::SerialInterrupt() const {
    return (control_ & 0xC0000000) == 0xC0000000 || (control_ & 0x18000000) == 0x18000000;
}
VideoSi& Devices() { static VideoSi model; return model; }
void ResetDevices() { Devices().Reset(); lastPoll = std::chrono::steady_clock::now(); }
void PollDeviceClock() {
    const auto now = std::chrono::steady_clock::now();
    const uint64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now - lastPoll).count();
    // Retain sub-microsecond time in lastPoll rather than dropping it each read.
    const auto us = ns / 1000;
    lastPoll += std::chrono::microseconds(us);
    Devices().Advance(us * 54);
}
} // namespace Kirby
