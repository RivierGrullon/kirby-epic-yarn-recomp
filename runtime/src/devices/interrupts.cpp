// SPDX-License-Identifier: GPL-3.0-only
// Register layout: Swiftshine/key RK5E01 SDK headers. Interrupt semantics were
// checked against Dolphin HW sources (GPL-2.0-or-later); see THIRD_PARTY.md.
#include "kirby/devices/interrupts.h"
#include "kirby/devices/video_si.h"
#include "kirby/boot/clock.h"
#include "kirby/graphics/gx_fifo.h"
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
constexpr uint32_t PiCause = 0xCC003000, PiMask = 0xCC003004;
constexpr uint32_t MiMask = 0xCC00401C, MiFlags = 0xCC00401E;
constexpr uint32_t AiControl = 0xCD006C00;
constexpr uint32_t IpcFlags = 0xCD000030, IpcMask = 0xCD000034;
constexpr uint16_t DspMaskBits = 0x150;
constexpr uint32_t ExiAddresses[] = {0xCD006800, 0xCD006814, 0xCD006828};
int ExiChannel(uint32_t address) {
    for (int i = 0; i < 3; ++i) if (address == ExiAddresses[i]) return i;
    return -1;
}
int ExiTransferChannel(uint32_t address) {
    if (address < ExiAddresses[0] || address >= ExiAddresses[0] + 0x3C || (address & 3)) return -1;
    return static_cast<int>((address - ExiAddresses[0]) / 0x14);
}
} // namespace

namespace Kirby {
InterruptController::InterruptController() { Reset(); }
void InterruptController::Reset() {
    // No VI source is asserted: this bootstrap has not started a video device.
    piCause_ = 0x10000; // Released reset-button input.
    piMask_ = ipcFlags_ = 0;
    ipcMask_ = 0x40000000;
    ipcControl_ = ipcMessage_ = 0;
    miMask_ = miFlags_ = miProtection_ = 0;
    dsp_ = DspBoot{};
    aiControl_ = 0x42; // Streaming 48 kHz / DMA 32 kHz, playback stopped.
    aiVolume_ = aiThreshold_ = 0;
    exiStatus_ = {0x800, 0x880, 0}; // No attached cards; initial EXT latches.
    exiTransfer_ = {};
    iplCommand_ = iplCommandBytes_ = 0;
    exiTransferCount_ = 0;
    euartControl_ = 0;
    euartBytesWritten_ = 0;
    euartLine_.clear();
    storage_ = IplStorage{};
    iplCursor_ = 0;
    dmaResolver_ = nullptr; rtcClock_ = nullptr;
}
void InterruptController::ConfigureIpl(const std::filesystem::path& path,
    uint8_t* (*dmaResolver)(uint32_t, uint32_t), uint32_t (*rtcClock)()) {
    if (!path.empty()) storage_.Open(path);
    dmaResolver_ = dmaResolver; rtcClock_ = rtcClock;
}

bool InterruptController::Read32(uint32_t address, uint32_t& value) {
    PollDsp();
    if (dsp_.Read32(address, value)) { UpdateDeviceLines(); return true; }
    if (address == PiCause) value = piCause_;
    else if (address == ::PiMask) value = piMask_;
    else if (address == AiControl) value = aiControl_;
    else if (address == AiControl + 4) value = aiVolume_;
    else if (address == AiControl + 8) value = 0; // Playback remains stopped.
    else if (address == AiControl + 12) value = aiThreshold_;
    else if (address == 0xCC003024) value = 0x246500B1; // Dolphin PI revision C profile.
    else if (address == 0xCD000004) value = ipcControl_;
    else if (address == 0xCD000008) value = 0; // No IOS reply has been delivered.
    // EXISync queries this read-only DI configuration byte. Dolphin's normal
    // Wii drive configuration is 1 (bootrom descrambler disabled).
    else if (address == 0xCD006024) value = 1;
    else if (const int channel = ExiChannel(address); channel >= 0) value = exiStatus_[channel];
    else if (const int channel = ExiTransferChannel(address); channel >= 0) {
        const auto& transfer = exiTransfer_[channel];
        switch (address - ExiAddresses[channel]) {
        case 4: value = transfer.address; break;
        case 8: value = transfer.length; break;
        case 12: value = transfer.control; break;
        case 16: value = transfer.data; break;
        default: return false;
        }
    }
    else return false;
    return true;
}

bool InterruptController::Read16(uint32_t address, uint16_t& value) {
    PollDsp();
    if (dsp_.Read16(address, value)) { UpdateDeviceLines(); return true; }
    if (address == MiMask) value = miMask_;
    else if (address == MiFlags) value = miFlags_;
    else if (address == 0xCC004010) value = miProtection_;
    else if (address == 0xCC004020) value = 0;
    else if ((address & 1) == 0 && address >= PiCause && address <= ::PiMask + 2) {
        uint32_t word = 0;
        if (!Read32(address & ~3u, word)) return false;
        value = static_cast<uint16_t>(word >> ((address & 2) ? 0 : 16));
    } else return false;
    return true;
}

bool InterruptController::Write16(uint32_t address, uint16_t value) {
    PollDsp();
    if (dsp_.Write16(address, value)) { UpdateDeviceLines(); return true; }
    if (address == MiMask) miMask_ = value;
    else if (address == MiFlags) miFlags_ = value; // MI flags are direct-write, not W1C.
    // The SDK boot path disables all four protection regions. Enabling a
    // protection rule requires native memory enforcement, not just a latch.
    else if (address == 0xCC004010 && value == 0xFF) miProtection_ = value;
    else if (address == 0xCC004020 && value == 0) { /* Reviewed SDK clear. */ }
    else return false;
    UpdateDeviceLines();
    return true;
}

bool InterruptController::Write32(uint32_t address, uint32_t value) {
    PollDsp();
    if (dsp_.Write32(address, value)) { UpdateDeviceLines(); return true; }
    if (address == PiCause) { piCause_ &= ~value; return true; }
    if (address == ::PiMask) { piMask_ = value; return true; }
    if (address == 0xCD000000) { ipcMessage_ = value; return true; }
    if (address == 0xCD000004) {
        // X1 submits a request. Do not acknowledge/complete it until an IOS
        // service and guest callback transport have actually handled it.
        if (value & ~0x3Eu) return false;
        ipcControl_ = (ipcControl_ & 6 & ~value) | (value & 0x38);
        if (((value & 4) && (value & 0x10)) || ((value & 2) && (value & 0x20)))
            ipcFlags_ |= 0x40000000;
        UpdateDeviceLines();
        return true;
    }
    if (address == ::IpcMask) {
        ipcMask_ = value;
        if (value & 0x40000000) {
            ipcFlags_ = ipcControl_ = ipcMessage_ = 0;
            ipcMask_ = 0x40000000;
        }
    } else if (address == IpcFlags) ipcFlags_ &= ~value;
    else if (address == AiControl + 4) aiVolume_ = value;
    else if (address == AiControl + 12) aiThreshold_ = value;
    else if (address == AiControl) {
        // AIInit clears the stopped stream counter. SCRESET is a strobe;
        // retain the reviewed rates and reject playback until it is modeled.
        if (((value ^ aiControl_) & ~0x2Cu) != 0) return false;
        aiControl_ = (aiControl_ & ~0xCu) | (value & 4) | (aiControl_ & 8 & ~value);
    } else if (const int channel = ExiChannel(address); channel >= 0) {
        const uint32_t masks = channel == 2 ? 5 : 0x405;
        const uint32_t status = channel == 2 ? 0xA : 0x80A;
        auto& old = exiStatus_[channel];
        const uint32_t select = value & 0x380;
        // Only one device can own a transaction. CS1/CS2 exist on channel 0.
        if ((select && (select & (select - 1))) || (channel != 0 && (select & 0x300))) return false;
        if (channel == 0 && select == 0x100 && (old & 0x380) != select) {
            iplCommand_ = iplCommandBytes_ = 0;
            iplCursor_ = 0;
        }
        const uint32_t writable = masks | 0x3F0 | (channel == 0 ? 0x2000 : 0);
        old = (value & writable) | (old & status & ~value);
    } else if (const int channel = ExiTransferChannel(address); channel >= 0) {
        auto& transfer = exiTransfer_[channel];
        switch (address - ExiAddresses[channel]) {
        case 4: transfer.address = value; break;
        case 8: transfer.length = value; break;
        case 12: if (!WriteExiControl(channel, value)) return false; break;
        case 16: transfer.data = value; break;
        default: return false;
        }
    } else return false;
    UpdateDeviceLines();
    return true;
}

bool InterruptController::WriteExiControl(unsigned channel, uint32_t value) {
    auto& transfer = exiTransfer_[channel];
    if (value & ~0x3Fu) return false;
    if (!(value & 1)) { transfer.control = value; return true; }
    const uint32_t select = exiStatus_[channel] & 0x380;
    const unsigned direction = (value >> 2) & 3;
    const unsigned bytes = ((value >> 4) & 3) + 1;
    if (!select || direction == 3) return false;
    const bool ipl = channel == 0 && select == 0x100;
    const bool commandPhase = iplCommandBytes_ < 4;
    const bool commandWrite = commandPhase && direction == 1 && iplCommandBytes_ + bytes <= 4;
    const bool euartInit = !commandPhase && iplCommand_ == 0xB0000000 && direction == 1 && bytes == 4 &&
        (transfer.data == 0xF2000000 || (transfer.data == 0xF3000000 && euartControl_ == 0xF2));
    const bool euartWrite = !commandPhase && iplCommand_ == 0xB0000100 && direction == 1 && EuartReady() &&
        euartLine_.size() + bytes <= 4096;
    const bool euartRead = !commandPhase && iplCommand_ == 0x30000100 && direction == 0 && EuartReady();
    const uint32_t length = (value & 2) ? transfer.length : bytes;
    const bool storageAccess = ipl && !commandPhase && direction <= 1 &&
        storage_.Supports(iplCommand_, iplCursor_, length, direction == 1);
    const bool dma = (value & 2) != 0;
    const bool supportedDma = storageAccess && dmaResolver_ && !(transfer.address & 31) && !(length & 31);
    // Supported topology: internal IPL, empty card slots, no SP1 accessory,
    // and no channel-2 debug accessory. Never substitute None for the IPL.
    if ((dma && !supportedDma) || (ipl && !(commandWrite || euartInit || euartWrite || euartRead || storageAccess))) {
        std::ostringstream message;
        message << "EXI backend unsupported: channel=" << channel << " CS=0x" << std::hex << select
                << " control=0x" << value << " data=0x" << transfer.data;
        if (ipl) message << " IPL command=0x" << iplCommand_ << " command-bytes=" << std::dec << iplCommandBytes_;
        if (value & 2) message << " DMA address=0x" << std::hex << transfer.address << " length=0x" << transfer.length;
        throw std::runtime_error(message.str());
    }
    if (storageAccess) {
        std::array<uint8_t, 4> immediate{};
        uint8_t* data = immediate.data();
        if (dma) {
            data = dmaResolver_(transfer.address, length); // Validate the entire span before device mutation.
            if (!data) throw std::runtime_error("EXI DMA resolver returned no memory");
        } else for (unsigned i = 0; i < bytes; ++i) immediate[i] = transfer.data >> (24 - i * 8);
        if (direction == 1) storage_.Write(iplCommand_, iplCursor_, {data, length});
        else {
            storage_.Read(iplCommand_, iplCursor_, {data, length});
            if (!dma) {
                transfer.data = 0;
                for (unsigned i = 0; i < bytes; ++i) transfer.data |= uint32_t(immediate[i]) << (24 - i * 8);
            }
        }
        iplCursor_ += length;
    } else if (ipl && commandPhase) {
        // Commands arrive most-significant byte first and reset on selection.
        // Payloads are restricted to the reviewed EUART protocol below.
        for (unsigned i = 0; i < bytes; ++i)
            iplCommand_ = (iplCommand_ << 8) | ((transfer.data >> (24 - 8 * i)) & 0xFF);
        iplCommandBytes_ += bytes;
        if (iplCommandBytes_ == 4 && rtcClock_) storage_.SetRtc(rtcClock_());
    } else if (ipl) {
        if (euartInit) {
            euartControl_ = static_cast<uint8_t>(transfer.data >> 24);
            if (euartControl_ == 0xF2) euartLine_.clear();
        } else if (euartRead) {
            transfer.data = 0; // Host sink consumes writes synchronously: FIFO empty.
        } else {
            for (unsigned i = 0; i < bytes; ++i) {
                const char byte = static_cast<char>(transfer.data >> (24 - 8 * i));
                if (byte == 0) continue; // SDK pads each character to four bytes.
                ++euartBytesWritten_;
                if (byte == '\r' || byte == '\n') {
                    std::clog << "[kirby:EUART] " << euartLine_ << '\n';
                    euartLine_.clear();
                } else euartLine_ += byte;
            }
        }
    } else if (direction == 0) {
        // Dolphin's None device leaves read bytes zero. This is an explicit
        // absent-device response, not a fallback for unknown MMIO or the IPL.
        transfer.data = 0;
    }
    // None-device writes discard output; duplex leaves the immediate word
    // unchanged, matching IEXIDevice's default immediate methods.
    transfer.control = value & ~1u;
    exiStatus_[channel] |= 8;
    ++exiTransferCount_;
    return true;
}

void InterruptController::SetPiLine(uint32_t source, bool asserted) {
    if (asserted) piCause_ |= source;
    else piCause_ &= ~source;
}
void InterruptController::UpdateDeviceLines() {
    SetPiLine(0x80, (miFlags_ & miMask_) != 0);
    SetPiLine(0x40, dsp_.Interrupt());
    SetPiLine(0x20, (aiControl_ & 0xC) == 0xC);
    bool exi = false;
    for (const auto status : exiStatus_) exi |= (status & (status << 1) & 0x80A) != 0;
    SetPiLine(0x10, exi);
    SetPiLine(0x4000, (ipcFlags_ & ipcMask_) != 0);
}
void InterruptController::PollDsp() { dsp_.Poll(); UpdateDeviceLines(); }
void InterruptController::RaiseDsp(uint16_t sources) {
    dsp_.Raise(sources);
    UpdateDeviceLines();
}
void InterruptController::RaiseAi() { aiControl_ |= 8; UpdateDeviceLines(); }
void InterruptController::RaiseExi(unsigned channel, uint32_t sources) {
    if (channel >= exiStatus_.size()) throw std::out_of_range("EXI interrupt channel");
    exiStatus_[channel] |= sources & (channel == 2 ? 0xA : 0x80A);
    UpdateDeviceLines();
}
void InterruptController::RaiseIpc(uint32_t sources) { ipcFlags_ |= sources; UpdateDeviceLines(); }
bool InterruptController::Pending() const { return (piCause_ & piMask_) != 0; }
uint32_t InterruptController::PiMask() const { return piMask_; }
uint32_t InterruptController::IpcMask() const { return ipcMask_; }
uint64_t InterruptController::ExiTransferCount() const { return exiTransferCount_; }
bool InterruptController::EuartReady() const { return euartControl_ == 0xF3; }
uint64_t InterruptController::EuartBytesWritten() const { return euartBytesWritten_; }
bool InterruptController::DeviceSourcesMasked() const {
    if (miMask_ || (dsp_.Control() & DspMaskBits) || (aiControl_ & 4) || (piMask_ & ~0xF0u)) return false;
    for (const auto status : exiStatus_) if (status & 0x405) return false;
    return true;
}
InterruptController& Interrupts() { static InterruptController model; return model; }
} // namespace Kirby

namespace {
void SyncDevices() {
    Kirby::PollDeviceClock();
    Kirby::Interrupts().SetPiLine(0x800, Kirby::GraphicsFifo().Interrupt());
    Kirby::Interrupts().SetPiLine(0x200, Kirby::Pixels().TokenInterrupt());
    Kirby::Interrupts().SetPiLine(0x400, Kirby::Pixels().FinishInterrupt());
    Kirby::Interrupts().SetPiLine(0x100, Kirby::Devices().VideoInterrupt());
    Kirby::Interrupts().SetPiLine(8, Kirby::Devices().SerialInterrupt());
}
}
bool KirbyMmioRead16(uint32_t address, uint16_t& value) {
    if (Kirby::Pixels().Read16(address, value)) { SyncDevices(); return true; }
    if (Kirby::GraphicsFifo().Read16(address, value)) { SyncDevices(); return true; }
    SyncDevices();
    if (Kirby::Devices().Read16(address, value)) { SyncDevices(); return true; }
    return Kirby::Interrupts().Read16(address, value);
}
bool KirbyMmioRead32(uint32_t address, uint32_t& value) {
    if (Kirby::GraphicsFifo().Read32(address, value)) { SyncDevices(); return true; }
    if (Kirby::Clock().Read32(address, value)) return true;
    SyncDevices();
    if (Kirby::Devices().Read32(address, value)) { SyncDevices(); return true; }
    return Kirby::Interrupts().Read32(address, value);
}
bool KirbyMmioWrite16(uint32_t address, uint16_t value) {
    if (Kirby::Pixels().Write16(address, value)) { SyncDevices(); return true; }
    if (Kirby::GraphicsFifo().Write16(address, value)) { SyncDevices(); return true; }
    SyncDevices();
    if (Kirby::Devices().Write16(address, value)) { SyncDevices(); return true; }
    return Kirby::Interrupts().Write16(address, value);
}
bool KirbyMmioWrite32(uint32_t address, uint32_t value) {
    if (Kirby::GraphicsFifo().Write32(address, value)) { SyncDevices(); return true; }
    if (Kirby::Clock().Write32(address, value)) return true;
    SyncDevices();
    if (Kirby::Devices().Write32(address, value)) { SyncDevices(); return true; }
    return Kirby::Interrupts().Write32(address, value);
}
