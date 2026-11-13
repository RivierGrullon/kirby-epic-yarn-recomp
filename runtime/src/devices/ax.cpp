// SPDX-License-Identifier: GPL-3.0-only
// Uses WiiCompiled's GPL-3.0 AXWii mixer through its hardware-only adapter.
#include "kirby/devices/ax.h"
#include "kirby/devices/interrupts.h"
#include "kirby/threading/scheduler.h"
#include "generated/KirbyBootConfig.h"
#include "hle/audio/ax_dsp.h"
#include "memory.h"
#include "ppc_runtime.h"
#include "audio_backend.h"
#include "timebase_contract.h"
#include <array>
#include <cstring>
#include <iostream>

namespace {
std::array<uint8_t, 0x2000> expectedCode{};
bool initialized = false;
bool audioOpen = false;
bool PushAudio(const uint8_t* data, uint32_t bytes) {
    if (!audioOpen) audioOpen = AudioBackend::Instance().Init(32000, 2);
    return audioOpen && AudioBackend::Instance().PushWiiAiSamplesBE16(data, bytes);
}
uint64_t Ticks() {
    for (;;) { const auto hi=PPC_Mftbu(), lo=PPC_Mftb(); if (hi == PPC_Mftbu()) return (uint64_t(hi)<<32)|lo; }
}
uint8_t* Resolve(uint32_t address, uint32_t size) {
    if (uint64_t(address)+size <= 0x01800000 ||
        (address >= 0x10000000 && uint64_t(address)+size <= 0x14000000)) return Memory::GetPointer(address,size);
    return nullptr;
}
bool Boot(const Kirby::DspTaskImage& image) {
    if (image.length != expectedCode.size() || image.iram || image.dramLength || image.entry != 0x10)
        return false;
    const auto physical = image.address & 0x1FFFFFFF;
    if (!Memory::Contains(physical, image.length) ||
        std::memcmp(Memory::GetPointer(physical, image.length), expectedCode.data(), image.length)) return false;
    // Preparation pins SHA-256 and vectors. This HashEctor identity selects the
    // same new-command/new-filter AXWii layout in WiiCompiled and Dolphin.
    AxDspHle::InitHardware(0x4CC52064);
    initialized = true;
    std::cout << "[kirby] verified AXWii task loaded: HashEctor=0x4cc52064 bytes=8192\n";
    return true;
}
bool ReadMail(uint32_t& mail) {
    if (!AxDspHle::CheckMailFromDSP()) return false;
    mail = AxDspHle::ReadMailFromDSP();
    return true;
}
}
namespace Kirby {
void ShutdownAx() {
    if (audioOpen) { AudioBackend::Instance().Shutdown(); audioOpen = false; }
    if (!initialized) return;
    AxDspHle::Stop(); AxDspHle::ShutdownMixWorker(); initialized = false;
}
void ConfigureAx() {
    // Capture before guest execution, from the already hash-verified DOL image.
    std::memcpy(expectedCode.data(), Memory::GetPointer(KirbyBootConfig::AxMicrocode, expectedCode.size()), expectedCode.size());
    Interrupts().Dsp().ConfigureTaskBackend(Boot, ReadMail, AxDspHle::SendMailToDSP, ShutdownAx,
        [] { return GetInterruptState().context != 0; });
    static_assert(TimeBaseContract::kTicksPerSecond == 60750000);
    Interrupts().Dsp().Audio().Configure(Resolve, Ticks, PushAudio);
}
}
