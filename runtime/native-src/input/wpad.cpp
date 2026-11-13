// SPDX-License-Identifier: GPL-3.0-only
// RK5E01 ABI: Swiftshine/key WPAD.c. Native input: Aurora (MIT).
#include "hle_stubs.h"
#include "generated/KirbyBootConfig.h"
#include "kirby/input/wpad.h"
#include "kirby/app/events.h"
#include "kirby/boot/boot_memory.h"
#include <aurora/aurora.h>
#include <aurora/wpad.h>
#include <dolphin/pad.h>
#include <array>
#include <chrono>
#include <cstring>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
struct Channel {
    uint32_t connect = 0, extension = 0, sampling = 0;
    uint32_t buffer = 0, count = 0, index = 0;
};
struct Event {
    uint32_t callback, channel, result; bool sample; WPADStatus report{};
    uint32_t infoOutput = 0;
    WPADInfo info{};
};
struct InfoRequest { WPADInfo info{}; uint32_t output=0, callback=0; bool busy=false; };
std::array<InfoRequest, 4> infoRequests;
std::array<Channel, 4> channels;
std::deque<Event> events;
std::array<std::deque<uint32_t>,4> dpdCallbacks;
bool initialized = false, polling = false, windowActive = false;
std::chrono::steady_clock::time_point lastPoll;

void CheckChannel(uint32_t chan) {
    if (chan >= channels.size()) throw std::runtime_error("WPAD channel outside RK5E01 range");
}
uint8_t* Ram(uint32_t address, uint32_t size) {
    // Never pass guest addresses or host-endian structs to Aurora.
    for (auto base : {0u, 0x80000000u, 0xC0000000u})
        if (address && address >= base && uint64_t(address) + size <= uint64_t(base) + Kirby::Mem1Size)
            return Memory::GetPointer(address, size);
    for (auto base : {0x10000000u, 0x90000000u, 0xD0000000u})
        if (address >= base && uint64_t(address) + size <= uint64_t(base) + Kirby::Mem2Size)
            return Memory::GetPointer(address, size);
    throw std::runtime_error("WPAD output exceeds guest RAM");
}
void Connected(int32_t chan, int32_t result) {
    events.push_back({channels[chan].connect, uint32_t(chan), uint32_t(result), false});
}
void Extended(int32_t chan, int32_t result) {
    events.push_back({channels[chan].extension, uint32_t(chan), uint32_t(result), false});
}
void Sampled(int32_t chan) {
    Event event{channels[chan].sampling, uint32_t(chan), 0, true};
    WPADRead(chan, &event.report);
    events.push_back(event);
}
void DpdDone(int32_t chan, int32_t result) {
    auto& callbacks=dpdCallbacks[chan];
    if (callbacks.empty()) throw std::runtime_error("DPD completion has no pending request");
    const auto callback=callbacks.front(); callbacks.pop_front();
    events.push_back({callback,uint32_t(chan),uint32_t(result),false});
}
void InfoDone(int32_t chan, int32_t result) {
    auto& request = infoRequests[chan];
    Event event{request.callback, uint32_t(chan), uint32_t(result), false};
    if (result == WPAD_ERR_NONE) { event.infoOutput=request.output; event.info=request.info; }
    events.push_back(event);
    request.busy = false;
}
void WriteInfo(uint32_t address, const WPADInfo& info) {
    Ram(address, 0x18);
    Memory::Write32(address,info.dpd); Memory::Write32(address+4,info.speaker);
    Memory::Write32(address+8,info.attach); Memory::Write32(address+12,info.lowBat);
    Memory::Write32(address+16,info.nearempty);
    Memory::Write8(address+20,info.battery); Memory::Write8(address+21,info.led);
    Memory::Write8(address+22,info.protocol); Memory::Write8(address+23,info.firmware);
}
uint16_t HorizontalButtons(uint16_t buttons) {
    // The game rotates the Wii Remote D-pad for its sideways grip. Convert
    // conventional host directions into the corresponding physical remote keys.
    uint16_t result = buttons & ~(WPAD_BUTTON_LEFT | WPAD_BUTTON_RIGHT | WPAD_BUTTON_UP |
        WPAD_BUTTON_DOWN | WPAD_BUTTON_A | WPAD_BUTTON_B | WPAD_BUTTON_1 | WPAD_BUTTON_2);
    const std::pair<uint16_t,uint16_t> mapping[]{
        {WPAD_BUTTON_LEFT,WPAD_BUTTON_UP}, {WPAD_BUTTON_RIGHT,WPAD_BUTTON_DOWN},
        {WPAD_BUTTON_UP,WPAD_BUTTON_RIGHT}, {WPAD_BUTTON_DOWN,WPAD_BUTTON_LEFT},
        {WPAD_BUTTON_A,WPAD_BUTTON_2}, {WPAD_BUTTON_B,WPAD_BUTTON_1},
        {WPAD_BUTTON_1,WPAD_BUTTON_B}, {WPAD_BUTTON_2,WPAD_BUTTON_A}};
    for (auto [host,remote] : mapping) if (buttons & host) result |= remote;
    return result;
}
void WriteReport(uint32_t chan, uint32_t address, const WPADStatus* snapshot = nullptr) {
    CheckChannel(chan);
    static_assert(sizeof(WPADStatus) == 0x2A && offsetof(WPADStatus, err) == 0x29);
    Ram(address, 0x2A);
    WPADStatus status{};
    if (snapshot) status = *snapshot; else WPADRead(chan, &status);
    Memory::Write16(address, HorizontalButtons(status.button));
    Memory::Write16(address+2, status.accX);
    Memory::Write16(address+4, status.accY);
    Memory::Write16(address+6, status.accZ);
    for (unsigned i = 0; i < 4; ++i) {
        const auto offset = address + 8 + i*8;
        Memory::Write16(offset, status.obj[i].x);
        Memory::Write16(offset+2, status.obj[i].y);
        Memory::Write16(offset+4, status.obj[i].size);
        Memory::Write8(offset+6, status.obj[i].traceId);
        Memory::Write8(offset+7, 0);
    }
    Memory::Write8(address+0x28, status.dev);
    Memory::Write8(address+0x29, status.err);
}
}

extern "C" void KirbyWPADInit() {
    if (initialized) return;
    Kirby::ConfigureControllerDefaults();
    WPADInit();
    for (int i = 0; i < 4; ++i) {
        aurora_wpad_set_device(i, WPAD_DEV_CORE);
        WPADSetConnectCallback(i, Connected);
        WPADSetExtensionCallback(i, Extended);
        WPADSetSamplingCallback(i, Sampled);
    }
    initialized = true;
    std::cout << "[kirby] WPAD initialized with Aurora native controller backend\n";
}
extern "C" int32_t KirbyWPADGetStatus() { return WPADGetStatus(); }
extern "C" int32_t KirbyWPADProbe(uint32_t chan, uint32_t type) {
    CheckChannel(chan);
    if (type) Ram(type, 4);
    uint32_t device;
    const auto result = WPADProbe(chan, &device);
    if (type) Memory::Write32(type, device);
    return result;
}
extern "C" void KirbyWPADRead(uint32_t chan, uint32_t output) { WriteReport(chan, output); }
extern "C" uint32_t KirbyWPADSetConnectCallback(uint32_t chan, uint32_t callback) {
    CheckChannel(chan); return std::exchange(channels[chan].connect, callback);
}
extern "C" uint32_t KirbyWPADSetExtensionCallback(uint32_t chan, uint32_t callback) {
    CheckChannel(chan); return std::exchange(channels[chan].extension, callback);
}
extern "C" uint32_t KirbyWPADSetSamplingCallback(uint32_t chan, uint32_t callback) {
    CheckChannel(chan); return std::exchange(channels[chan].sampling, callback);
}
extern "C" uint32_t KirbyWPADGetDataFormat(uint32_t chan) {
    CheckChannel(chan); return WPADGetDataFormat(chan);
}
extern "C" int32_t KirbyWPADSetDataFormat(uint32_t chan, uint32_t format) {
    CheckChannel(chan);
    if (format > WPAD_FMT_CORE_ACC_DPD)
        throw std::runtime_error("WPAD extension report format needs a reviewed Kirby serializer");
    return WPADSetDataFormat(chan, format);
}
extern "C" void KirbyWPADSetAutoSamplingBuf(uint32_t chan, uint32_t buffer, uint32_t count) {
    CheckChannel(chan);
    if (buffer) {
        if (!count || uint64_t(count)*0x2A > UINT32_MAX)
            throw std::runtime_error("WPAD sampling buffer size is invalid");
        std::memset(Ram(buffer, count*0x2A), 0, count*0x2A);
        const auto error = WPADProbe(chan, nullptr) == WPAD_ERR_NO_CONTROLLER ? -1 : -4;
        for (uint32_t i = 0; i < count; ++i) Memory::Write8(buffer + i*0x2A + 0x29, error);
    }
    auto& state = channels[chan]; state.buffer = buffer; state.count = count; state.index = 0;
}
extern "C" void KirbyWPADGetAccGravityUnit(uint32_t chan, uint32_t type, uint32_t output) {
    CheckChannel(chan); Ram(output, 6);
    WPADAcc acc{}; WPADGetAccGravityUnit(chan, type, &acc);
    Memory::Write16(output, acc.x); Memory::Write16(output+2, acc.y); Memory::Write16(output+4, acc.z);
}
extern "C" void KirbyWPADControlMotor(uint32_t chan, uint32_t command) {
    CheckChannel(chan); WPADControlMotor(chan, command);
}
extern "C" uint32_t KirbyWPADIsDpdEnabled(uint32_t chan) {
    CheckChannel(chan); return WPADIsDpdEnabled(chan);
}
extern "C" void KirbyWPADControlDpd(CpuContext* ctx) {
    const auto chan=ctx->gpr[3], command=ctx->gpr[4], callback=ctx->gpr[5];
    CheckChannel(chan);
    if (command!=0 && command!=1 && command!=3 && command!=5)
        throw std::runtime_error("WPAD DPD command is outside the audited RK5E01 modes");
    auto result=WPADProbe(chan,nullptr);
    if (result==WPAD_ERR_NONE && bool(WPADIsDpdEnabled(chan))!=(command!=0)) {
        dpdCallbacks[chan].push_back(callback);
        // Aurora exposes pointer enablement; the report serializer owns the format.
        result=WPADControlDpd(chan,command ? 1 : 0,DpdDone);
    } else if (callback) {
        CpuContext scratch=*ctx; scratch.gpr[3]=chan; scratch.gpr[4]=result;
        CpuContextScope scope(&scratch); InvokeIndirectCpu(callback,&scratch);
    }
    ctx->gpr[3]=result;
}
extern "C" uint32_t KirbyWPADGetDpdSensitivity() { return WPADGetDpdSensitivity(); }
extern "C" uint32_t KirbyWPADiGetMplsStatus(uint32_t chan) {
    CheckChannel(chan); return 0; // The selected device is a core Remote, without MotionPlus.
}
extern "C" void KirbyWPADDisconnect(uint32_t chan) { CheckChannel(chan); WPADDisconnect(chan); }
extern "C" void KirbyWPADGetInfoAsync(CpuContext* ctx) {
    const auto chan=ctx->gpr[3], output=ctx->gpr[4], callback=ctx->gpr[5];
    CheckChannel(chan); Ram(output,0x18);
    auto& request = infoRequests[chan];
    auto result = WPADProbe(chan,nullptr);
    if (result == WPAD_ERR_NONE && request.busy) result = -2; // SDK WPAD_ECOMM.
    if (result == WPAD_ERR_NONE) {
        request.output=output; request.callback=callback; request.busy=true;
        result=WPADGetInfoAsync(chan,&request.info,InfoDone);
    } else if (callback) {
        // The SDK calls failure callbacks immediately, with the caller's EE state.
        CpuContext scratch=*ctx;
        scratch.gpr[3]=chan; scratch.gpr[4]=result;
        CpuContextScope scope(&scratch);
        InvokeIndirectCpu(callback,&scratch);
    }
    ctx->gpr[3]=result;
}

namespace Kirby {
void SetWpadWindowActive(bool active) { windowActive = active; }
void PollWpad(CpuContext& cpu, bool force) {
    if (!initialized || polling || !(cpu.msr & 0x8000)) return;
    const auto now = std::chrono::steady_clock::now();
    if (!force && now-lastPoll < std::chrono::milliseconds(5)) return;
    struct Guard { Guard() { polling = true; } ~Guard() { polling = false; } } guard;
    lastPoll = now;
    // Aurora's noexcept update only queues host events. Invoke guest callbacks
    // afterwards, at a boundary where translated resident registers are flushed.
    if (windowActive) PollHostEvents(); else aurora_wpad_update();
    auto pending = std::exchange(events, {});
    for (const auto& event : pending) {
        if (event.infoOutput) WriteInfo(event.infoOutput,event.info);
        auto& state = channels[event.channel];
        if (event.sample && state.buffer && state.count) {
            WriteReport(event.channel, state.buffer + state.index*0x2A, &event.report);
            state.index = (state.index+1) % state.count;
        }
        if (!event.callback) continue;
        CpuContext scratch = cpu;
        scratch.msr &= ~0x8000u;
        scratch.gpr[3] = event.channel;
        if (!event.sample) scratch.gpr[4] = event.result;
        CpuContextScope scope(&scratch);
        InvokeIndirectCpu(event.callback, &scratch);
    }
}
}

// Addresses are RK5E01 addresses; do not use MKW region translation.
#undef REGISTER_NATIVE_FUNCTION
#define REGISTER_NATIVE_FUNCTION(address, fn) \
    static AbiTrampoline<decltype(fn)> kirby_##fn(address, #fn, fn, FunctionKind::Native, \
        false, kPpcAllNonvolatileFprMask, 0, 0, &AbiRawCpuThunk<&fn>::Invoke)

static_assert(KirbyBootConfig::WPADInit == 0x806B4360u);
PPC_NATIVE_OVERRIDE_VOID(806B4360, KirbyWPADInit, (), ());

static_assert(KirbyBootConfig::WPADControlDpd == 0x806B8030u);
PPC_NATIVE_OVERRIDE_VOID(806B8030, KirbyWPADControlDpd, (CpuContext* ctx), (ctx));
static_assert(KirbyBootConfig::WPADIsDpdEnabled == 0x806B7FA0u);
PPC_NATIVE_OVERRIDE(806B7FA0, KirbyWPADIsDpdEnabled, uint32_t, (uint32_t chan), (chan));
static_assert(KirbyBootConfig::WPADGetInfoAsync == 0x806B5A70u);
PPC_NATIVE_OVERRIDE_VOID(806B5A70, KirbyWPADGetInfoAsync, (CpuContext* ctx), (ctx));

static_assert(KirbyBootConfig::WPADGetStatus == 0x806B4410u);
PPC_NATIVE_OVERRIDE(806B4410, KirbyWPADGetStatus, int32_t, (), ());

static_assert(KirbyBootConfig::WPADProbe == 0x806B56F0u);
PPC_NATIVE_OVERRIDE(806B56F0, KirbyWPADProbe, int32_t, (uint32_t chan, uint32_t type), (chan, type));

static_assert(KirbyBootConfig::WPADRead == 0x806B6080u);
PPC_NATIVE_OVERRIDE_VOID(806B6080, KirbyWPADRead, (uint32_t chan, uint32_t output), (chan, output));

static_assert(KirbyBootConfig::WPADSetSamplingCallback == 0x806B5780u);
PPC_NATIVE_OVERRIDE(806B5780, KirbyWPADSetSamplingCallback, uint32_t, (uint32_t chan, uint32_t callback), (chan, callback));

static_assert(KirbyBootConfig::WPADSetConnectCallback == 0x806B5810u);
PPC_NATIVE_OVERRIDE(806B5810, KirbyWPADSetConnectCallback, uint32_t, (uint32_t chan, uint32_t callback), (chan, callback));

static_assert(KirbyBootConfig::WPADSetExtensionCallback == 0x806B58A0u);
PPC_NATIVE_OVERRIDE(806B58A0, KirbyWPADSetExtensionCallback, uint32_t, (uint32_t chan, uint32_t callback), (chan, callback));

static_assert(KirbyBootConfig::WPADGetDataFormat == 0x806B5900u);
PPC_NATIVE_OVERRIDE(806B5900, KirbyWPADGetDataFormat, uint32_t, (uint32_t chan), (chan));

static_assert(KirbyBootConfig::WPADSetDataFormat == 0x806B5950u);
PPC_NATIVE_OVERRIDE(806B5950, KirbyWPADSetDataFormat, int32_t, (uint32_t chan, uint32_t format), (chan, format));

static_assert(KirbyBootConfig::WPADSetAutoSamplingBuf == 0x806B6120u);
PPC_NATIVE_OVERRIDE_VOID(806B6120, KirbyWPADSetAutoSamplingBuf, (uint32_t chan, uint32_t buffer, uint32_t count), (chan, buffer, count));

static_assert(KirbyBootConfig::WPADGetAccGravityUnit == 0x806B5520u);
PPC_NATIVE_OVERRIDE_VOID(806B5520, KirbyWPADGetAccGravityUnit, (uint32_t chan, uint32_t type, uint32_t output), (chan, type, output));

static_assert(KirbyBootConfig::WPADControlMotor == 0x806B5C90u);
PPC_NATIVE_OVERRIDE_VOID(806B5C90, KirbyWPADControlMotor, (uint32_t chan, uint32_t command), (chan, command));

static_assert(KirbyBootConfig::WPADGetDpdSensitivity == 0x806B7F60u);
PPC_NATIVE_OVERRIDE(806B7F60, KirbyWPADGetDpdSensitivity, uint32_t, (), ());

static_assert(KirbyBootConfig::WPADiGetMplsStatus == 0x806B92E0u);
PPC_NATIVE_OVERRIDE(806B92E0, KirbyWPADiGetMplsStatus, uint32_t, (uint32_t chan), (chan));

static_assert(KirbyBootConfig::WPADDisconnect == 0x806B5630u);
PPC_NATIVE_OVERRIDE_VOID(806B5630, KirbyWPADDisconnect, (uint32_t chan), (chan));

namespace Kirby {
namespace {
constexpr std::pair<uint32_t, const char*> hooks[]{
    {KirbyBootConfig::WPADInit, "KirbyWPADInit"},
    {KirbyBootConfig::WPADControlDpd, "KirbyWPADControlDpd"},
    {KirbyBootConfig::WPADIsDpdEnabled, "KirbyWPADIsDpdEnabled"},
    {KirbyBootConfig::WPADGetInfoAsync, "KirbyWPADGetInfoAsync"},
    {KirbyBootConfig::WPADGetStatus, "KirbyWPADGetStatus"},
    {KirbyBootConfig::WPADProbe, "KirbyWPADProbe"},
    {KirbyBootConfig::WPADRead, "KirbyWPADRead"},
    {KirbyBootConfig::WPADSetSamplingCallback, "KirbyWPADSetSamplingCallback"},
    {KirbyBootConfig::WPADSetConnectCallback, "KirbyWPADSetConnectCallback"},
    {KirbyBootConfig::WPADSetExtensionCallback, "KirbyWPADSetExtensionCallback"},
    {KirbyBootConfig::WPADGetDataFormat, "KirbyWPADGetDataFormat"},
    {KirbyBootConfig::WPADSetDataFormat, "KirbyWPADSetDataFormat"},
    {KirbyBootConfig::WPADSetAutoSamplingBuf, "KirbyWPADSetAutoSamplingBuf"},
    {KirbyBootConfig::WPADGetAccGravityUnit, "KirbyWPADGetAccGravityUnit"},
    {KirbyBootConfig::WPADControlMotor, "KirbyWPADControlMotor"},
    {KirbyBootConfig::WPADGetDpdSensitivity, "KirbyWPADGetDpdSensitivity"},
    {KirbyBootConfig::WPADiGetMplsStatus, "KirbyWPADiGetMplsStatus"},
    {KirbyBootConfig::WPADDisconnect, "KirbyWPADDisconnect"},
};
}
bool AcceptWpadRegistration(uint32_t address, const char* name) noexcept {
    for (const auto& hook : hooks)
        if (address == hook.first && name && std::strcmp(name, hook.second) == 0) return true;
    return false;
}
void VerifyWpadRegistrations() {
    for (const auto& [address, name] : hooks) {
        const auto* hook = TranslatedFunctionRegistry::FindByAddressPtr(address);
        if (!hook || hook->kind != FunctionKind::Native || hook->name != name)
            throw std::runtime_error("Audited WPAD native registration is missing");
    }
}
void ProbeWpad(CpuContext& cpu) {
    auto check = [](bool value, const char* reason) { if (!value) throw std::runtime_error(reason); };
    auto call = [&](uint32_t address, uint32_t a=0, uint32_t b=0, uint32_t c=0) {
        cpu.gpr[3]=a; cpu.gpr[4]=b; cpu.gpr[5]=c;
        InvokeIndirectCpu(address, &cpu); return cpu.gpr[3];
    };
    namespace C = KirbyBootConfig;
    InvokeIndirectCpu(C::InitRegisters, &cpu);
    call(C::WPADInit);
    check(call(C::WPADGetStatus) == 3, "WPAD native backend did not initialize");
    constexpr uint32_t output = 0x81000000, ring = 0x81000100, used = 0x808E6558;
    PADStatus pad{};
    PADClearAllVirtualStatus();
    cpu.msr |= 0x8000;
    PollWpad(cpu, true);
    check(call(C::WPADProbe, 1, output) == uint32_t(-1) && Memory::Read32(output) == 253,
          "Absent native controller was reported connected");
    std::memset(Ram(output, 0x20), 0xCD, 0x20);
    Memory::Write32(used, 0);
    check(call(C::WPADGetInfoAsync, 1, output+4, C::WPADSetCallbackByKPAD) == uint32_t(-1)
          && Memory::Read32(used) == 1 && Memory::Read32(output+4) == 0xCDCDCDCD,
          "Disconnected info request did not fail synchronously");
    call(C::WPADSetSamplingCallback, 1, C::WPADSetCallbackByKPAD);
    call(C::WPADSetAutoSamplingBuf, 1, ring, 2);
    check(Memory::Read8(ring+0x29) == 255 && Memory::Read8(ring+0x2A+0x29) == 255,
          "Sampling buffer did not initialize disconnected reports");
    Memory::Write32(used, 0);
    cpu.msr &= ~0x8000u;
    pad.err = PAD_ERR_NONE; pad.button = PAD_BUTTON_A | PAD_BUTTON_X | PAD_BUTTON_LEFT;
    PADSetVirtualStatus(1, &pad);
    PollWpad(cpu, true);
    check(Memory::Read32(used) == 0, "WPAD callback ran while interrupts were masked");
    cpu.msr |= 0x8000;
    const auto saved = cpu;
    PollWpad(cpu, true);
    check(Memory::Read32(used) == 1 && std::memcmp(cpu.gpr, saved.gpr, sizeof(cpu.gpr)) == 0 && cpu.msr == saved.msr && cpu.lr == saved.lr &&
          std::memcmp(cpu.fpr, saved.fpr, sizeof(cpu.fpr)) == 0 && cpu.cr == saved.cr && cpu.xer == saved.xer,
          "Original sampling callback failed or changed the caller CPU");
    check(call(C::WPADProbe, 1, output) == 0 && Memory::Read32(output) == 0,
          "Native controller was not exposed as a core Wii Remote");
    check(call(C::WPADSetDataFormat, 1, 2) == 0, "Core accelerometer format failed");
    PollWpad(cpu, true);
    std::memset(Ram(output, 0x2E), 0xCD, 0x2E);
    call(C::WPADRead, 1, output+2);
    check(Memory::Read16(output+2) == 0x0508 && Memory::Read8(output+2+0x29) == 0,
          "Native buttons were not serialized in big endian WPAD format");
    check(Memory::Read16(output) == 0xCDCD && Memory::Read16(output+0x2C) == 0xCDCD,
          "WPADRead overwrote its 42-byte report");
    check(Memory::Read16(ring) == 0x0508 && Memory::Read16(ring+0x2A) == 0x0508,
          "Sampling ring did not wrap with native reports");
    pad.button = PAD_BUTTON_Y;
    PADSetVirtualStatus(1, &pad);
    PollWpad(cpu, true);
    check(Memory::Read16(ring) == 0x0800 && Memory::Read16(ring+0x2A) == 0x0508,
          "Sampling ring did not preserve order when overwriting the oldest report");
    call(C::WPADGetAccGravityUnit, 1, 0, output);
    check(Memory::Read16(output) == 256 && Memory::Read16(output+2) == 256 && Memory::Read16(output+4) == 256,
          "WPAD calibration does not match native accelerometer scale");
    for (auto [host,remote] : {std::pair{PAD_BUTTON_LEFT,0x0008},
            {PAD_BUTTON_RIGHT,0x0004}, {PAD_BUTTON_UP,0x0002}, {PAD_BUTTON_DOWN,0x0001},
            {PAD_BUTTON_A,0x0100}, {PAD_BUTTON_B,0x0200},
            {PAD_BUTTON_X,0x0400}, {PAD_BUTTON_Y,0x0800}}) {
        pad.button=host; PADSetVirtualStatus(1,&pad); PollWpad(cpu,true);
        call(C::WPADRead,1,output);
        check(Memory::Read16(output)==remote,"Horizontal controller mapping differs from the reviewed layout");
    }
    call(C::WPADSetSamplingCallback, 1, 0);
    Memory::Write32(used,0);
    check(call(C::WPADControlDpd,1,5,C::WPADSetCallbackByKPAD)==0 &&
          call(C::WPADIsDpdEnabled,1)==1 && Memory::Read32(used)==0,
          "DPD enable did not defer its original guest callback");
    PollWpad(cpu,true);
    check(Memory::Read32(used)==1,"DPD enable callback was not delivered");
    check(call(C::WPADControlDpd,1,0,0)==0 && call(C::WPADIsDpdEnabled,1)==0,
          "DPD disable did not update the native pointer state");
    PollWpad(cpu,true);

    std::memset(Ram(output, 0x20), 0xCD, 0x20);
    Memory::Write32(used, 0);
    check(call(C::WPADGetInfoAsync, 1, output+4, C::WPADSetCallbackByKPAD) == 0
          && Memory::Read32(used) == 0, "Connected info request completed synchronously");
    check(call(C::WPADGetInfoAsync, 1, output+4, 0) == uint32_t(-2),
          "Concurrent info request was accepted");
    cpu.msr &= ~0x8000u;
    PollWpad(cpu, true);
    check(Memory::Read32(output+4) == 0xCDCDCDCD && Memory::Read32(used) == 0,
          "Info request completed with interrupts masked");
    cpu.msr |= 0x8000u;
    PollWpad(cpu, true);
    check(Memory::Read32(used) == 1 && Memory::Read32(output+12) == 0
          && Memory::Read8(output+24) == 4 && Memory::Read8(output+25) == 2
          && Memory::Read32(output) == 0xCDCDCDCD && Memory::Read32(output+28) == 0xCDCDCDCD,
          "Info completion serialization or callback failed");
    bool infoRejected = false;
    try { call(C::WPADGetInfoAsync, 1, 0x817FFFF0, 0); }
    catch (const std::runtime_error&) { infoRejected = true; }
    check(infoRejected, "Info output outside guest RAM was accepted");
    call(C::WPADDisconnect, 1);
    PollWpad(cpu, true);
    check(call(C::WPADProbe, 1, output) == uint32_t(-1), "WPADDisconnect kept a controller connected");
    call(C::WPADRead, 1, output);
    check(Memory::Read16(output) == 0 && Memory::Read8(output+0x29) == 255,
          "Disconnect left stale buttons or success status");
    bool rejected = false;
    try { call(C::WPADRead, 1, 0x817FFFF0); } catch (const std::runtime_error&) { rejected = true; }
    check(rejected, "WPAD report outside guest RAM was accepted");
    rejected = false;
    try { call(C::WPADSetDataFormat, 1, 5); } catch (const std::runtime_error&) { rejected = true; }
    check(rejected, "Unsupported extension report was silently accepted");
    PADClearAllVirtualStatus();
    aurora_wpad_shutdown();
    initialized = false; events.clear(); channels = {}; infoRequests = {};
    std::cout << "[kirby] WPAD native bridge: reports, disconnect, sampling buffer and masked callbacks verified\n";
}
}
