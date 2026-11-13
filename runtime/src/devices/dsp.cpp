// SPDX-License-Identifier: GPL-3.0-only
// Behavior references: Dolphin Emulator Project, GPL-2.0-or-later,
// DSP.cpp, DSPHLE.cpp, UCodes/INIT.cpp and MailHandler.cpp; see THIRD_PARTY.md.
#include "kirby/devices/dsp.h"
#include <cstring>
namespace Kirby {
namespace { constexpr uint16_t Status = 0xA8, Masks = 0x150; }
void DspBoot::Configure(Resolver memory, bool (*verify)(), uint64_t (*clock)()) {
    memory_ = memory; verifyInitCode_ = verify; clock_ = clock;
}
void DspBoot::ConfigureTaskBackend(bool (*boot)(const DspTaskImage&),
    bool (*readMail)(uint32_t&), void (*sendMail)(uint32_t), void (*stop)()) {
    bootTask_ = boot; readTaskMail_ = readMail; sendTaskMail_ = sendMail; stopTask_ = stop;
}
bool DspBoot::SendMail(uint32_t mail) {
    if (control_ & 4) return false;
    if (taskActive_) { sendTaskMail_(mail); return true; }
    if (!romCommand_) {
        switch (mail) {
        case 0x80F3A001: case 0x80F3A002: case 0x80F3B002:
        case 0x80F3C002: case 0x80F3D001: romCommand_ = mail; return true;
        default: return false;
        }
    }
    switch (romCommand_) {
    case 0x80F3A001: task_.address = mail; romFields_ |= 1; break;
    case 0x80F3A002: task_.length = mail & 0xFFFF; romFields_ |= 2; break;
    case 0x80F3B002: task_.dramLength = mail; romFields_ |= 4; break;
    case 0x80F3C002: task_.iram = mail; romFields_ |= 8; break;
    case 0x80F3D001: {
        auto image = task_; image.entry = mail;
        if (romFields_ != 15 || !image.length || !memory_ ||
            !memory_(image.address & 0x1FFFFFFF, image.length) ||
            !bootTask_ || !readTaskMail_ || !sendTaskMail_ || !stopTask_ ||
            !bootTask_(image)) return false;
        task_ = image; taskActive_ = true; ++taskBootCount_;
        break;
    }
    default: return false;
    }
    romCommand_ = 0;
    return true;
}
void DspBoot::Poll() {
    if (audio_.Poll()) Raise(8);
    if (taskActive_ && !(control_ & 4) && !pendingMail_ && readTaskMail_(pendingMail_))
        Raise(0x80); // Each queued task mail requests the original DSP IRQ handler.
    if (!clock_) return;
    const auto now = clock_();
    if ((control_ & 0x200) && now >= dmaDeadline_) {
        control_ = (control_ & ~0x200u) | 0x20;
        ++dmaCount_;
    }
    if ((control_ & 0x400) && now >= initDeadline_) control_ &= ~0x400u;
}
void DspBoot::Raise(uint16_t bits) { control_ |= bits & Status; }
bool DspBoot::Interrupt() const { return (control_ & (control_ >> 1) & Status) != 0; }
bool DspBoot::Read16(uint32_t a, uint16_t& v) {
    Poll();
    if (audio_.Read16(a, v)) return true;
    switch (a) {
    case 0xCC005000: v = toDsp_ >> 16; return true;
    case 0xCC005002: v = toDsp_; return true;
    case 0xCC005004:
        if (!(control_ & 4) && pendingMail_) lastMail_ = pendingMail_;
        v = lastMail_ >> 16; return true;
    case 0xCC005006:
        if (!(control_ & 4) && pendingMail_) {
            lastMail_ = pendingMail_;
            if (pendingMail_ == 0x80544348) ++initMailCount_;
            pendingMail_ = 0;
        }
        lastMail_ &= 0x7FFFFFFF;
        v = lastMail_; return true;
    case 0xCC00500A: v = control_; return true;
    case 0xCC005012: v = arInfo_; return true;
    case 0xCC00501A: v = arRefresh_; return true;
    case 0xCC005016: v = 1; return true; // AR_MODE, controller initialized.
    case 0xCC005020: v = mmAddress_ >> 16; return true;
    case 0xCC005022: v = mmAddress_; return true;
    case 0xCC005024: v = arAddress_ >> 16; return true;
    case 0xCC005026: v = arAddress_; return true;
    case 0xCC005028: v = dmaCountReg_ >> 16; return true;
    case 0xCC00502A: v = dmaCountReg_; return true;
    default: return false;
    }
}
bool DspBoot::Read32(uint32_t a, uint32_t& v) {
    if (a != 0xCC005000 && a != 0xCC005004 && a != 0xCC005020 && a != 0xCC005024 && a != 0xCC005028) return false;
    uint16_t hi, lo;
    Read16(a, hi); Read16(a+2, lo); v = (uint32_t(hi) << 16) | lo; return true;
}
bool DspBoot::WriteControl(uint16_t value) {
    // DSP assert-interrupt/command processing is not implemented. Busy bits
    // are read-only; status bits are W1C, not simple register storage.
    if (value & ~0xFFDu) return false;
    const bool reset = value & 1;
    const bool init = (control_ & 0x800) && !(value & 0x800);
    if ((reset && (control_ & 0x200)) ||
        (init && (!clock_ || !verifyInitCode_ || !verifyInitCode_()))) return false;
    const uint16_t status = control_ & Status & ~value;
    const uint16_t busy = control_ & 0x600;
    if (reset) {
        if (taskActive_) stopTask_();
        taskActive_ = false; romCommand_ = romFields_ = 0; task_ = {};
        audio_.Reset();
        pendingMail_ = 0x8071FEED; toDsp_ = 0;
    }
    control_ = status | busy | (value & (Masks | 0x804));
    if (init) {
        pendingMail_ = 0x80544348;
        control_ |= 0x400;
        initDeadline_ = clock_() + 130;
        ++initCount_;
    }
    return true;
}
bool DspBoot::StartDma(uint32_t value) {
    const uint32_t count = value & 0x03FFFFE0;
    if (!memory_ || !clock_ || !count || (control_ & 0x200) || (arInfo_ & 0xF) != 3) return false;
    // Wii ARAM DMA targets MEM2. This is distinct from DSP sample fetches,
    // which can select MEM1 or MEM2. Validate both full spans before copying.
    uint8_t* main = memory_(mmAddress_, count);
    uint8_t* aram = memory_(0x10000000 + arAddress_, count);
    if (!main || !aram) return false;
    if (value & 0x80000000) std::memmove(main, aram, count);
    else std::memmove(aram, main, count);
    dmaCountReg_ = value & 0x80000000;
    mmAddress_ += count; arAddress_ += count;
    control_ |= 0x200;
    // Dolphin's measured 246 CPU cycles per 32-byte block, converted to
    // Broadway time-base ticks (12 CPU cycles per tick), rounded upward.
    dmaDeadline_ = clock_() + (uint64_t(count / 32) * 246 + 11) / 12;
    return true;
}
bool DspBoot::Write16(uint32_t a, uint16_t v) {
    Poll();
    if (audio_.Write16(a, v)) return true;
    switch (a) {
    case 0xCC005000: toDsp_ = (toDsp_ & 0xFFFF) | (uint32_t(v) << 16); return true;
    case 0xCC005002: {
        const uint32_t mail = (toDsp_ & 0xFFFF0000) | v;
        if (!SendMail(mail)) return false;
        toDsp_ = mail & 0x7FFFFFFF;
        Poll();
        return true;
    }
    case 0xCC00500A: return WriteControl(v);
    case 0xCC005012: arInfo_ = v & 0x7F; return true;
    case 0xCC00501A: arRefresh_ = v & 0x7FF; return true;
    case 0xCC005020: mmAddress_ = (mmAddress_ & 0xFFFF) | (uint32_t(v & 0x3FF) << 16); return true;
    case 0xCC005022: mmAddress_ = (mmAddress_ & 0xFFFF0000) | (v & 0xFFE0); return true;
    case 0xCC005024: arAddress_ = (arAddress_ & 0xFFFF) | (uint32_t(v & 0x3FF) << 16); return true;
    case 0xCC005026: arAddress_ = (arAddress_ & 0xFFFF0000) | (v & 0xFFE0); return true;
    case 0xCC005028: dmaCountReg_ = (dmaCountReg_ & 0xFFFF) | (uint32_t(v & 0x83FF) << 16); return true;
    case 0xCC00502A: return StartDma((dmaCountReg_ & 0xFFFF0000) | (v & 0xFFE0));
    default: return false;
    }
}
bool DspBoot::Write32(uint32_t a, uint32_t v) {
    Poll();
    if (a == 0xCC005020) mmAddress_ = v & 0x03FFFFE0;
    else if (a == 0xCC005024) arAddress_ = v & 0x03FFFFE0;
    else if (a == 0xCC005028) return StartDma(v & 0x83FFFFE0);
    else return false;
    return true;
}
}
