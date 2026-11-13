// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <vector>
#include <functional>

namespace Kirby {
struct VideoOutput {
    uint32_t address = 0, width = 0, height = 0, stride = 0, xOffset = 0;
    uint64_t field = 0;
    bool enabled = false;
};
// Broadway GPIO pins 14/15 drive the AV encoder's I2C write interface.
// Retains register payloads; analog output and encoder reads are not implemented.
class AveGpio {
public:
    AveGpio() { Reset(); }
    void Reset();
    bool Read32(uint32_t address, uint32_t& value) const;
    bool Write32(uint32_t address, uint32_t value);
    std::span<const uint8_t> Payload(uint8_t reg) const { return registers_[reg]; }
    uint64_t WriteCount() const { return writes_; }
    uint64_t AckCount() const { return acks_; }
private:
    void Edge(bool oldClock, bool oldData);
    void Finish();
    bool AcceptByte(uint8_t byte);
    bool ClockHigh() const;
    bool MasterDataHigh() const;
    uint32_t direction_ = 0, output_ = 0;
    bool active_ = false, selected_ = false, slaveLow_ = false, ack_ = false, ackClock_ = false;
    unsigned phase_ = 0, bits_ = 0;
    uint8_t shift_ = 0, reg_ = 0;
    uint64_t writes_ = 0, acks_ = 0;
    std::vector<uint8_t> pending_;
    std::array<std::vector<uint8_t>, 256> registers_;
};
// VI register/timing model and four empty GameCube SI ports. Wii Remote input
// belongs to IOS/Bluetooth. This class does not draw frames or dispatch CPU IRQs.
class VideoSi {
public:
    VideoSi() { Reset(); }
    void Reset();
    void Advance(uint64_t cycles54MHz);
    bool Read16(uint32_t address, uint16_t& value);
    bool Read32(uint32_t address, uint32_t& value);
    bool Write16(uint32_t address, uint16_t value);
    bool Write32(uint32_t address, uint32_t value);
    bool VideoInterrupt() const;
    bool SerialInterrupt() const;
    uint64_t TransferCount() const { return transfers_; }
    uint64_t PollCount() const { return polls_; }
    const AveGpio& Ave() const { return ave_; }
    VideoOutput Output() const;
    void ObserveVideo(std::function<void(const VideoOutput&)> observer) { observer_ = std::move(observer); }
private:
    void RefreshSerialStatus();
    uint32_t FieldHalfLines(unsigned offset) const;
    uint32_t HalfLineCycles() const;
    void HalfLine();
    std::array<uint16_t, 0x76 / 2> vi_{};
    std::array<uint32_t, 12> channels_{};
    std::array<uint8_t, 128> buffer_{};
    uint32_t poll_, control_, status_, exiClock_;
    uint32_t halfLine_, nextPoll_;
    uint64_t remainder_, transfers_, polls_;
    uint64_t fields_ = 0;
    std::function<void(const VideoOutput&)> observer_;
    AveGpio ave_;
};

VideoSi& Devices();
void ResetDevices();
void PollDeviceClock();
} // namespace Kirby
