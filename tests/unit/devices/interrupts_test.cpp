// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/devices/interrupts.h"
#include "kirby/graphics/gx_fifo.h"
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks = 0;
void Check(bool result, const char* message) {
    ++checks;
    if (!result) throw std::runtime_error(message);
}
uint32_t Read32(Kirby::InterruptController& model, uint32_t address) {
    uint32_t value = 0xDEADBEEF;
    Check(model.Read32(address, value), "32-bit register was not decoded");
    return value;
}
uint16_t Read16(Kirby::InterruptController& model, uint32_t address) {
    uint16_t value = 0xBEEF;
    Check(model.Read16(address, value), "16-bit register was not decoded");
    return value;
}
} // namespace

int main() {
    try {
        Kirby::InterruptController pi;
        Check(Read32(pi, 0xCC003000) == 0x10000, "reset button is initially released, with no device IRQs");
        Check(Read32(pi, 0xCC003024) == 0x246500B1 && !pi.Write32(0xCC003024, 0), "PI revision is read-only and matches the reviewed platform profile");
        pi.SetPiLine(0x100, true);
        Check(!pi.Pending(), "masked VI source must not assert external IRQ");
        Check(pi.Write32(0xCC003004, 0x104), "PI mask write");
        Check(pi.Pending(), "unmasking a pending VI source must assert external IRQ");
        pi.SetPiLine(4, true);
        Check(pi.Write32(0xCC003000, 0x100), "PI acknowledge write");
        Check(Read32(pi, 0xCC003000) == 0x10004 && pi.Pending(), "acknowledge must preserve other pending sources");
        Check(pi.Write32(0xCC003000, 4) && !pi.Pending(), "acknowledging final source must deassert external IRQ");
        Check(Read16(pi, 0xCC003000) == 1 && Read16(pi, 0xCC003002) == 0, "PI halfword reads are big endian");
        Check(!pi.Write16(0xCC003004, 0xFFFF) && pi.PiMask() == 0x104, "PI halfword writes must be rejected atomically");

        Kirby::InterruptController devices;
        Check(devices.Write32(0xCC003004, 0x4070), "group IRQ masks");
        devices.RaiseDsp(8);
        Check(!devices.Pending(), "DSP interrupt must respect its local mask");
        Check(devices.Write16(0xCC00500A, 0x814) && devices.Pending(), "DSP mask must propagate to PI");
        devices.RaiseDsp(0x20);
        Check(devices.Write16(0xCC00500A, 0x81C) && !devices.Pending(), "DSP acknowledge and masks must be independent");
        Check(Read16(devices, 0xCC00500A) == 0x834, "DSP acknowledge must retain other status and halt bits");
        Check(!devices.Write16(0xCC00500A, 0x836) && Read16(devices, 0xCC00500A) == 0x834, "unimplemented DSP command interrupt must fail without mutation");
        Check(devices.Write16(0xCC004020, 0) && devices.Write16(0xCC004010, 0xFF), "SDK disables all MI protection regions");
        Check(Read16(devices, 0xCC004020) == 0 && Read16(devices, 0xCC004010) == 0xFF, "MI setup readback");
        Check(!devices.Write16(0xCC004010, 0) && Read16(devices, 0xCC004010) == 0xFF, "unsupported protected regions rejected without changing permissions");
        devices.RaiseAi();
        Check(!devices.Pending(), "AI local mask");
        Check(devices.Write32(0xCD006C00, 0x46) && devices.Pending(), "AI mask propagation");
        Check(devices.Write32(0xCD006C00, 0x4E) && !devices.Pending(), "AI write-one-to-clear");
        Check(!devices.Write32(0xCD006C00, 0x47) && Read32(devices, 0xCD006C00) == 0x46,
              "unimplemented audio start must not succeed or mutate control state");
        Check(devices.Write32(0xCD006C04, 0x12345678) && Read32(devices, 0xCD006C04) == 0x12345678,
              "AI volume register readback matches the reference model");
        Check(devices.Write32(0xCD006C0C, 1234) && Read32(devices, 0xCD006C0C) == 1234,
              "AI interrupt threshold round trip");
        Check(devices.Write32(0xCD006C00, 0x66) && Read32(devices, 0xCD006C00) == 0x46 &&
              Read32(devices, 0xCD006C08) == 0, "AI stopped-counter reset strobe does not latch");
        devices.RaiseExi(0, 2);
        Check(devices.Write32(0xCD006800, 1) && devices.Pending(), "EXI channel mask propagation");
        Check(devices.Write32(0xCD006800, 3) && !devices.Pending(), "EXI acknowledge");
        Check(Read32(devices, 0xCD006814) == 0x880, "independent EXI channel state");
        Check(!devices.Write32(0xCD006800, 0x181) && Read32(devices, 0xCD006800) == 0x801,
              "multiple EXI chip selects must be rejected without mutation");
        Check(devices.Write32(0xCD000034, 0), "IPC mask write");
        devices.RaiseIpc(0x40000000);
        Check(!devices.Pending(), "IPC local mask");
        Check(devices.Write32(0xCD000034, 0x40000000) && !devices.Pending(), "IPC reset mask write clears pending state");
        Check(devices.Write32(0xCD000004, 0x38) && Read32(devices, 0xCD000004) == 0x38, "IPC client enables ack/reply masks");
        Check(devices.Write32(0xCD000000, 0x135E0000) && !devices.Write32(0xCD000004, 0x31), "unsupported IOS submission must fail");
        Check(Read32(devices, 0xCD000004) == 0x38 && Read32(devices, 0xCD000008) == 0, "rejected submission cannot invent ack or reply");
        Check(devices.Write32(0xCD000004, 0x32) && devices.Pending(), "ack write triggers IPC IRQ when its mask is enabled");
        Check(devices.Write32(0xCD000030, 0x40000000) && !devices.Pending(), "IPC IRQ acknowledgment");
        devices.RaiseIpc(0x40000000);
        Check(devices.Pending(), "IPC group line propagation");
        Check(devices.Write32(0xCD000030, 0x40000000) && !devices.Pending(), "IPC acknowledge");
        uint32_t untouched = 123;
        Check(!devices.Read32(0xCD000034, untouched) && untouched == 123, "IPC write-only register must not invent reads");
        Check(!devices.Read32(0xCD000000, untouched), "PPC request message register remains write-only");
        Check(!devices.Write32(0xCC005030, 0), "Audio sample DMA remains unsupported");
        Check(!devices.Read32(0xDEADBEEF, untouched) && untouched == 123, "unknown register must preserve output");
        Check(devices.Write16(0xCC00401C, 0x1F), "MI mask write");
        Check(Read16(devices, 0xCC00401C) == 0x1F, "MI mask round trip");
        devices.Reset();
        Check(Read32(devices, 0xCD006C04) == 0 && Read32(devices, 0xCD006C0C) == 0,
              "reset clears AI volume and threshold");
        Check(!devices.Pending() && devices.DeviceSourcesMasked() && devices.PiMask() == 0,
              "reset must restore masked idle device state");
        Check(devices.Write32(0xCC003004, 0x80) && devices.Write16(0xCC00401C, 1), "MI group setup");
        Check(devices.Write16(0xCC00401E, 1) && devices.Pending(), "MI flags and mask must propagate to PI");
        Check(devices.Write16(0xCC00401E, 1) && devices.Pending(), "MI flags are direct-write, unlike DSP W1C flags");
        Check(devices.Write16(0xCC00401E, 0) && !devices.Pending(), "clearing MI flags must drop its PI line");
        Kirby::InterruptController exi;
        Check(Read32(exi, 0xCD006024) == 1, "DI config reports the reviewed bootrom-descrambler setting");
        Check(!exi.Write32(0xCD006024, 0) && Read32(exi, 0xCD006024) == 1,
              "DI config is read-only");
        Check(!exi.Write32(0xCD00601C, 1), "DI commands remain unsupported");
        for (const auto base : {0xCD006800u, 0xCD006814u, 0xCD006828u}) {
            Check(Read32(exi, base + 12) == 0, "EXI starts with no active transfer");
            Check(exi.Write32(base, 0), "EXI channel deselection");
        }
        Check(exi.Write32(0xCD006800, 0x2204), "select absent SP1 with ROM disabled and TC mask");
        Check(exi.Write32(0xCC003004, 0x10), "enable PI EXI group");
        Check(exi.Write32(0xCD006810, 0), "EXI ID command staging");
        Check(exi.Write32(0xCD00680C, 0x15), "two-byte immediate ID command to absent SP1");
        Check(Read32(exi, 0xCD00680C) == 0x14 && exi.Pending(), "completed write clears TSTART and raises TC");
        Check(exi.Write32(0xCD006800, 0x220C) && !exi.Pending(), "TC acknowledge preserves selection");
        Check(exi.Write32(0xCD00680C, 0x31), "four-byte absent-device read");
        Check(Read32(exi, 0xCD006810) == 0 && exi.Pending(), "absent SP1 returns Dolphin None-device ID");
        Check(Read32(exi, 0xCD006824) == 0, "immediate registers are channel-local");
        Check(exi.Write32(0xCD006800, 0x200C), "deselect and acknowledge");
        Check(!exi.Write32(0xCD00680C, 0x31) && Read32(exi, 0xCD00680C) == 0x30,
              "unselected transfer cannot complete");
        Check(exi.Write32(0xCD006800, 0x2130), "select internal IPL at 8 MHz");
        Check(exi.Write32(0xCD006810, 0x20000100) && exi.Write32(0xCD00680C, 0x35),
              "IPL accepts its four-byte SRAM read command");
        Check(exi.Write32(0xCD006800, 0x2138), "acknowledge IPL command");
        Check(exi.Write32(0xCD006804, 0x00100000) && exi.Write32(0xCD006808, 64), "stage SRAM DMA registers");
        bool blocked = false;
        try { exi.Write32(0xCD00680C, 3); }
        catch (const std::runtime_error& error) {
            blocked = std::string(error.what()).find("IPL command=0x20000100") != std::string::npos;
        }
        Check(blocked, "unimplemented SRAM DMA must report its IPL command");
        Check(Read32(exi, 0xCD00680C) == 0x34 && !(Read32(exi, 0xCD006800) & 8),
              "unsupported DMA must not mutate control or claim completion");
        Check(exi.ExiTransferCount() == 3, "failed DMA must not increase transfer count");
        Check(!exi.Write32(0xCD00680D, 0) && !exi.Write16(0xCD00680C, 0), "EXI requires aligned 32-bit accesses");
        Check(!exi.Write32(0xCD006814, 0x100), "channel 1 has no internal device select");
        Check(exi.Write32(0xCD006800, 0) && exi.Write32(0xCD006800, 0x100), "reselect IPL to start a new command");
        Check(exi.Write32(0xCD006810, 0x20000000) && exi.Write32(0xCD00680C, 0x15), "first two IPL command bytes");
        Check(exi.Write32(0xCD006800, 0x108), "acknowledgment with unchanged select must preserve command bytes");
        Check(exi.Write32(0xCD006810, 0x01000000) && exi.Write32(0xCD00680C, 0x15), "final two IPL command bytes");
        Check(exi.Write32(0xCD00680C, 0x31) && Read32(exi, 0xCD006810) == 0x002CFFD0,
              "split IPL commands preserve big endian SRAM addressing");
        Check(exi.Write32(0xCD006800, 0) && exi.Write32(0xCD006800, 0x200), "select empty SP1 again");
        Check(exi.Write32(0xCD006810, 0xDEADBEEF) && exi.Write32(0xCD00680C, 0x39), "None-device duplex transfer");
        Check(Read32(exi, 0xCD006810) == 0xDEADBEEF, "None-device duplex preserves data like Dolphin");
        auto command = [&](uint32_t word) {
            Check(exi.Write32(0xCD006800, 0) && exi.Write32(0xCD006800, 0x100), "select IPL for EUART");
            Check(exi.Write32(0xCD006810, word) && exi.Write32(0xCD00680C, 0x35), "send EUART command");
        };
        command(0xB0000000);
        Check(exi.Write32(0xCD006810, 0xF2000000) && exi.Write32(0xCD00680C, 0x35), "EUART reset handshake");
        command(0xB0000000);
        Check(exi.Write32(0xCD006810, 0xF3000000) && exi.Write32(0xCD00680C, 0x35), "EUART enable handshake");
        command(0x30000100);
        Check(exi.Write32(0xCD00680C, 0x31) && Read32(exi, 0xCD006810) == 0, "drained EUART queue reports no pending bytes");
        command(0xB0000100);
        Check(exi.Write32(0xCD006810, 0x41000000) && exi.Write32(0xCD00680C, 0x35), "EUART accepts a padded character");
        Check(exi.EuartReady() && exi.EuartBytesWritten() == 1, "EUART output consumes only non-padding bytes");
        exi.Reset();
        Check(Read32(exi, 0xCD006804) == 0 && Read32(exi, 0xCD006808) == 0 &&
              Read32(exi, 0xCD006810) == 0, "reset clears all transfer registers");
        Check(exi.ExiTransferCount() == 0, "reset clears transfer accounting");
        Check(!exi.EuartReady() && exi.EuartBytesWritten() == 0, "reset clears EUART state");
        Kirby::GraphicsFifo().Reset();
        Kirby::Pixels() = Kirby::PixelEngine{};
        Kirby::Interrupts().Reset();
        Check(KirbyMmioWrite16(0xCC00100A, 3), "PE control routed through native MMIO");
        Kirby::Pixels().SetToken(0x5678, true);
        Kirby::Pixels().Finish();
        uint32_t cause = 0;
        Check(KirbyMmioRead32(0xCC003000, cause) && (cause & 0x600) == 0x600,
              "PE token and finish assert their independent PI sources");
        Check(KirbyMmioWrite16(0xCC00100A, 7) && KirbyMmioRead32(0xCC003000, cause) &&
              (cause & 0x600) == 0x400, "PE token acknowledgment leaves PI finish asserted");
        Check(KirbyMmioWrite16(0xCC00100A, 11) && KirbyMmioRead32(0xCC003000, cause) &&
              !(cause & 0x600), "PE finish acknowledgment clears PI finish");
        Check(KirbyMmioWrite16(0xCC00002C, 32) && KirbyMmioWrite16(0xCC000002, 9) &&
              KirbyMmioRead32(0xCC003000, cause) && (cause & 0x800), "CP underflow interrupt reaches PI");
        Check(KirbyMmioWrite16(0xCC000002, 0) && KirbyMmioRead32(0xCC003000, cause) &&
              !(cause & 0x800), "disabled CP reads mask the CP interrupt");
        std::cout << checks << " interrupt controller checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "interrupt check " << checks << " failed: " << error.what() << '\n';
        return 1;
    }
}
