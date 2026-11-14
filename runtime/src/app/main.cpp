// SPDX-License-Identifier: GPL-3.0-only
// Native bootstrap and verified SDK device support. Original game constructors run normally.
#include "abi_bridge.h"
#include "generated/KirbyBootConfig.h"
#include "generated/RuntimeConfig.h"
#include "kirby/devices/interrupts.h"
#include "kirby/devices/ax.h"
#include "kirby/devices/video_si.h"
#include "kirby/threading/scheduler.h"
#include "kirby/threading/continuations.h"
#include "kirby/input/wpad.h"
#include "kirby/boot/boot_memory.h"
#include "kirby/boot/clock.h"
#include "kirby/services/ios_bridge.h"
#include "kirby/graphics/gx_fifo.h"
#include "kirby/graphics/gx_renderer.h"
#include "kirby/graphics/cache.h"
#include "kirby/app/events.h"
#include "kirby/app/frame_log.h"
#include "kirby/settings.h"
#include "audio_backend.h"
#include <memory>
#include "timebase_contract.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <cmath>
#include <cstdlib>

extern "C" void InitializeDataSections();
void KirbyInstallDiagnostics();

namespace {
std::atomic<size_t> rejectedRuntimeRegistrations{0};
std::array<uint8_t, 128> expectedDspInitCode{};
constexpr std::array cacheHooks{
    std::pair{KirbyBootConfig::DCInvalidateRange,"KirbyDCInvalidateRange"},
    std::pair{KirbyBootConfig::DCFlushRange,"KirbyDCFlushRange"},
    std::pair{KirbyBootConfig::DCStoreRange,"KirbyDCStoreRange"},
    std::pair{KirbyBootConfig::DCFlushRangeNoSync,"KirbyDCFlushRangeNoSync"},
    std::pair{KirbyBootConfig::DCStoreRangeNoSync,"KirbyDCStoreRangeNoSync"},
};

void ForeignNative(CpuContext*) {
    throw std::runtime_error("Foreign native hook executed");
}

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
uint8_t* ResolveDma(uint32_t address, uint32_t length) {
    for (const uint32_t base : {0u, 0x80000000u, 0xC0000000u})
        if (address >= base && uint64_t(address) + length <= uint64_t(base) + KirbyBootConfig::MemorySize)
            return Memory::GetPointer(address, length);
    throw std::runtime_error("EXI DMA span lies outside verified MEM1");
}
uint32_t RtcSeconds() {
    // Reuse WiiCompiled's Broadway clock; do not introduce a second RTC epoch.
    for (;;) {
        const auto hi = PPC_Mftbu(), lo = PPC_Mftb();
        if (hi == PPC_Mftbu()) return ((uint64_t(hi) << 32) | lo) / TimeBaseContract::kTicksPerSecond;
    }
}
uint64_t DspTicks() {
    for (;;) {
        const auto hi = PPC_Mftbu(), lo = PPC_Mftb();
        if (hi == PPC_Mftbu()) return (uint64_t(hi) << 32) | lo;
    }
}
uint8_t* ResolveDspMemory(uint32_t address, uint32_t length) {
    if (uint64_t(address) + length <= Kirby::Mem1Size ||
        (address >= 0x10000000 && uint64_t(address) + length <= 0x10000000ull + Kirby::Mem2Size))
        return Memory::GetPointer(address, length);
    return nullptr;
}
bool VerifyDspInitCode() {
    return std::memcmp(expectedDspInitCode.data(), Memory::GetPointer(0x81000000, 128), 128) == 0;
}

void DumpRegisters(const CpuContext& cpu) {
    std::cerr << "[kirby] pc=0x" << std::hex << cpu.pc << " lr=0x" << cpu.lr
              << " ctr=0x" << cpu.ctr << " r1=0x" << cpu.gpr[1]
              << " r2=0x" << cpu.gpr[2] << " r13=0x" << cpu.gpr[13] << '\n';
    // Bounded PowerPC backchain: each callee saves LR in its caller's linkage area.
    // This distinguishes normal GX startup from a secondary OSFatal drawing path.
    auto frame = cpu.gpr[1];
    for (unsigned depth = 0; depth < 12; ++depth) {
        if ((frame & 3) || frame < 0x80000000 || uint64_t(frame)+8 > 0x81800000) break;
        const auto previous = Memory::Read32(frame);
        if ((previous & 3) || previous <= frame || uint64_t(previous)+8 > 0x81800000) break;
        std::cerr << "[kirby] stack frame=0x" << std::hex << frame
                  << " return=0x" << Memory::Read32(previous+4) << '\n';
        frame = previous;
    }
}
void DumpIpcRequest() {
    const auto request = Kirby::Interrupts().IpcMessage();
    if (!request || !Memory::Contains(request, 32)) return;
    const auto command = Memory::Read32(request);
    std::cerr << "[kirby] unsupported IOS request at 0x" << std::hex << request
              << " command=" << std::dec << command;
    if (command == 1) {
        const auto path = Memory::Read32(request + 12);
        std::string name;
        for (uint32_t i = 0; i < 128 && uint64_t(path) + i <= UINT32_MAX && Memory::Contains(path + i, 1); ++i) {
            const auto c = Memory::Read8(path + i);
            if (!c) break;
            name += c >= 32 && c < 127 ? static_cast<char>(c) : '?';
        }
        std::cerr << " open=\"" << name << "\" mode=" << Memory::Read32(request + 16);
    }
    std::cerr << '\n';
}
} // namespace

// Called during static registration, before main. No address from another game
// may take priority over a translated Kirby function. Audited Kirby HLE can be
// added here once its implementation and ABI have been checked.
bool KirbyAcceptRuntimeRegistration(uint32_t address, const char* name) noexcept {
    for (const auto& [expected,label] : cacheHooks)
        if (address==expected && name && std::strcmp(name,label)==0) return true;
    if (address == KirbyBootConfig::DspBootTask && name && std::strcmp(name, "KirbyDspBootTask") == 0) return true;
    if (Kirby::AcceptWpadRegistration(address, name)) return true;
    if (address == KirbyBootConfig::SelectThread && name && std::strcmp(name, "KirbySelectThread") == 0) return true;
    if (address == KirbyBootConfig::LoadContext && name && std::strcmp(name, "KirbyLoadContext") == 0) return true;
    if (address == KirbyBootConfig::IosTransport && name && std::strcmp(name, "KirbyIosSubmit") == 0)
        return true;
    if (address == KirbyBootConfig::RestoreInterrupts && name && std::strcmp(name, "KirbyRestoreInterrupts") == 0)
        return true;
    rejectedRuntimeRegistrations.fetch_add(1, std::memory_order_relaxed);
    return false;
}

int main(int argc, char** argv) {
    std::string_view mode = "--headless";
    std::filesystem::path dataRoot = "disc/DATA";
    float masterVolume = 100;
    bool vsync = true;
    bool gpuResidentCopies = false;
    std::filesystem::path configPath = "local/user/settings.toml";
    bool volumeOverride = false, dataOverride = false, vsyncOverride = false, copiesOverride = false;
    bool explicitConfig = false;
    Kirby::ControlBindings controlBindings;
    std::filesystem::path frameLogPath;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument == "--print-settings" && i + 1 < argc && argv[i+1][0]) {
            mode = argument; configPath = argv[++i]; explicitConfig = true;
        } else if (argument == "--config" && i + 1 < argc && argv[i+1][0]) {
            configPath = argv[++i]; explicitConfig = true;
        } else if (argument == "--volume" && i + 1 < argc) {
            volumeOverride = true;
            char* end = nullptr;
            masterVolume = std::strtof(argv[++i], &end);
            if (!end || end == argv[i] || *end || !std::isfinite(masterVolume) || masterVolume < 0 || masterVolume > 100) {
                std::cerr << "--volume requires a number from 0 to 100\n";
                return 2;
            }
        } else if (argument == "--no-vsync" || argument == "--vsync") {
            vsync = argument == "--vsync"; vsyncOverride = true;
        } else if (argument == "--gpu-resident-copies" || argument == "--ram-copies") {
            gpuResidentCopies = argument == "--gpu-resident-copies"; copiesOverride = true;
        }
        else if (argument == "--frame-log" && i + 1 < argc && argv[i+1][0]) frameLogPath=argv[++i];
        else if (argument == "--game-data" && i + 1 < argc && argv[i+1][0]) {
            dataRoot = argv[++i]; dataOverride = true;
        }
        else if (argument == "--headless" || argument == "--video" || argument == "--video-hidden" || argument == "--probe-configured-controls" || argument == "--probe-ax-task" || argument == "--probe-thread-fault" || argument == "--probe-threads" || argument == "--probe-wpad" || argument == "--probe-sdl-controller" || argument == "--monitor-controller" || argument == "--probe-video-retrace" || argument == "--verify-bootstrap" || argument == "--probe-init" ||
                 argument == "--probe-alarms" || argument == "--probe-interrupt-init" || argument == "--probe-exi-init" || argument == "--probe-si-init" ||
                 argument == "--probe-sram-init" || argument == "--probe-boot-memory" ||
                 argument == "--probe-audio-clock" || argument == "--probe-dsp-init" ||
                 argument == "--probe-ipc-client" || argument == "--probe-stm-init" || argument == "--probe-system-config" || argument == "--probe-disc" || argument == "--probe-es-metadata" || argument == "--probe-ave") mode = argument;
        else {
            std::cerr << "Usage: WiiCompiled [--headless|--video|--video-hidden|--probe-alarms|--probe-ax-task|--probe-thread-fault|--probe-threads|--probe-wpad|--probe-sdl-controller|--monitor-controller|--probe-video-retrace|--verify-bootstrap|--probe-init|--probe-interrupt-init|--probe-exi-init|--probe-si-init|--probe-sram-init|--probe-boot-memory|--probe-audio-clock|--probe-dsp-init|--probe-ipc-client|--probe-stm-init|--probe-system-config|--probe-disc|--probe-es-metadata|--probe-ave] [--game-data DATA] [--volume 0..100] [--no-vsync] [--gpu-resident-copies] [--frame-log CSV]\n";
            std::cerr << "Settings: [--config TOML] or --print-settings TOML; CLI values override saved settings.\n";
            return 2;
        }
    }
    if (explicitConfig || mode == "--video" || mode == "--video-hidden") {
        try {
            const auto settings = Kirby::LoadSettings(configPath);
            controlBindings = settings.controls;
            if (!volumeOverride) masterVolume = settings.masterVolume;
            if (!dataOverride) dataRoot = settings.gameData;
            if (!vsyncOverride) vsync = settings.vsync;
            if (!copiesOverride) gpuResidentCopies = settings.gpuResidentCopies;
        } catch (const std::exception& error) {
            std::cerr << "Settings: " << error.what() << '\n'; return 2;
        }
    }
    if (mode == "--print-settings") {
        std::cout << Kirby::FormatSettings({dataRoot, masterVolume, vsync, gpuResidentCopies, controlBindings});
        return 0;
    }
    if (!frameLogPath.empty() && mode != "--video" && mode != "--video-hidden") {
        std::cerr << "--frame-log requires --video or --video-hidden\n";
        return 2;
    }

    if (mode == "--probe-configured-controls" && !explicitConfig) {
        std::cerr << "--probe-configured-controls requires --config TOML\n"; return 2;
    }
    if (mode == "--video" || mode == "--video-hidden" || mode == "--probe-configured-controls")
        Kirby::ConfigureControlBindings(controlBindings);

    KirbyInstallDiagnostics();
    AudioBackend::Instance().SetMasterVolume(masterVolume / 100);
    auto& cpu = GetPersistentCpuContext();
    cpu = {};
    CpuContextScope cpuScope(&cpu);
    std::unique_ptr<Kirby::GxRenderer> renderer;
    // Release owned audio streams before Aurora tears down SDL globally.
    struct StackCleanup { ~StackCleanup() {
        Kirby::ObserveDataCache({}); Kirby::ShutdownAx(); Kirby::ShutdownThreadStacks();
    } } stackCleanup;
    try {
        std::shared_ptr<Kirby::FrameLog> frameLog;
        if (!frameLogPath.empty()) frameLog=std::make_shared<Kirby::FrameLog>(frameLogPath);
        const auto* originalEntry = TranslatedFunctionRegistry::FindByAddressPtr(KirbyBootConfig::Entry);
        const auto originalInvoker = originalEntry ? originalEntry->rawCpuInvoker : nullptr;
        // Neither native nor handwritten "translated" foreign hooks may win.
        if (mode == "--verify-bootstrap") {
            TranslatedFunctionInfo foreign{};
            foreign.address = KirbyBootConfig::Entry;
            foreign.name = "foreign_native_probe";
            foreign.kind = FunctionKind::Native;
            foreign.entryPoint = reinterpret_cast<void*>(&ForeignNative);
            foreign.rawCpuInvoker = &ForeignNative;
            TranslatedFunctionRegistry::Register(foreign);
            AbiTrampoline<decltype(ForeignNative)> legacy(
                KirbyBootConfig::Entry, "foreign_translated_probe", &ForeignNative,
                FunctionKind::BaseTranslated, false, 0, UINT32_MAX);
        }
        TranslatedFunctionRegistry::Finalize();
        for (const auto& [address,name] : cacheHooks) {
            const auto* hook=TranslatedFunctionRegistry::FindByAddressPtr(address);
            Require(hook && hook->kind==FunctionKind::Native && hook->name==name,
                    "Audited Kirby cache boundary is missing from the native registry");
        }
        Kirby::VerifyWpadRegistrations();
        const auto* ios = TranslatedFunctionRegistry::FindByAddressPtr(KirbyBootConfig::IosTransport);
        Require(ios && ios->kind == FunctionKind::Native && ios->name == "KirbyIosSubmit",
                "Audited Kirby IOS transport is missing from the native registry");
        const auto* restore = TranslatedFunctionRegistry::FindByAddressPtr(KirbyBootConfig::RestoreInterrupts);
        Require(restore && restore->kind == FunctionKind::Native && restore->name == "KirbyRestoreInterrupts",
                "Audited Kirby interrupt boundary is missing from the native registry");
        for (const auto [address, name] : {std::pair{KirbyBootConfig::SelectThread, "KirbySelectThread"},
                                          std::pair{KirbyBootConfig::LoadContext, "KirbyLoadContext"}}) {
            const auto* hook = TranslatedFunctionRegistry::FindByAddressPtr(address);
            Require(hook && hook->kind == FunctionKind::Native && hook->name == name,
                    "Audited Kirby scheduler/context boundary is missing from the native registry");
        }
        const auto* entry = TranslatedFunctionRegistry::FindByAddressPtr(KirbyBootConfig::Entry);
        Require(entry && entry->kind == FunctionKind::BaseTranslated && entry->rawCpuInvoker &&
                entry->rawCpuInvoker == originalInvoker,
                "Kirby entry was not resolved to its translated implementation");
        std::cout << "[kirby] Kirby translated entry resolved: 0x" << std::hex << entry->address << '\n';
        std::cout << "[kirby] rejected foreign runtime registrations: " << std::dec << rejectedRuntimeRegistrations.load() << '\n';
        if (mode == "--verify-bootstrap") {
            cpu.gpr[4] = 0xFFFFFFFFu;
            ApplyRuntimeCallOptions(0x8023BD38u, &cpu);
            Require(cpu.gpr[4] == 0xFFFFFFFFu, "Foreign graphics options modified Kirby's arguments");
            std::cout << "[kirby] foreign native override rejected\n";
            std::cout << "[kirby] foreign translated override rejected\n";
            for (const auto& [address,name] : cacheHooks) {
                for (const uint32_t length : {0u,61u}) {
                    unsigned observed=0;
                    Kirby::ObserveDataCache([&](uint32_t start,uint64_t size,Kirby::CacheOperation operation) {
                        Require(operation==(address==KirbyBootConfig::DCInvalidateRange ? Kirby::CacheOperation::Invalidate : Kirby::CacheOperation::Writeback),"Native cache operation mismatch");
                        Require(start==0x90000000 && size==64,"Native cache observer range mismatch");
                        ++observed;
                    });
                    cpu.gpr[3]=0x90000003; cpu.gpr[4]=length; cpu.gpr[5]=123;
                    cpu.ctr=456; cpu.cr=0x01234567; cpu.xer=0x80000000;
                    InvokeIndirectCpu(address,&cpu);
                    Require(cpu.gpr[3]==(length ? 0x90000043u : 0x90000003u) &&
                            cpu.gpr[4]==(length ? 2u : 0u) && cpu.gpr[5]==(length ? 3u : 123u) &&
                            cpu.ctr==(length ? 0u : 456u) && observed==(length!=0) &&
                            cpu.cr==(length ? 0x51234567u : 0x31234567u),
                            "Native cache dispatcher/register contract mismatch");
                }
            }
            Kirby::ObserveDataCache({});
            std::cout << "[kirby] native cache boundaries verified\n";
            for (const uint32_t initial : {0x12340002u, 0x12348002u}) {
                for (const uint32_t requested : {0u, 1u, 0xFFFFFFFFu}) {
                    cpu.msr = initial;
                    cpu.gpr[3] = requested;
                    cpu.cr = 0x01234567;
                    cpu.xer = 0x80000000;
                    InvokeIndirectCpu(KirbyBootConfig::RestoreInterrupts, &cpu);
                    const auto expected = requested ? initial | 0x8000u : initial & ~0x8000u;
                    const uint32_t comparison = requested == 0 ? 2 : requested == 1 ? 4 : 8;
                    Require(cpu.msr == expected && cpu.gpr[3] == ((initial >> 15) & 1) &&
                            cpu.gpr[4] == initial && cpu.gpr[5] == expected &&
                            cpu.cr == (((comparison | 1) << 28) | 0x01234567),
                            "Native interrupt restore differs from the original SDK register effects");
                }
            }
            std::cout << "[kirby] interrupt restore ABI verified\n";
            return 0;
        }

        // MEM1's cached/uncached/physical aliases share the same backing store.
        // Size and loaded section containment were verified from this DOL.
        Memory::Config memory;
        for (const uint32_t base : {Memory::kMem1CachedBase, Memory::kMem1UncachedBase, Memory::kMem1PhysicalBase})
            memory.regions.push_back({"Kirby MEM1", base, KirbyBootConfig::MemorySize});
        static_assert(Kirby::Mem1Size == KirbyBootConfig::MemorySize);
        static_assert(Kirby::BusClock / 4 == TimeBaseContract::kTicksPerSecond);
        for (const uint32_t base : {0x10000000u, 0x90000000u, 0xD0000000u})
            memory.regions.push_back({"Kirby MEM2", base, Kirby::Mem2Size});
        Memory::Init(memory);
        InitializeDataSections();
        const auto bootData = Kirby::ReadBootData(dataRoot);
        const auto bootLayout = Kirby::InstallBootData(bootData, KirbyBootConfig::ProtectedEnd,
            [](uint32_t address, std::span<const uint8_t> bytes) {
                std::memcpy(Memory::GetPointer(address, bytes.size()), bytes.data(), bytes.size());
            });
        std::cout << "[kirby] IOS56 boot memory: MEM1=24 MiB MEM2=64 MiB; FST=0x" << std::hex
                  << bootLayout.fst << " BI2=0x" << bootLayout.bi2 << '\n';
        if (mode == "--probe-boot-memory") {
            for (const uint32_t offset : {0u, Kirby::Mem2Size - 4}) {
                Memory::Write32(0x90000000 + offset, 0x1234ABCD);
                Require(Memory::Read32(0x10000000 + offset) == 0x1234ABCD &&
                        Memory::Read32(0xD0000000 + offset) == 0x1234ABCD,
                        "MEM2 physical/cached/uncached aliases disagree");
                Memory::Write32(0x90000000 + offset, 0);
            }
            for (const uint32_t base : {0x10000000u, 0x90000000u, 0xD0000000u}) {
                bool rejected = false;
                try { (void)Memory::GetPointer(base + Kirby::Mem2Size, 4); }
                catch (const Memory::AccessViolation&) { rejected = true; }
                Require(rejected, "MEM2 mapping extends past retail 64 MiB");
            }
            Require(Memory::Read32(0x800000F8) / 4 == TimeBaseContract::kTicksPerSecond &&
                    Memory::Read32(0x80003130) == 0x935E0000 && Memory::Read32(0x80003134) == 0x93600000,
                    "Guest clock or IPC buffer disagrees with the IOS56 profile");
            Require((Memory::Read32(0x80003188) >> 16) == 56 &&
                    Memory::Read32(0x80003188) <= Memory::Read32(0x80003140),
                    "Apploader minimum IOS version is incompatible with the native profile");
            Require(std::memcmp(Memory::GetPointer(bootLayout.fst, bootData.fst.size()),
                                bootData.fst.data(), bootData.fst.size()) == 0 &&
                    std::memcmp(Memory::GetPointer(bootLayout.bi2, bootData.bi2.size()),
                                bootData.bi2.data(), bootData.bi2.size()) == 0,
                    "Installed disc metadata differs from the extracted image");
            InvokeIndirectCpu(KirbyBootConfig::InitRegisters, &cpu);
            const auto stack = cpu.gpr[1];
            InvokeIndirectCpu(KirbyBootConfig::OsIpcBufferInit, &cpu);
            InvokeIndirectCpu(KirbyBootConfig::IpcInit, &cpu);
            InvokeIndirectCpu(KirbyBootConfig::IpcGetBufferLo, &cpu);
            Require(cpu.gpr[3] == 0x935E0000, "Kirby IPC buffer lo disagrees with boot memory");
            InvokeIndirectCpu(KirbyBootConfig::IpcGetBufferHi, &cpu);
            Require(cpu.gpr[3] == 0x93600000 && cpu.gpr[1] == stack,
                    "Kirby IPC buffer hi/stack disagrees with boot memory");
            std::cout << "[kirby] boot memory verified; MEM2 aliases and boundaries; disc metadata; IOS56 IPC buffer; 60.75 MHz timebase\n";
            std::cout << "[kirby] Kirby IPC buffer initialization returned; this probe checks buffer layout only\n";
            return 0;
        }
        Kirby::Interrupts().Reset();
        Kirby::GraphicsFifo().Reset();
        Kirby::Pixels() = Kirby::PixelEngine{};
        Kirby::GraphicsFifo().Configure(ResolveDspMemory);
        Kirby::Ios().ConfigureNand("local/user/nand", bootLayout.titleId);
        Kirby::Ios().ConfigureDisc(dataRoot, KirbyBootConfig::DiscCapacity);
        Kirby::Ios().ConfigureDiscTitle(dataRoot, bootLayout.titleId);
        std::memcpy(expectedDspInitCode.data(), Memory::GetPointer(KirbyBootConfig::DspInitCode, 128), 128);
        Kirby::Interrupts().Dsp().Configure(ResolveDspMemory, VerifyDspInitCode, DspTicks);
        Kirby::ConfigureAx();
        if (mode == "--probe-ax-task") { Kirby::ProbeAx(cpu); return 0; }
        if (mode == "--probe-alarms") { Kirby::ProbeAlarms(cpu); return 0; }
        Kirby::ResetDevices();
        Kirby::Clock().Reset();
        Kirby::Interrupts().ConfigureIpl("local/user/sram.bin", ResolveDma, RtcSeconds);
        if (mode == "--probe-thread-fault") { Kirby::ProbeThreads(cpu, true); return 0; }
        if (mode == "--probe-threads") { Kirby::ProbeThreads(cpu); return 0; }
        if (mode == "--probe-wpad") { Kirby::ProbeWpad(cpu); return 0; }
        if (mode == "--probe-sdl-controller") { Kirby::ProbeSdlController(cpu); return 0; }
        if (mode == "--probe-configured-controls") { Kirby::ProbeSdlController(cpu, false, &controlBindings); return 0; }
        if (mode == "--monitor-controller") { Kirby::ProbeSdlController(cpu, true); return 0; }
        if (mode == "--probe-video-retrace") {
            InvokeIndirectCpu(KirbyBootConfig::InitRegisters, &cpu);
            const auto stack = cpu.gpr[1];
            Memory::Write32(KirbyBootConfig::ExceptionTablePointer, 0x80003000);
            InvokeIndirectCpu(KirbyBootConfig::InterruptInit, &cpu);
            InvokeIndirectCpu(KirbyBootConfig::ThreadInit, &cpu);
            const auto thread = Memory::Read32(0x800000E4);
            Require(thread && Memory::Read32(0x800000D4) == thread, "Original SDK thread initializer did not install the main thread");
            InvokeIndirectCpu(KirbyBootConfig::ViInit, &cpu);
            const auto table = Memory::Read32(RuntimeConfig::SDA1_BASE - 5328);
            Require(Memory::Read32(table + 24*4) == KirbyBootConfig::ViInterrupt,
                    "Original VI initializer did not install the audited retrace handler");
            cpu.msr &= ~0x8000u;
            Require(!Kirby::DispatchInterrupt(cpu, KirbyBootConfig::IdleContext, KirbyBootConfig::SelectThread + 0x150),
                    "IRQ delivery ignored the CPU interrupt-enable bit");
            cpu.msr |= 0x8000;
            const auto mask = Kirby::Interrupts().PiMask();
            KirbyMmioWrite32(0xCC003004, 0);
            Require(!Kirby::DispatchInterrupt(cpu, KirbyBootConfig::IdleContext, KirbyBootConfig::SelectThread + 0x150),
                    "IRQ delivery ignored the PI mask");
            KirbyMmioWrite32(0xCC003004, mask);
            for (unsigned reg = 14; reg < 32; ++reg) {
                cpu.gpr[reg] = 0xA5000000u + reg;
                cpu.fpr[reg].raw = 0x400921FB54442D00ull + reg;
            }
            for (unsigned reg = 1; reg < 8; ++reg) cpu.gqr[reg] = reg << 16;
            for (unsigned i = 0; i < 3; ++i) {
                const auto previous = Memory::Read32(RuntimeConfig::SDA1_BASE - 4724);
                InvokeIndirectCpu(KirbyBootConfig::ViWait, &cpu);
                Require(Memory::Read32(RuntimeConfig::SDA1_BASE - 4724) > previous && cpu.gpr[1] == stack &&
                        cpu.gpr[2] == RuntimeConfig::SDA2_BASE && cpu.gpr[13] == RuntimeConfig::SDA1_BASE &&
                        (cpu.msr & 0x8000) &&
                        Memory::Read32(0x800000E4) == thread && Memory::Read32(0x800000D4) == thread &&
                        Memory::Read16(thread + 0x2C8) == 2 && Memory::Read32(KirbyBootConfig::RunQueueBits) == 0,
                        "Original video wait did not resume with its CPU/thread state intact");
                for (unsigned reg = 14; reg < 32; ++reg)
                    Require(cpu.gpr[reg] == 0xA5000000u + reg && cpu.fpr[reg].raw == 0x400921FB54442D00ull + reg,
                            "Scheduler/IRQ path clobbered a nonvolatile CPU register");
                for (unsigned reg = 1; reg < 8; ++reg)
                    Require(cpu.gqr[reg] == reg << 16, "Scheduler/IRQ path clobbered a GQR");
            }
            Require(Kirby::DeliveredInterrupts() >= 3 && Kirby::SchedulerWakeups() == 3,
                    "Video retraces did not pass through IRQ return and scheduler wakeup");
            std::cout << "[kirby] three original VI waits resumed through the original interrupt handler; CPU state and SDK queues preserved\n";
            return 0;
        }
        if (mode == "--probe-ave") {
            InvokeIndirectCpu(KirbyBootConfig::InitRegisters, &cpu);
            const auto stack = cpu.gpr[1];
            constexpr uint32_t payload = 0x81001000;
            for (const uint8_t reg : {0x6A, 0x65}) {
                cpu.gpr[3] = reg == 0x65;
                InvokeIndirectCpu(KirbyBootConfig::RestoreInterrupts, &cpu);
                const auto msr = cpu.msr;
                Memory::Write8(payload, reg); Memory::Write8(payload+1, 1);
                cpu.gpr[3] = 0xE0; cpu.gpr[4] = payload; cpu.gpr[5] = 2;
                InvokeIndirectCpu(KirbyBootConfig::AveI2cWrite, &cpu);
                const auto bytes = Kirby::Devices().Ave().Payload(reg);
                Require(cpu.gpr[3] == 1 && cpu.gpr[1] == stack && cpu.msr == msr &&
                        bytes.size() == 1 && bytes[0] == 1,
                        "Original AV encoder I2C writer failed or changed interrupt state");
            }
            Require(Kirby::Devices().Ave().WriteCount() == 2 && Kirby::Devices().Ave().AckCount() == 6,
                    "AV encoder wire transaction or acknowledgment count is incorrect");
            std::cout << "[kirby] original AV encoder I2C writer completed; two register payloads, six ACKs; stack and interrupt state preserved\n";
            return 0;
        }
        if (mode == "--probe-dsp-init") {
            InvokeIndirectCpu(KirbyBootConfig::InitRegisters, &cpu);
            const auto stack = cpu.gpr[1];
            cpu.gpr[3] = bootLayout.arenaHi;
            InvokeIndirectCpu(KirbyBootConfig::SetMem1ArenaHi, &cpu);
            std::array<uint8_t, 128> originalWork{};
            originalWork.fill(0xA5);
            std::memcpy(Memory::GetPointer(0x81000000, 128), originalWork.data(), 128);
            cpu.pc = KirbyBootConfig::AudioSystemInit;
            InvokeIndirectCpu(cpu.pc, &cpu);
            const auto& dsp = Kirby::Interrupts().Dsp();
            Require(cpu.gpr[1] == stack && dsp.DmaCount() == 2 && dsp.InitCount() == 1 &&
                    dsp.InitMailCount() == 1 && dsp.Control() == 0x804,
                    "Audio system initializer did not complete DSP DMA/reset/mail sequence");
            Require(std::memcmp(Memory::GetPointer(0x81000000, 128), originalWork.data(), 128) == 0,
                    "Audio system initializer did not restore its work buffer");
            Require(std::memcmp(Memory::GetPointer(0x90000000, 32), expectedDspInitCode.data(), 32) == 0,
                    "ARAM DMA bytes do not match the verified DSP initialization microcode");
            std::cout << "[kirby] Kirby audio system initializer returned; two ARAM DMAs; verified DSP init mail; work buffer restored\n";
            return 0;
        }
        if (mode == "--probe-audio-clock") {
            InvokeIndirectCpu(KirbyBootConfig::InitRegisters, &cpu);
            const auto stack = cpu.gpr[1];
            for (const auto clockMode : {1u, 0u}) {
                cpu.gpr[3] = clockMode;
                const auto start = std::chrono::steady_clock::now();
                cpu.pc = KirbyBootConfig::AudioClockInit;
                InvokeIndirectCpu(cpu.pc, &cpu);
                const auto elapsed = std::chrono::steady_clock::now() - start;
                Require(elapsed >= std::chrono::milliseconds(3), "Audio clock skipped its SDK settling delays");
                Require(cpu.gpr[1] == stack && Kirby::Clock().AudioClockReady() &&
                        Kirby::Clock().Mode32Khz() == bool(clockMode),
                        "Audio clock initializer did not select the requested mode");
            }
            std::cout << "[kirby] Kirby audio clock initializer returned; both modes and settling delays verified; DSP still uninitialized\n";
            return 0;
        }
        if (mode == "--probe-interrupt-init" || mode == "--probe-exi-init" || mode == "--probe-si-init" || mode == "--probe-sram-init" || mode == "--probe-ipc-client" || mode == "--probe-stm-init" || mode == "--probe-system-config" || mode == "--probe-disc" || mode == "--probe-es-metadata") {
            InvokeIndirectCpu(KirbyBootConfig::InitRegisters, &cpu);
            const auto initialStack = cpu.gpr[1];
            // Isolated-test prerequisite normally established by OSExceptionInit.
            // The pointer address was checked against its symbol and the DOL's
            // __OSSetExceptionHandler SDA load. This is never used by __start.
            Memory::Write32(KirbyBootConfig::ExceptionTablePointer, 0x80003000);
            cpu.pc = KirbyBootConfig::InterruptInit;
            InvokeIndirectCpu(cpu.pc, &cpu);
            Require(cpu.gpr[1] == initialStack, "Interrupt initializer did not restore its stack");
            // SDK exception number 4 is OS_ERR_EXT_INTERRUPT (OSError.h).
            Require(Memory::Read32(0x80003010) == KirbyBootConfig::ExternalInterruptHandler,
                    "Kirby's external interrupt handler was not installed");
            Require(Kirby::Interrupts().PiMask() == 0xF0 && Kirby::Interrupts().IpcMask() == 0x40000000 &&
                    Kirby::Interrupts().DeviceSourcesMasked() && !Kirby::Interrupts().Pending(),
                    "Interrupt initializer left unexpected source masks or a pending IRQ");
            std::cout << "[kirby] Kirby interrupt initializer returned; external handler installed; device sources masked\n";
            if (mode == "--probe-ipc-client" || mode == "--probe-stm-init" || mode == "--probe-system-config" || mode == "--probe-disc" || mode == "--probe-es-metadata") {
                InvokeIndirectCpu(KirbyBootConfig::OsIpcBufferInit, &cpu);
                cpu.pc = KirbyBootConfig::IpcClientInit;
                InvokeIndirectCpu(cpu.pc, &cpu);
                Require(cpu.gpr[3] == 0 && cpu.gpr[1] == initialStack,
                        "IPC client initialization failed or did not restore its stack");
                InvokeIndirectCpu(KirbyBootConfig::IpcGetBufferLo, &cpu);
                Require(cpu.gpr[3] == 0x935E2000, "IPC client did not reserve its verified 8192-byte heap");
                Require(Memory::Read32(0x80003040 + 27 * 4) == KirbyBootConfig::IpcInterruptHandler &&
                        Memory::Read32(0xCD000004) == 0x38 && (Kirby::Interrupts().PiMask() & 0x4000),
                        "IPC client did not install its handler and enable ack/reply interrupts");
                std::cout << "[kirby] Kirby IPC client initializer returned; 8192-byte heap; handler and mailbox masks installed\n";
                if (mode == "--probe-es-metadata") {
                    InvokeIndirectCpu(KirbyBootConfig::EspInit, &cpu);
                    Require(cpu.gpr[3] == 0, "Original ESP library could not open ES");
                    constexpr uint32_t output = 0x81001000, input = 0x81002000, sizePointer = 0x81003000;
                    std::array<uint8_t, 0x2A4> ticket{};
                    std::ifstream file(dataRoot / "ticket.bin", std::ios::binary);
                    file.read(reinterpret_cast<char*>(ticket.data()), ticket.size());
                    Require(bool(file), "Cannot load expected ticket for native probe");
                    for (const bool supplied : {false, true}) {
                        std::memcpy(Memory::GetPointer(input, ticket.size()), ticket.data(), ticket.size());
                        cpu.gpr[3] = supplied ? input : 0;
                        cpu.gpr[4] = output;
                        InvokeIndirectCpu(KirbyBootConfig::EspTicketView, &cpu);
                        Require(cpu.gpr[3] == 0 && Memory::Read32(output) == uint32_t(ticket[0x1BC]) << 24 &&
                                std::memcmp(Memory::GetPointer(output+4, 0xD4), ticket.data()+0x1D0, 0xD4) == 0,
                                "Original ESP ticket view differs from extracted metadata");
                    }
                    cpu.gpr[3] = 0; cpu.gpr[4] = sizePointer;
                    InvokeIndirectCpu(KirbyBootConfig::EspTmd, &cpu);
                    Require(cpu.gpr[3] == 0 && Memory::Read32(sizePointer) == bootData.tmd.size(),
                            "Original ESP TMD size query disagrees with extraction");
                    cpu.gpr[3] = output; cpu.gpr[4] = sizePointer;
                    InvokeIndirectCpu(KirbyBootConfig::EspTmd, &cpu);
                    Require(cpu.gpr[3] == 0 &&
                            std::memcmp(Memory::GetPointer(output, bootData.tmd.size()), bootData.tmd.data(), bootData.tmd.size()) == 0,
                            "Original ESP TMD query differs from extracted metadata");
                    cpu.gpr[3] = Memory::Read32(input+0x1D0); cpu.gpr[4] = Memory::Read32(input+0x1D4);
                    cpu.gpr[5] = 0; cpu.gpr[6] = sizePointer;
                    Memory::Write32(sizePointer, 0xFFFFFFFF);
                    InvokeIndirectCpu(KirbyBootConfig::EspConsumption, &cpu);
                    Require(cpu.gpr[3] == 0 && Memory::Read32(sizePointer) == 0 && !Kirby::Ios().DiscTitle()->HasTimeLimits(),
                            "Original ESP consumption query disagrees with mounted ticket limits");
                    InvokeIndirectCpu(KirbyBootConfig::EspClose, &cpu);
                    Require(cpu.gpr[3] == 0 && cpu.gpr[1] == initialStack, "ESP close or stack restoration failed");
                    std::cout << "[kirby] original ESP ticket views and TMD queries match extracted metadata; unlimited-ticket consumption verified; library closed\n";
                }
                if (mode == "--probe-disc") {
                    InvokeIndirectCpu(KirbyBootConfig::DvdInit, &cpu);
                    Require(cpu.gpr[3] == 1 && cpu.gpr[1] == initialStack,
                            "Original DVD low initializer failed");
                    auto be = [](const uint8_t* p) {
                        return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
                    };
                    // The mounted FST has already passed structural/path validation.
                    const auto nodes = be(bootData.fst.data()+8);
                    uint32_t bannerOffset = 0;
                    for (uint32_t i = 1; i < nodes; ++i) {
                        const auto* node = bootData.fst.data()+12*i;
                        const auto* name = reinterpret_cast<const char*>(bootData.fst.data()+12*nodes+(be(node)&0xFFFFFF));
                        if (!node[0] && !std::strcmp(name, "opening.bnr")) {
                            Require(!bannerOffset && be(node+8) >= 64, "Ambiguous or short banner fixture");
                            bannerOffset = be(node+4);
                        }
                    }
                    Require(bannerOffset != 0, "Banner is missing from the mounted FST");
                    std::array<uint8_t, 64> expectedBanner{};
                    std::ifstream banner(dataRoot / "files/opening.bnr", std::ios::binary);
                    banner.read(reinterpret_cast<char*>(expectedBanner.data()), expectedBanner.size());
                    Require(bool(banner), "Cannot read expected banner bytes");
                    constexpr uint32_t buffer = 0x81001000;
                    const auto before = Kirby::CompletedIosCallbacks();
                    for (unsigned i = 0; i < 16; ++i) {
                        const bool enabled = i & 1;
                        cpu.gpr[3] = enabled;
                        InvokeIndirectCpu(KirbyBootConfig::RestoreInterrupts, &cpu);
                        Memory::Write32(KirbyBootConfig::DvdProbeResult, 0);
                        std::memset(Memory::GetPointer(buffer, 64), 0xCC, 64);
                        cpu.gpr[3] = buffer;
                        if (i == 0) {
                            cpu.gpr[4] = KirbyBootConfig::DvdProbeCallback;
                            InvokeIndirectCpu(KirbyBootConfig::DvdReadDiskId, &cpu);
                        } else {
                            cpu.gpr[4] = 64;
                            cpu.gpr[5] = i == 1 ? be(bootData.boot.data()+0x424) : bannerOffset;
                            cpu.gpr[6] = KirbyBootConfig::DvdProbeCallback;
                            InvokeIndirectCpu(KirbyBootConfig::DvdRead, &cpu);
                        }
                        Require(cpu.gpr[3] == 1 && Memory::Read32(KirbyBootConfig::DvdProbeResult) == uint32_t(enabled),
                                "DVD callback delivery disagrees with interrupt state at transport return");
                        cpu.gpr[3] = 1;
                        InvokeIndirectCpu(KirbyBootConfig::RestoreInterrupts, &cpu);
                        const auto* expected = i == 0 ? bootData.boot.data() :
                                               i == 1 ? bootData.fst.data() : expectedBanner.data();
                        const uint32_t length = i == 0 ? 32 : 64;
                        Require(Memory::Read32(KirbyBootConfig::DvdProbeResult) == 1 &&
                                cpu.gpr[1] == initialStack &&
                                Kirby::CompletedIosCallbacks() == before+i+1 &&
                                std::memcmp(Memory::GetPointer(buffer, length), expected, length) == 0,
                                "Original DVD callback, context reuse, or extracted bytes failed verification");
                    }
                    std::cout << "[kirby] Kirby DVD low initializer and 16 reads completed; disc ID, FST and banner bytes verified; original callbacks and context reuse\n";
                    std::cout << "[kirby] DVD completions delivered at transport return when enabled, deferred when masked\n";
                    std::cout << "[kirby] extracted DATA files=" << std::dec << Kirby::Ios().Disc()->FileCount()
                              << " reads=" << Kirby::Ios().Disc()->ReadCount()
                              << " bytes=" << Kirby::Ios().Disc()->BytesRead() << '\n';
                }
                if (mode == "--probe-system-config") {
                    cpu.pc = KirbyBootConfig::ScInit;
                    InvokeIndirectCpu(cpu.pc, &cpu);
                    InvokeIndirectCpu(KirbyBootConfig::ScCheckStatus, &cpu);
                    Require(cpu.gpr[3] == 1 && Kirby::CompletedIosCallbacks() == 0,
                            "Configuration callbacks ran while external interrupts were disabled");
                    cpu.gpr[3] = 1;
                    InvokeIndirectCpu(KirbyBootConfig::RestoreInterrupts, &cpu);
                    for (unsigned step = 0; step < 16; ++step) {
                        InvokeIndirectCpu(KirbyBootConfig::ScCheckStatus, &cpu);
                        if (cpu.gpr[3] == 0) break;
                    }
                    Require(cpu.gpr[3] == 0 && cpu.gpr[1] == initialStack &&
                            Memory::Read32(KirbyBootConfig::ScItemCount) > 0 &&
                            Kirby::CompletedIosCallbacks() >= 6,
                            "System configuration did not finish loading and parsing NAND settings");
                    InvokeIndirectCpu(KirbyBootConfig::ScGetLanguage, &cpu);
                    Require(cpu.gpr[3] <= 9, "Parsed system language is invalid");
                    std::cout << "[kirby] Kirby system configuration loaded; parsed items="
                              << std::dec << Memory::Read32(KirbyBootConfig::ScItemCount)
                              << " language=" << cpu.gpr[3] << "; original NAND/ES wrappers and async callbacks\n";
                    std::cout << "[kirby] IOS callback delivery follows guest interrupt enable; completed="
                              << Kirby::CompletedIosCallbacks() << '\n';
                }
                if (mode == "--probe-stm-init") {
                    cpu.pc = KirbyBootConfig::StmInit;
                    InvokeIndirectCpu(cpu.pc, &cpu);
                    Require(cpu.gpr[3] == 1 && cpu.gpr[1] == initialStack &&
                            Memory::Read32(KirbyBootConfig::StmReady) == 1 &&
                            Memory::Read32(KirbyBootConfig::StmEhRegistered) == 1 &&
                            Kirby::Ios().HasHandle(Memory::Read32(KirbyBootConfig::StmImDesc)) &&
                            Kirby::Ios().HasHandle(Memory::Read32(KirbyBootConfig::StmEhDesc)) &&
                            Kirby::Ios().PendingEventRequest(),
                            "Original STM initializer did not open devices and retain its event request");
                    // Isolated probe storage, unused by this initializer. Exercise
                    // more requests than fit in the real 8192-byte IPC heap.
                    constexpr uint32_t path = 0x81001000;
                    constexpr char device[] = "/dev/stm/immediate";
                    std::memcpy(Memory::GetPointer(path, sizeof(device)), device, sizeof(device));
                    for (unsigned i = 0; i < 256; ++i) {
                        cpu.gpr[3] = path;
                        cpu.gpr[4] = 0;
                        InvokeIndirectCpu(KirbyBootConfig::IosOpen, &cpu);
                        const auto fd = cpu.gpr[3];
                        Require(Kirby::Ios().HasHandle(fd), "IOS open exhausted or corrupted its original SDK heap");
                        InvokeIndirectCpu(KirbyBootConfig::IosClose, &cpu);
                        Require(cpu.gpr[3] == 0 && !Kirby::Ios().HasHandle(fd) && cpu.gpr[1] == initialStack,
                                "IOS close failed to release its descriptor or preserve the guest stack");
                    }
                    Require(Kirby::Ios().PendingEventRequest() && !Kirby::Ios().TakeCompletion(),
                            "Unrelated requests completed the pending STM event");
                    InvokeIndirectCpu(KirbyBootConfig::StmUnregisterEvent, &cpu);
                    Require(cpu.gpr[3] == 0 && !Kirby::Ios().PendingEventRequest() &&
                            Memory::Read32(KirbyBootConfig::StmEhRegistered) == 0,
                            "Original STM release did not cancel its event request");
                    cpu.msr |= 0x8000; // Explicit prerequisite for this isolated callback probe.
                    const CpuContext beforeCallback = cpu;
                    const auto callbacksBefore = Kirby::CompletedIosCallbacks();
                    Kirby::DrainIosCompletions(&cpu);
                    Require(std::memcmp(&cpu, &beforeCallback, sizeof(cpu)) == 0 &&
                            !Kirby::Ios().PendingEventRequest() && !Kirby::Ios().TakeCompletion() &&
                            Kirby::CompletedIosCallbacks() == callbacksBefore + 1,
                            "STM callback changed the caller's registers or completed twice");
                    std::cout << "[kirby] Kirby STM initializer returned; owned descriptors; pending event; 256 open/close cycles through original SDK heap\n";
                    std::cout << "[kirby] STM release and original callback completed; caller context preserved; request freed\n";
                }
            }
            if (mode == "--probe-exi-init" || mode == "--probe-si-init" || mode == "--probe-sram-init") {
                cpu.pc = KirbyBootConfig::ExiInit;
                InvokeIndirectCpu(cpu.pc, &cpu);
                Require(cpu.gpr[1] == initialStack, "EXI initializer did not restore its stack");
                Require(Kirby::Interrupts().ExiTransferCount() > 2 && Kirby::Interrupts().EuartReady() &&
                        Kirby::Interrupts().EuartBytesWritten() > 0,
                        "EXI initializer did not complete identification and diagnostic output");
                for (const auto base : {0xCD006800u, 0xCD006814u, 0xCD006828u}) {
                    Require((Memory::Read32(base) & 0x380) == 0 && (Memory::Read32(base + 12) & 1) == 0,
                            "EXI initializer left a device selected or an active transfer");
                }
                Require(!Kirby::Interrupts().Pending(), "EXI initializer left an unmasked pending interrupt");
                std::cout << "[kirby] Kirby EXI initializer returned; diagnostic output delivered; completed transfers="
                          << Kirby::Interrupts().ExiTransferCount() << '\n';
                if (mode == "--probe-si-init" || mode == "--probe-sram-init") {
                    cpu.pc = KirbyBootConfig::SiInit;
                    InvokeIndirectCpu(cpu.pc, &cpu);
                    Require(cpu.gpr[1] == initialStack, "SI initializer did not restore its stack");
                    Require(Memory::Read32(0xCD006430) == 0x01EC0200 && (Kirby::Interrupts().PiMask() & 8),
                            "SI initializer did not configure NTSC polling and its PI interrupt mask");
                    Require(!(Memory::Read32(0xCD006434) & 1), "SI transfer remained active");
                    std::cout << "[kirby] Kirby SI initializer returned; polling configured; transfers="
                              << Kirby::Devices().TransferCount() << '\n';
                    if (mode == "--probe-sram-init") {
                        cpu.pc = KirbyBootConfig::SramInit;
                        InvokeIndirectCpu(cpu.pc, &cpu);
                        Require(cpu.gpr[1] == initialStack, "SRAM initializer did not restore its stack");
                        Require(Memory::Read32(KirbyBootConfig::SramControl + 0x4C) == 1 &&
                                Memory::Read32(KirbyBootConfig::SramControl + 0x48) == 0 &&
                                Memory::Read32(KirbyBootConfig::SramControl + 0x40) == 64,
                                "SRAM initializer did not synchronize and unlock its control block");
                        std::array<char,64> saved{};
                        std::ifstream input("local/user/sram.bin", std::ios::binary);
                        Require(bool(input.read(saved.data(), saved.size())) &&
                                std::memcmp(saved.data(), Memory::GetPointer(KirbyBootConfig::SramControl, saved.size()), saved.size()) == 0,
                                "SRAM guest data disagrees with its persistent backing");
                        std::cout << "[kirby] Kirby SRAM initializer returned; 64 bytes synchronized with persistent storage\n";
                    }
                }
            }
            return 0;
        }
        if (mode == "--headless" || mode == "--video" || mode == "--video-hidden") {
            renderer = std::make_unique<Kirby::GxRenderer>([](uint32_t address, uint32_t size) -> std::span<const uint8_t> {
                auto* memory = ResolveDspMemory(address & 0x1FFFFFFF, size);
                return memory ? std::span<const uint8_t>(memory, size) : std::span<const uint8_t>{};
            }, [](uint32_t address, std::span<const uint8_t> bytes) {
                auto* memory = ResolveDspMemory(address, bytes.size());
                Require(memory != nullptr, "XFB write lies outside MEM1/MEM2");
                std::memcpy(memory, bytes.data(), bytes.size());
            });
            if (mode == "--video" || mode == "--video-hidden") {
                renderer->OpenWindow(mode == "--video-hidden", vsync);
                renderer->SetGpuResidentTextureCopies(gpuResidentCopies);
                renderer->SetGpuResidentDisplayCopies(gpuResidentCopies);
                renderer->SetTextureSnapshotReuse(gpuResidentCopies);
                if (frameLog) renderer->ObservePresentation([frameLog](const auto& sample) { frameLog->Write(sample); });
                if (gpuResidentCopies) std::cout << "[kirby] experimental GPU-resident texture and display copies enabled\n";
                Kirby::SetWpadWindowActive(true);
            }
            Kirby::Devices().ObserveVideo([&](const auto& output) { renderer->PollVideo(output); });
            Kirby::ObserveDataCache([&](uint32_t address,uint64_t size,Kirby::CacheOperation operation) {
                if (operation==Kirby::CacheOperation::Writeback) renderer->WritebackTextureCopiesForRange(address,size);
                else renderer->FlushTextureCopiesForRange(address,size);
            });
            Kirby::GraphicsFifo().Configure(ResolveDspMemory, [&](auto bytes) { renderer->Feed(bytes); });
        }
        cpu.pc = mode == "--probe-init" ? KirbyBootConfig::InitRegisters : KirbyBootConfig::Entry;
        cpu.lr = 0xFFFFFFFFu;
        std::cout << "[kirby] entering " << (mode == "--probe-init" ? "__init_registers" : "__start")
                  << " at 0x" << std::hex << cpu.pc << std::endl;
        InvokeIndirectCpu(cpu.pc, &cpu);

        if (mode == "--probe-init") {
            Require(cpu.gpr[2] == RuntimeConfig::SDA2_BASE && cpu.gpr[13] == RuntimeConfig::SDA1_BASE,
                    "Translated register initialization disagrees with the verified DOL");
            Require(cpu.gpr[1] >= KirbyBootConfig::MemoryBase &&
                    uint64_t(cpu.gpr[1]) < uint64_t(KirbyBootConfig::MemoryBase) + KirbyBootConfig::MemorySize,
                    "Game's initial stack is outside MEM1");
            std::cout << "[kirby] r2=0x" << std::hex << cpu.gpr[2] << " r13=0x" << cpu.gpr[13]
                      << " r1=0x" << cpu.gpr[1] << "; stack is inside MEM1\n";
            return 0;
        }
        std::cout << "[kirby] __start returned; no graphical milestone is implied\n";
        return 0;
    } catch (const Kirby::ExitRequested&) {
        std::cout << "[kirby] host exit requested; shutting down native runtime\n";
        return 0;
    } catch (const Memory::AccessViolation& error) {
        std::cerr << "[kirby] blocked: guest memory access 0x" << std::hex << error.address()
                  << ": " << error.what() << '\n';
    } catch (const std::exception& error) {
        std::cerr << "[kirby] blocked: " << error.what() << '\n';
    }
    if (renderer) std::cerr << std::dec << "[kirby] GX stream: packets=" << renderer->Stream().Packets()
        << " register writes=" << renderer->RegisterWrites() << " display lists=" << renderer->Stream().DisplayLists()
        << " pending bytes=" << renderer->Stream().PendingBytes()
        << " XFB copies=" << renderer->DisplayCopies() << " VI submissions=" << renderer->Presentations() << '\n';
    DumpRegisters(cpu);
    std::cerr << "[kirby] scheduler: IRQ returns=" << Kirby::DeliveredInterrupts()
              << " resumed waits=" << Kirby::SchedulerWakeups()
              << " native stack switches=" << Kirby::ThreadStackSwitches()
              << " alarms=" << Kirby::DeliveredAlarms() << '\n';
    std::cerr << "[kirby] PI mask=0x" << std::hex << Kirby::Interrupts().PiMask()
              << " pending=" << Kirby::Interrupts().Pending() << '\n';
    std::cerr << "[kirby] completed EXI transfers=" << std::dec << Kirby::Interrupts().ExiTransferCount() << '\n';
    const auto& dsp = Kirby::Interrupts().Dsp();
    std::cerr << "[kirby] DSP boot: DMA=" << dsp.DmaCount() << " init=" << dsp.InitCount()
              << " mail=" << dsp.InitMailCount() << " AX tasks=" << dsp.TaskBootCount()
              << " audio blocks=" << dsp.Audio().Blocks()
              << " skipped audio blocks=" << dsp.Audio().DroppedBlocks() << '\n';
    DumpIpcRequest();
    if (Memory::Contains(KirbyBootConfig::StmReady, 4))
        std::cerr << "[kirby] STM: ready=" << Memory::Read32(KirbyBootConfig::StmReady)
                  << " pending request=0x" << std::hex << Kirby::Ios().PendingEventRequest() << '\n';
    if (Memory::Contains(KirbyBootConfig::ScItemCount, 4))
        std::cerr << "[kirby] SC: items=" << std::dec << Memory::Read32(KirbyBootConfig::ScItemCount)
                  << " IOS callbacks=" << Kirby::CompletedIosCallbacks()
                  << " KD RTC synchronized=" << Kirby::Ios().NetworkRtcCounter().has_value() << '\n';
    if (const auto* disc = Kirby::Ios().Disc())
        std::cerr << "[kirby] disc: files=" << disc->FileCount() << " reads=" << disc->ReadCount()
                  << " bytes=" << disc->BytesRead() << '\n';
    std::cerr << "[kirby] GX FIFO: control=0x" << std::hex << Kirby::GraphicsFifo().Control()
              << " base=0x" << Kirby::GraphicsFifo().Base() << " end=0x" << Kirby::GraphicsFifo().End()
              << std::dec << " queued=" << Kirby::GraphicsFifo().Distance()
              << " bursts=" << Kirby::GraphicsFifo().Bursts()
              << " consumed=" << Kirby::GraphicsFifo().ConsumedBursts() << '\n';
    std::cerr << "[kirby] AV encoder: writes=" << Kirby::Devices().Ave().WriteCount()
              << " ACKs=" << Kirby::Devices().Ave().AckCount() << '\n';
    return 1;
}
