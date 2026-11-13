// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/graphics/gx_renderer.h"
#include "kirby/graphics/gx_fifo.h"
#include "kirby/app/events.h"
#include <aurora/aurora.h>
#include "gx/command_processor.hpp"
#include "gx/regs.hpp"
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace Kirby {
namespace {
using namespace aurora::gx;
void Log(AuroraLogLevel, const char* module, const char* message, unsigned length) {
    std::cerr << '[' << module << "] " << std::string_view(message, length) << '\n';
}
uint32_t Physical(uint32_t address) { return address & 0x1FFFFFFF; }
uint32_t Available(uint32_t address) {
    if (address < 0x01800000) return 0x01800000 - address;
    if (address >= 0x10000000 && address < 0x14000000) return 0x14000000 - address;
    return 0;
}
}
GxRenderer::GxRenderer(GxStream::Resolve resolve, WriteMemory write) : resolve_(std::move(resolve)), write_(std::move(write)),
    stream_([this](auto bytes) { Packet(bytes); }, [this](auto format) { return VertexSize(format); },
        [this](uint32_t address,uint32_t size) {
            FlushTextureCopiesForRange(address,size);
            return resolve_(address,size);
        }) {}
GxRenderer::~GxRenderer() { if (initialized_) { CloseXfb(); aurora_shutdown(); } }
void GxRenderer::OpenWindow(bool startHidden, bool vsync) {
    if (initialized_) throw std::runtime_error("Aurora window is already initialized");
    std::filesystem::create_directories("local/user/aurora/cache");
    userPath_ = std::filesystem::absolute("local/user/aurora").string();
    cachePath_ = std::filesystem::absolute("local/user/aurora/cache").string();
    AuroraConfig config{};
    config.appName = "Kirby's Epic Yarn - native development";
    config.userPath = userPath_.c_str(); config.cachePath = cachePath_.c_str();
    config.resourcesPath = userPath_.c_str();
    config.windowWidth = 640; config.windowHeight = 480;
    config.desiredBackend = BACKEND_VULKAN;
    config.logCallback = Log; config.logLevel = LOG_INFO;
    config.vsync = vsync;
    config.startHidden = startHidden;
    const auto info = aurora_initialize(0, nullptr, &config);
    if (info.initializationStatus != AURORA_INITIALIZATION_SUCCESS)
        throw std::runtime_error(info.initializationError ? info.initializationError : "Aurora initialization failed");
    initialized_ = true;
    // Bring-up must not silently skip EFB clear/draw operations while their
    // pipelines compile. GX readback observes the resulting pixels immediately.
    aurora::gx::g_gxState.drawsBlocking = true;
    PollHostEvents();
    if (!aurora_begin_frame()) throw std::runtime_error("Aurora could not begin the initial graphics frame");
    std::cout << "[kirby] Aurora window initialized; backend=" << info.backend
              << "; no game frame has been submitted\n";
}
void GxRenderer::ArrayBase(uint8_t reg, uint32_t value) {
    auto& state = aurora::gx::g_gxState;
    const auto address = Physical(value), size = Available(address);
    const auto memory = size ? resolve_(address, size) : std::span<const uint8_t>{};
    if (memory.size() != size || !size) throw std::runtime_error("GX array base lies outside MEM1/MEM2");
    auto& array = state.arrays[GX_VA_POS + reg - 0xA0];
    if (array.data != memory.data() || array.le) array.cachedRange = {};
    array.data = memory.data(); array.size = memory.size(); array.le = false;
    state.cpRegCache[reg] = value; state.cpRegValid.set(reg);
    state.dirty |= aurora::gx::DirtyImmediates | aurora::gx::DirtyPipeline;
}
uint32_t GxRenderer::VertexSize(uint8_t format) const {
    using namespace aurora::gx;
    uint32_t size = 0;
    for (unsigned attr = GX_VA_PNMTXIDX; attr <= GX_VA_TEX7; ++attr) {
        const auto& fmt = g_gxState.vtxFmts[format].attrs[attr];
        switch (g_gxState.vtxDesc[attr]) {
        case GX_NONE: break;
        case GX_INDEX8: size += attr == GX_VA_NRM && fmt.cnt == GX_NRM_NBT3 ? 3 : 1; break;
        case GX_INDEX16: size += attr == GX_VA_NRM && fmt.cnt == GX_NRM_NBT3 ? 6 : 2; break;
        case GX_DIRECT:
            // Validate enums before calling Aurora's fatal-on-invalid helpers.
            if (attr >= GX_VA_POS && (fmt.type > (attr == GX_VA_CLR0 || attr == GX_VA_CLR1 ? GX_RGBA8 : GX_F32) ||
                fmt.cnt > (attr == GX_VA_NRM ? GX_NRM_NBT3 : 1)))
                throw std::runtime_error("GX direct vertex format is invalid");
            size += comp_type_size(static_cast<GXAttr>(attr), fmt.type) * comp_cnt_count(static_cast<GXAttr>(attr), fmt.cnt);
            break;
        default: throw std::runtime_error("GX vertex descriptor is invalid");
        }
    }
    return size;
}
void GxRenderer::Packet(std::span<const uint8_t> bytes) {
    using namespace aurora::gx;
    const auto opcode = bytes[0];
    if (opcode == 0) return;
    if (opcode == 0x08) {
        const auto reg = bytes[1]; const auto value = GxBig32(bytes.data() + 2);
        if (reg >= 0xA0 && reg <= 0xAF) ArrayBase(reg, value);
        else fifo::handle_cp(reg, value);
        ++registerWrites_; return;
    }
    if (opcode == 0x10) {
        const auto header = GxBig32(bytes.data() + 1);
        const auto address = header & 0xFFFF, count = (header >> 16) + 1;
        if (address < 0x1000) {
            if (!fifo::copy_xf_data(address, bytes.data() + 5, count, std::endian::big))
                throw std::runtime_error("GX direct XF memory destination is unsupported by Aurora");
        } else {
            if (address + count > 0x1058) throw std::runtime_error("GX XF register range is unsupported by Aurora");
            if (!initialized_ && address < 0x1027 && address + count > 0x101A)
                throw std::runtime_error("GX viewport/projection requires initialized Aurora; run --video");
            fifo::handle_xf(address, bytes.subspan(5));
        }
        ++registerWrites_; return;
    }
    if (opcode >= 0x20 && opcode <= 0x38) {
        const auto index = (uint32_t(bytes[1]) << 8) | bytes[2];
        const auto field = (uint32_t(bytes[3]) << 8) | bytes[4];
        const auto words = (field >> 12) + 1;
        const auto& array = g_gxState.arrays[GX_POS_MTX_ARRAY + (opcode - 0x20) / 8];
        const auto offset = index * array.stride;
        if (!array.data || offset > array.size || words * 4 > array.size - offset)
            throw std::runtime_error("GX indexed XF load lies outside guest array");
        const auto base=g_gxState.cpRegCache[0xA0+GX_POS_MTX_ARRAY-GX_VA_POS+(opcode-0x20)/8] & 0x1FFFFFFF;
        FlushTextureCopiesForRange(base+offset,words*4);
        if (!fifo::copy_xf_data(field & 0xFFF, static_cast<const uint8_t*>(array.data) + offset, words, std::endian::big))
            throw std::runtime_error("GX indexed XF destination is unsupported by Aurora");
        ++registerWrites_; return;
    }
    if (opcode == 0x61) {
        const auto value = GxBig32(bytes.data() + 1), reg = value >> 24;
        if (!initialized_ && (reg == 0x20 || reg == 0x21 || reg == 0x59))
            throw std::runtime_error("GX scissor requires initialized Aurora; run --video");
        if (reg == 0x52) {
            CopyDisplay(value);
            ++registerWrites_; return;
        }
        if (reg == 0x65) { LoadPalette(value); ++registerWrites_; return; }
        fifo::handle_bp(value);
        const auto effective = g_gxState.bpRegCache[reg];
        // Resident copies remain GPU-authoritative across logical completion;
        // CPU invalidation and RAM-consuming GX commands materialize them.
        if (!gpuResidentTextureCopies_ && (reg == 0x47 || reg == 0x48 || (reg == 0x45 && (effective & 0xFF) == 2)))
            FlushTextureCopies();
        if (reg == 0x47 || reg == 0x48) Pixels().SetToken(effective & 0xFFFF, reg == 0x48);
        if (reg == 0x45 && (effective & 0xFF) == 2) Pixels().Finish();
        ++registerWrites_; return;
    }
    if (opcode >= 0x80) { Draw(bytes); return; }
    const auto result = fifo::process(bytes.data(), bytes.size());
    if (result.bytesProcessed != bytes.size()) throw std::runtime_error("Aurora did not consume the complete GX packet");
}
}
