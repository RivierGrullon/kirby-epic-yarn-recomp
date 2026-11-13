// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/graphics/gx_renderer.h"
#include "kirby/graphics/gx_fifo.h"
#include "kirby/graphics/xfb.h"
#include "gfx/readback/copy_layout.hpp"
#include "kirby/app/events.h"
#include "gx/gx.hpp"
#include "gx/fifo.hpp"
#include "gx/texture.hpp"
#include <dolphin/gx.h>
#include <bit>
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_events.h>

namespace {
unsigned checks;
void Check(bool value, const char* reason) { ++checks; if (!value) throw std::runtime_error(reason); }
template<class F> void Throws(F fn, const char* reason) {
    bool threw = false; try { fn(); } catch (const std::runtime_error&) { threw = true; }
    Check(threw, reason);
}
void Cp(Kirby::GxRenderer& renderer, uint8_t reg, uint32_t value) {
    const uint8_t packet[]{8,reg,uint8_t(value >> 24),uint8_t(value >> 16),uint8_t(value >> 8),uint8_t(value)};
    renderer.Feed(packet);
}
void Bp(Kirby::GxRenderer& renderer, uint32_t value) {
    const uint8_t packet[]{0x61,uint8_t(value >> 24),uint8_t(value >> 16),uint8_t(value >> 8),uint8_t(value)};
    renderer.Feed(packet);
}
void GpuXfb(bool startHidden = false) {
    // Readback-only format: guest GX texture format numbers are unchanged.
    const auto xfbFormat = aurora::gfx::readback::format_info(0x100);
    Check(xfbFormat.has_value() && xfbFormat->tileWidth == 2 && xfbFormat->tileHeight == 1 &&
          aurora::gfx::readback::byte_size(0x100, 640, 480) == 614400,
          "GPU XFB packing uses tightly packed YUYV pairs instead of RGBA8 tiles");
    std::vector<uint8_t> memory(0x1800000, 0xCD);
    unsigned writes = 0;
    auto resolve = [&](uint32_t address, uint32_t size) -> std::span<const uint8_t> {
        if (uint64_t(address) + size > memory.size()) return {};
        return std::span(memory).subspan(address, size);
    };
    Kirby::GxRenderer renderer(resolve, [&](uint32_t address, auto bytes) {
        Check(resolve(address, bytes.size()).size() == bytes.size(), "GPU write respects guest RAM bounds");
        std::memcpy(memory.data() + address, bytes.data(), bytes.size()); ++writes;
    });
    renderer.OpenWindow(startHidden);
    if (startHidden) {
        int count = 0;
        auto* windows = SDL_GetWindows(&count);
        Check(count == 1 && (SDL_GetWindowFlags(windows[0]) & SDL_WINDOW_HIDDEN),
              "Hidden GPU probe never maps a visible window");
        SDL_free(windows);
    }
    GXInit(nullptr, 0); GXFlush(); aurora::gx::fifo::drain();
    Bp(renderer, 0xFEFFFFFF); Bp(renderer, 0x40000010); Bp(renderer, 0x41000018);
    Bp(renderer, 0x43000000); Bp(renderer, 0x49000000);
    Bp(renderer, 0x4A000000 | (3 << 10) | 31); Bp(renderer, 0x4D000002); Bp(renderer, 0x4E000100);
    // Identity copy filter: center coefficient 64, no adjacent-row contribution.
    Bp(renderer, 0x53000000 | (32 << 12) | (32 << 18)); Bp(renderer, 0x54000000);
    Bp(renderer, 0x4F00FFFF); Bp(renderer, 0x50000000); // clear red after first copy
    Bp(renderer, 0x4B000080); Bp(renderer, 0x52004803);
    Bp(renderer, 0x4F00FF00); Bp(renderer, 0x5000FF00); // clear green after second copy
    Bp(renderer, 0x4B000100); Bp(renderer, 0x52004803);
    Bp(renderer, 0x4B000180); Bp(renderer, 0x52004803);
    const std::array<uint8_t,4> black{16,128,16,128}, red{82,90,82,239}, green{144,54,144,34};
    for (unsigned i = 0; i < 256; i += 4) {
        Check(std::equal(black.begin(), black.end(), memory.begin() + 0x1000 + i), "first GPU copy reads initial EFB before the red clear");
        Check(std::equal(red.begin(), red.end(), memory.begin() + 0x2000 + i), "second GPU copy reads previous red EFB clear");
        Check(std::equal(green.begin(), green.end(), memory.begin() + 0x3000 + i), "third GPU copy reads green EFB clear");
    }
    Check(memory[0x1FFF] == 0xCD && memory[0x2100] == 0xCD && memory[0x2FFF] == 0xCD && memory[0x3100] == 0xCD,
          "readback leaves bytes outside the XFB intact");
    Check(writes == 3 && renderer.DisplayCopies() == 3 && renderer.Presentations() == 0,
          "GX copies reach RAM without presenting before VI selection");
    renderer.PollVideo({0x2000,32,4,64,0,1,true});
    renderer.PollVideo({0x2000,32,4,64,0,2,true});
    Check(renderer.Presentations() == 1, "VI can select an older retained XFB without duplicating the submission");
    memory[0x2000] ^= 1;
    Throws([&] { renderer.PollVideo({0x2000,32,4,64,0,3,true}); }, "CPU modifications cannot silently present a stale GPU copy");
    // Exercise an actual primitive: clear-only copies cannot verify drawing.
    float projection[4][4]{{2.f/32,0,0,-1},{0,-2.f/4,0,1},{0,0,-1,0},{0,0,0,1}};
    float identity[3][4]{{1,0,0,0},{0,1,0,0},{0,0,1,0}};
    GXSetProjection(projection, GX_ORTHOGRAPHIC);
    GXLoadPosMtxImm(identity, GX_PNMTX0); GXSetCurrentMtx(GX_PNMTX0);
    GXSetViewport(0,0,32,4,0,1); GXSetScissor(0,0,32,4); GXSetScissorBoxOffset(0,0);
    GXClearVtxDesc(); GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetNumChans(1); GXSetNumTexGens(0); GXSetNumTevStages(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_REG, 0, GX_DF_NONE, GX_AF_NONE);
    GXSetChanMatColor(GX_COLOR0A0, {255,0,0,255});
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GXSetCullMode(GX_CULL_NONE); GXSetZMode(GX_FALSE, GX_ALWAYS, GX_TRUE);
    GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_COPY);
    GXSetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);
    GXSetColorUpdate(GX_TRUE); GXSetAlphaUpdate(GX_TRUE);
    GXFlush();
    aurora::gx::fifo::drain();
    std::vector<uint8_t> quad{0x80,0,4};
    for (float f : {0.f,0.f,0.f, 32.f,0.f,0.f, 32.f,4.f,0.f, 0.f,4.f,0.f}) {
        const auto word=std::bit_cast<uint32_t>(f);
        for (int shift : {24,16,8,0}) quad.push_back(word >> shift);
    }
    renderer.Feed(quad);
    Bp(renderer, 0x4B000080); Bp(renderer, 0x52004803);
    const auto sample = memory.data()+0x1000+2*64+16*2;
    std::cout << "[test] primitive sample YUYV: " << unsigned(sample[0]) << ',' << unsigned(sample[1])
              << ',' << unsigned(sample[2]) << ',' << unsigned(sample[3]) << '\n';
    Check(std::equal(red.begin(), red.end(), sample),
          "direct GX primitive produces red pixels in GPU readback");
    // CI4 texture and RGB565 palette come from guest RAM, not native GXTexObj pointers.
    std::fill_n(memory.begin()+0x400, 32, 0x11);
    std::fill_n(memory.begin()+0x500, 32, 0);
    memory[0x503] = 0x1F;
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GXSetNumChans(0); GXSetNumTexGens(1);
    GXSetTexCoordGen(GX_TEXCOORD0,GX_TG_MTX2x4,GX_TG_TEX0,GX_IDENTITY);
    GXSetTevOrder(GX_TEVSTAGE0,GX_TEXCOORD0,GX_TEXMAP0,GX_COLOR_NULL);
    GXSetTevOp(GX_TEVSTAGE0,GX_REPLACE);
    GXFlush(); aurora::gx::fifo::drain();
    Bp(renderer, 0x80000000); Bp(renderer, 0x84000000);
    Bp(renderer, 0x88000000 | (GX_TF_C4 << 20) | (7 << 10) | 7);
    Bp(renderer, 0x94000020); Bp(renderer, 0x98000400);
    Bp(renderer, 0x64000028); Bp(renderer, 0x65000400);
    quad = {0x80,0,4};
    for (float f : {0.f,0.f,0.f,0.f,0.f, 32.f,0.f,0.f,1.f,0.f,
                   32.f,4.f,0.f,1.f,1.f, 0.f,4.f,0.f,0.f,1.f}) {
        const auto word=std::bit_cast<uint32_t>(f);
        for (int shift : {24,16,8,0}) quad.push_back(word >> shift);
    }
    renderer.Feed(quad);
    Bp(renderer, 0x4B000080); Bp(renderer, 0x52004803);
    const std::array<uint8_t,4> blue{41,239,41,110};
    Check(std::equal(blue.begin(),blue.end(),memory.begin()+0x1000+2*64+16*2),
          "guest CI4 texture and uploaded RGB565 palette produce blue pixels");
    // Reproduce the game's XY-only UI vertices, texture matrix and two TEV stages.
    GXSetVtxAttrFmt(GX_VTXFMT0,GX_VA_POS,GX_POS_XY,GX_F32,0);
    GXLoadTexMtxImm(identity,GX_TEXMTX0,GX_MTX2x4);
    GXSetTexCoordGen(GX_TEXCOORD0,GX_TG_MTX2x4,GX_TG_TEX0,GX_TEXMTX0);
    GXSetNumChans(1); GXSetNumTevStages(2);
    GXSetChanMatColor(GX_COLOR0A0,{255,255,255,255});
    GXSetTevColor(GX_TEVREG0,{0,0,0,0}); GXSetTevColor(GX_TEVREG1,{255,255,255,255});
    GXSetTevColorIn(GX_TEVSTAGE0,GX_CC_C0,GX_CC_C1,GX_CC_TEXC,GX_CC_ZERO);
    GXSetTevAlphaIn(GX_TEVSTAGE0,GX_CA_A0,GX_CA_A1,GX_CA_TEXA,GX_CA_ZERO);
    GXSetTevColorOp(GX_TEVSTAGE0,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_FALSE,GX_TEVPREV);
    GXSetTevAlphaOp(GX_TEVSTAGE0,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_FALSE,GX_TEVPREV);
    GXSetTevOrder(GX_TEVSTAGE1,GX_TEXCOORD0,GX_TEXMAP_NULL,GX_COLOR0A0);
    GXSetTevColorIn(GX_TEVSTAGE1,GX_CC_ZERO,GX_CC_CPREV,GX_CC_RASC,GX_CC_ZERO);
    GXSetTevAlphaIn(GX_TEVSTAGE1,GX_CA_ZERO,GX_CA_APREV,GX_CA_RASA,GX_CA_ZERO);
    GXSetTevColorOp(GX_TEVSTAGE1,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
    GXSetTevAlphaOp(GX_TEVSTAGE1,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
    GXSetBlendMode(GX_BM_BLEND,GX_BL_SRCALPHA,GX_BL_INVSRCALPHA,GX_LO_COPY);
    GXFlush(); aurora::gx::fifo::drain();
    quad={0x80,0,4};
    for(float f : {0.f,0.f,0.f,0.f,32.f,0.f,1.f,0.f,32.f,4.f,1.f,1.f,0.f,4.f,0.f,1.f}) {
        const auto word=std::bit_cast<uint32_t>(f);
        for(int shift : {24,16,8,0}) quad.push_back(word>>shift);
    }
    renderer.Feed(quad);
    Bp(renderer,0x4B000080); Bp(renderer,0x52004803);
    Check(std::equal(blue.begin(),blue.end(),memory.begin()+0x1000+2*64+16*2),
          "XY UI vertices, texture matrix and two-stage TEV blending produce blue pixels");
    float uiProjection[4][4]{{0.107142858f,0,0,0},{0,0.190476194f,0,0},
                             {0,0,-0.000166666941f,-1.00000167f},{0,0,0,1}};
    float uiModel[3][4]{{0.0218749978f,0,0,0},{0,0.021874994f,0,0},{0,0,0.999999881f,-2999.99976f}};
    GXSetProjection(uiProjection,GX_ORTHOGRAPHIC); GXLoadPosMtxImm(uiModel,GX_PNMTX0);
    GXFlush(); aurora::gx::fifo::drain();
    std::fill_n(memory.begin()+0x400,32,0); memory[0x400]=0xF0;
    Bp(renderer,0x88000000); Bp(renderer,0x80000190);
    quad={0x80,0,4};
    for(float f : {-500.f,400.f,0.f,0.f,500.f,400.f,1.f,0.f,
                   500.f,-400.f,1.f,1.f,-500.f,-400.f,0.f,1.f}) {
        const auto word=std::bit_cast<uint32_t>(f);
        for(int shift : {24,16,8,0}) quad.push_back(word>>shift);
    }
    renderer.Feed(quad);
    renderer.PollVideo({0x3000,32,4,64,0,4,true});
    GXSetColorUpdate(GX_FALSE); GXSetAlphaUpdate(GX_FALSE);
    GXSetZMode(GX_FALSE,GX_ALWAYS,GX_FALSE);
    GXFlush(); aurora::gx::fifo::drain();
    Bp(renderer,0x4B000080); Bp(renderer,0x52004003);
    const std::array<uint8_t,4> white{235,128,235,128};
    Check(std::equal(white.begin(),white.end(),memory.begin()+0x1000+2*64+16*2),
          "UI model/projection and one-pixel I4 texture produce white pixels");
    GXSetColorUpdate(GX_TRUE); GXSetAlphaUpdate(GX_TRUE);
    GXSetZMode(GX_FALSE,GX_ALWAYS,GX_TRUE);
    GXFlush(); aurora::gx::fifo::drain();
    Bp(renderer,0x4B000080); Bp(renderer,0x52004803);
    Check(std::equal(white.begin(),white.end(),memory.begin()+0x1000+2*64+16*2),
          "copy without guest clear preserves EFB through the readback boundary");
    Bp(renderer,0x4B000080); Bp(renderer,0x52004803);
    Check(std::equal(green.begin(),green.end(),memory.begin()+0x1000+2*64+16*2),
          "guest-requested clear still replaces EFB after a preserved host boundary");
    // Mixed 16-bit position and 8-bit UV indices reference interleaved guest data.
    std::copy(quad.begin()+3,quad.end(),memory.begin()+0x600);
    GXSetVtxDesc(GX_VA_POS,GX_INDEX16); GXSetVtxDesc(GX_VA_TEX0,GX_INDEX8);
    GXFlush(); aurora::gx::fifo::drain();
    Cp(renderer,0xA0,0x600); Cp(renderer,0xB0,16);
    Cp(renderer,0xA4,0x608); Cp(renderer,0xB4,16);
    const uint8_t indexed[]{0x80,0,4, 0,0,0, 0,1,1, 0,2,2, 0,3,3};
    renderer.Feed(indexed);
    Check(aurora::gx::g_gxState.arrays[GX_VA_POS].size == 56 &&
          aurora::gx::g_gxState.arrays[GX_VA_TEX0].size == 56,
          "indexed uploads stop at the last referenced element");
    const auto positionRange = aurora::gx::g_gxState.arrays[GX_VA_POS].cachedRange;
    const auto uvRange = aurora::gx::g_gxState.arrays[GX_VA_TEX0].cachedRange;
    Cp(renderer,0xA0,0x600); Cp(renderer,0xA4,0x608);
    renderer.Feed(indexed);
    Check(aurora::gx::g_gxState.arrays[GX_VA_POS].cachedRange.offset == positionRange.offset &&
          aurora::gx::g_gxState.arrays[GX_VA_TEX0].cachedRange.offset == uvRange.offset,
          "unchanged indexed data reuses its existing GPU storage upload");
    Bp(renderer,0x4B000080); Bp(renderer,0x52004803);
    Check(std::equal(white.begin(),white.end(),memory.begin()+0x1000+2*64+16*2),
          "big-endian mixed index widths produce the textured UI quad");
    // Change positions without changing CP array bases; the next draw must reread RAM.
    const auto outside=std::bit_cast<uint32_t>(60000.f);
    for(unsigned vertex=0;vertex<4;++vertex)
        for(unsigned byte=0;byte<4;++byte) memory[0x600+vertex*16+byte]=outside >> (24-byte*8);
    renderer.Feed(indexed);
    Bp(renderer,0x4B000080); Bp(renderer,0x52004803);
    Check(std::equal(green.begin(),green.end(),memory.begin()+0x1000+2*64+16*2),
          "indexed draws observe guest array changes without CP rebinding");
    // EFB -> tiled RGBA8 RAM with padding between tile rows.
    Bp(renderer,0x49000000); Bp(renderer,0x4A000000 | (7<<10) | 7);
    Bp(renderer,0x4B000040); Bp(renderer,0x4D000006);
    Bp(renderer,0x52000063); // RGBA8 target, no EFB clear, clamp both edges
    for (unsigned tile=0;tile<2;++tile) for (unsigned pixel=0;pixel<16;++pixel) {
        const auto ar=0x800+tile*64+pixel*2;
        Check(memory[ar]==255 && memory[ar+1]==0 && memory[ar+32]==255 && memory[ar+33]==0,
              "texture copy writes green in native tiled AR/GB order");
    }
    Check(memory[0x880]==0xCD && memory[0x8BF]==0xCD && memory[0x940]==0xCD,
          "texture-copy row padding and trailing RAM stay untouched");
    Check(memory[0x8C0]!=0xCD && renderer.TextureCopies()==1 && renderer.DisplayCopies()==11,
          "second tile row uses guest stride and texture copies do not present");
    Check(!aurora::gx::g_gxState.copyTextures.contains(memory.data()+0x800),
          "texture copies leave RAM authoritative for future sampling and CPU writes");
    Bp(renderer,0x4A000000 | (3<<10) | 31); Bp(renderer,0x4D000002);
    // CPU edits to copied RAM must be visible when that texture is sampled later.
    memory[0x800]=255; memory[0x801]=255; memory[0x820]=0; memory[0x821]=0;
    GXSetVtxDesc(GX_VA_POS,GX_DIRECT); GXSetVtxDesc(GX_VA_TEX0,GX_DIRECT);
    GXFlush(); aurora::gx::fifo::drain();
    Bp(renderer,0x80000000); Bp(renderer,0x88000000 | (6<<20) | (7<<10) | 7);
    Bp(renderer,0x94000040);
    const auto uv=std::bit_cast<uint32_t>(0.0625f);
    for(unsigned vertex=0;vertex<4;++vertex) for(unsigned component=0;component<2;++component)
        for(unsigned byte=0;byte<4;++byte) quad[3+vertex*16+8+component*4+byte]=uv>>(24-byte*8);
    renderer.Feed(quad);
    Bp(renderer,0x4B000080); Bp(renderer,0x52004803);
    Check(std::equal(red.begin(),red.end(),memory.begin()+0x1000+2*64+16*2),
          "sampling an EFB copy observes subsequent CPU writes to texture RAM");
    // A packed 1x RGB8 EFB copy can stay on the GPU while RAM remains current.
    Bp(renderer,0x49000000); Bp(renderer,0x4A000000 | (7<<10) | 7);
    Bp(renderer,0x4B000060); Bp(renderer,0x4D000004); Bp(renderer,0x52000063);
    Check(aurora::gx::g_gxState.copyTextures.contains(memory.data()+0xC00),
          "packed RGBA8 copies retain Aurora's GPU texture instead of discarding it");
    const auto retained = aurora::gx::g_gxState.copyTextures.at(memory.data()+0xC00).handle;
    Bp(renderer,0x52000063);
    Check(aurora::gx::g_gxState.copyTextures.at(memory.data()+0xC00).handle == retained,
          "repeated packed copies reuse the render texture allocation");
    Bp(renderer,0x4A000000 | (3<<10) | 31); Bp(renderer,0x4D000002);
    Bp(renderer,0x94000060);
    const auto uploadedBeforeCopySample = aurora::gx::texture_stats().uploadBytes;
    renderer.Feed(quad);
    Check(aurora::gx::texture_stats().uploadBytes == uploadedBeforeCopySample,
          "sampling an unchanged copied texture avoids a RAM-to-GPU upload");
    Bp(renderer,0x4B000080); Bp(renderer,0x52004803);
    Check(std::equal(green.begin(),green.end(),memory.begin()+0x1000+2*64+16*2),
          "retained GPU copy samples the same green pixels as its RAM representation");
    memory[0xC00]=255; memory[0xC01]=255; memory[0xC20]=0; memory[0xC21]=0;
    renderer.Feed(quad);
    Check(!aurora::gx::g_gxState.copyTextures.contains(memory.data()+0xC00),
          "a CPU write invalidates the retained GPU copy before sampling");
    Bp(renderer,0x4B000080); Bp(renderer,0x52004803);
    Check(std::equal(red.begin(),red.end(),memory.begin()+0x1000+2*64+16*2),
          "CPU edits to a retained copy reach the displayed pixels without rebinding");
    // Reinterpreting the same bytes as I4 must not reuse the green RGBA8 view.
    Bp(renderer,0x4A000000 | (7<<10) | 7);
    Bp(renderer,0x4B000060); Bp(renderer,0x4D000004); Bp(renderer,0x52000063);
    Bp(renderer,0x88000000);
    renderer.Feed(quad);
    Check(!aurora::gx::g_gxState.copyTextures.contains(memory.data()+0xC00),
          "a changed texture descriptor invalidates the retained GPU view");
    Bp(renderer,0x4A000000 | (3<<10) | 31); Bp(renderer,0x4D000002);
    Bp(renderer,0x4B000080); Bp(renderer,0x52004803);
    Check(std::equal(white.begin(),white.end(),memory.begin()+0x1000+2*64+16*2),
          "an I4 reinterpretation samples copied RAM instead of the original RGBA8 colors");
    // Other EFB precision modes retain the RAM conversion path.
    Bp(renderer,0x4A000000 | (7<<10) | 7); Bp(renderer,0x4D000004);
    Bp(renderer,0x4B000060); Bp(renderer,0x43000002); Bp(renderer,0x52000063);
    Check(!aurora::gx::g_gxState.copyTextures.contains(memory.data()+0xC00),
          "RGB565 EFB copies do not bypass the RAM conversion path");
    Bp(renderer,0x43000000);
    Bp(renderer,0x49000000); Bp(renderer,0x4A000000 | (7<<10) | 7);
    Bp(renderer,0x4B000050); Bp(renderer,0x4D000002); Bp(renderer,0x52000263);
    Check(memory[0xA00]==255 && memory[0xA01]==0 && memory[0xA20]==255 && memory[0xA21]==0,
          "half-scale EFB copy preserves the expected source color");
    Bp(renderer,0x43000003); Bp(renderer,0x4A000000 | (3<<10) | 3);
    Bp(renderer,0x4B000058); Bp(renderer,0x52000063);
    Check(std::all_of(memory.begin()+0xB00,memory.begin()+0xB40,[](uint8_t v){return v==255;}),
          "far depth copies pack Z24X8 into tiled guest RAM");
    if (startHidden) {
        // More frames than the swapchain can retain: two submissions alone do
        // not expose a wait for a compositor that cannot display this window.
        const auto before = renderer.Presentations();
        Bp(renderer,0x43000000); Bp(renderer,0x49000000);
        Bp(renderer,0x4A000000 | (3<<10) | 31); Bp(renderer,0x4B000180);
        for (unsigned i = 0; i < 16; ++i) {
            SDL_Delay(30); // Replenish display admission instead of dropping fast test frames.
            Bp(renderer,0x52004003);
            renderer.PollVideo({0x3000,32,4,64,0,5+i,true});
            Check(renderer.Presentations() == before+i+1, "Hidden VI frames keep advancing");
            std::cout << "[test] hidden VI frame " << i+1 << std::endl;
        }
    }
    Bp(renderer,0x43000000); Bp(renderer,0x4A000000 | (3<<10) | 31);
    Bp(renderer,0x4D000002);
    Bp(renderer,0x4B000180); Bp(renderer,0x52004003);
    Check(std::string_view(SDL_GetWindowTitle(aurora_get_window())).find(" FPS") != std::string_view::npos,
          "native game window exposes the renderer presentation rate");
    // Compare varied GPU colors against the independently retained CPU encoder.
    // Odd heights and multiple rows expose row/tile-boundary mistakes; adjacent
    // unlike colors expose incorrect chroma averaging and rounding.
    GXSetProjection(projection,GX_ORTHOGRAPHIC); GXLoadPosMtxImm(identity,GX_PNMTX0);
    GXSetViewport(0,0,32,4,0,1); GXSetScissor(0,0,32,4);
    GXSetNumChans(0); GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0,GX_TEXCOORD0,GX_TEXMAP0,GX_COLOR_NULL);
    GXSetTevOp(GX_TEVSTAGE0,GX_REPLACE);
    GXSetBlendMode(GX_BM_NONE,GX_BL_ONE,GX_BL_ZERO,GX_LO_COPY);
    GXFlush(); aurora::gx::fifo::drain();
    uint32_t random = 0x12345678;
    for (unsigned tile=0;tile<8;++tile) for (unsigned pixel=0;pixel<16;++pixel) {
        const auto at=0x20000+tile*64+pixel*2;
        auto next = [&] { random=random*1664525u+1013904223u; return uint8_t(random>>24); };
        memory[at]=255; memory[at+1]=next(); memory[at+32]=next(); memory[at+33]=next();
    }
    Bp(renderer,0x80000000); Bp(renderer,0x84000000);
    Bp(renderer,0x88000000 | (6<<20) | (3<<10) | 31); Bp(renderer,0x94001000);
    quad={0x80,0,4};
    for (float f : {0.f,0.f,0.f,0.f,32.f,0.f,1.f,0.f,
                   32.f,4.f,1.f,1.f,0.f,4.f,0.f,1.f}) {
        const auto word=std::bit_cast<uint32_t>(f);
        for (int shift : {24,16,8,0}) quad.push_back(word>>shift);
    }
    renderer.Feed(quad);
    for (const unsigned width : {16u,32u}) for (const unsigned height : {1u,3u,4u,7u}) {
        Bp(renderer,0x4A000000 | ((height-1)<<10) | (width-1));
        Bp(renderer,0x4D000000 | width/2); // RGBA8 tile row: width*16 bytes
        Bp(renderer,0x4B001800); Bp(renderer,0x52000063);
        const auto rgba=std::span(memory).subspan(0x30000,width*16*((height+3)/4));
        const Kirby::XfbCopy copy{0x40000,width,height,width*2,width*height*2};
        const auto expected=Kirby::EncodeXfb(copy,rgba);
        std::fill_n(memory.begin()+copy.address,32*7*2+1,0xCD);
        Bp(renderer,0x4D000000 | width/16);
        Bp(renderer,0x4B002000); Bp(renderer,0x52004003);
        Check(std::equal(expected.begin(),expected.end(),memory.begin()+copy.address),
              "GPU YUYV bytes match the CPU oracle for varied adjacent colors and partial tile rows");
        Check(memory[copy.address+copy.bytes]==0xCD,
              "GPU YUYV readback does not write beyond the requested rows");
    }
    const auto writesBeforeInvalid = writes;
    Bp(renderer, 0x4B800000);
    Throws([&] { Bp(renderer, 0x52004803); }, "unmapped XFB stops before GPU submission or memory write");
    Check(writes == writesBeforeInvalid, "invalid destination does not write RAM");
    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    Check(SDL_PushEvent(&quit), "Quit event enters the real SDL queue");
    bool exited = false;
    try { renderer.PollVideo({0x3000,32,4,64,0,100,true}); }
    catch (const Kirby::ExitRequested&) { exited = true; }
    Check(exited, "Native video loop propagates an SDL quit request");
    std::cout << checks << " Vulkan XFB checks passed (synthetic colors, not a game frame)\n";
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--gpu-xfb") { GpuXfb(); return 0; }
        if (argc == 2 && std::string_view(argv[1]) == "--gpu-xfb-hidden") { GpuXfb(true); return 0; }
        if (argc != 1) throw std::runtime_error("Unknown renderer test option");
        using namespace aurora::gx;
        std::vector<uint8_t> memory(0x1800000);
        auto resolve = [&](uint32_t address, uint32_t size) -> std::span<const uint8_t> {
            if (uint64_t(address) + size > memory.size()) return {};
            return std::span(memory).subspan(address, size);
        };
        Kirby::GxRenderer renderer(resolve);
        Cp(renderer, 0x50, 1 << 9);
        Cp(renderer, 0x70, 1 | (GX_F32 << 1));
        Check(g_gxState.vtxDesc[GX_VA_POS] == GX_DIRECT && g_gxState.vtxFmts[0].attrs[GX_VA_POS].type == GX_F32,
              "real Aurora CP decoder receives vertex descriptors and formats");
        Cp(renderer, 0xA0, 0x80000100); Cp(renderer, 0xB0, 12);
        const auto& array = g_gxState.arrays[GX_VA_POS];
        Check(array.data == memory.data() + 0x100 && array.size == memory.size() - 0x100 && !array.le && array.stride == 12,
              "guest array address becomes a bounded big-endian host view");
        Bp(renderer, 0x41001234); Bp(renderer, 0xFE0000FF); Bp(renderer, 0x4100ABCD);
        Check(g_gxState.bpRegCache[0x41] == 0x410012CD && g_gxState.bpRegCache[0xFE] == 0xFFFFFF,
              "Aurora applies one-shot BP write mask");
        const uint8_t xf[]{0x10,0,0,0,0,0x3F,0x80,0,0};
        renderer.Feed(xf);
        Check(g_gxState.pnMtx[0].pos.m0.x() == 1.0f, "direct XF words use guest endianness");
        memory[0x204] = 0x40;
        Cp(renderer, 0xAC, 0x200); Cp(renderer, 0xBC, 4);
        const uint8_t indexed[]{0x20,0,1,0,0}; renderer.Feed(indexed);
        Check(g_gxState.pnMtx[0].pos.m0.x() == 2.0f, "indexed XF resolves guest matrix array");
        Kirby::Pixels() = Kirby::PixelEngine{};
        Kirby::Pixels().Write16(0xCC00100A, 3);
        Bp(renderer, 0x48001234); Bp(renderer, 0x45000002);
        uint16_t token = 0;
        Check(Kirby::Pixels().Read16(0xCC00100E, token) && token == 0x1234 &&
              Kirby::Pixels().TokenInterrupt() && Kirby::Pixels().FinishInterrupt(), "completed register stream signals PE events");
        const uint8_t invalidate[]{0x48}; renderer.Feed(invalidate);
        Check(renderer.Stream().PendingBytes() == 0 && renderer.RegisterWrites() == 13, "register packets consumed without leaving partial bytes");
        Bp(renderer, 0x64000008); Bp(renderer, 0x65000400);
        Check((g_gxState.bpRegCache[0x65] & 0xFFFFFF)==0x400,
              "palette upload commits a bounded 32-byte TMEM copy");
        Kirby::GxRenderer badPalette(resolve);
        Bp(badPalette, 0x64FFFFFF);
        Throws([&] { Bp(badPalette, 0x65000400); }, "palette source outside guest RAM is rejected");
        Kirby::GxRenderer overflowPalette(resolve);
        Bp(overflowPalette, 0x64000000);
        Throws([&] { Bp(overflowPalette, 0x650007FF | (32 << 10)); }, "palette upload beyond TMEM is rejected");
        Kirby::GxRenderer headlessDraw(resolve);
        const uint8_t triangle[39]{0x90,0,3};
        Throws([&] { headlessDraw.Feed(triangle); }, "draw requires initialized GPU before processing");
        Kirby::GxRenderer badArray(resolve);
        Throws([&] { Cp(badArray, 0xA0, 0x0C000000); }, "MMIO address cannot become a host array pointer");
        Kirby::GxRenderer scissor(resolve);
        Throws([&] { Bp(scissor, 0x20000000); }, "headless scissor stops before dereferencing uninitialized render target");
        Kirby::GxRenderer viewport(resolve);
        const uint8_t vp[]{0x10,0,0,0x10,0x1A,0,0,0,0};
        Throws([&] { viewport.Feed(vp); }, "headless viewport requires initialized renderer");
        Kirby::GxRenderer badXf(resolve);
        const uint8_t invalid[]{0x10,0,0,0xFF,0xFF,0,0,0,0};
        Throws([&] { badXf.Feed(invalid); }, "invalid XF destination is not silently discarded");
        Kirby::GxRenderer copy(resolve);
        Throws([&] { Bp(copy, 0x52004000); }, "headless EFB copy requires initialized graphics");
        std::cout << checks << " Aurora register integration checks passed\n";
    } catch (const std::exception& e) { std::cerr << "check " << checks << ": " << e.what() << '\n'; return 1; }
}
