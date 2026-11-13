// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/graphics/gx_renderer.h"
#include "kirby/app/events.h"
#include "kirby/graphics/xfb.h"
#include <aurora/aurora.h>
#include "video.hpp"
#include "gx/regs.hpp"
#include "gx/fifo.hpp"
#include "gx/texture.hpp"
#include "gfx/frame.hpp"
#include "gfx/render_worker.hpp"
#include "gfx/recording.hpp"
#include "gfx/readback/readback.hpp"
#include "gfx/readback/copy_layout.hpp"
#include <algorithm>
#include <cstdio>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>
#include <chrono>
#include <iostream>
#include <map>
#include <stdexcept>
#include <thread>

namespace Kirby {
struct GxRenderer::XfbState {
    struct Entry {
        XfbCopy copy;
        aurora::gfx::TextureHandle texture;
        std::vector<uint8_t> bytes;
        uint64_t generation;
    };
    aurora::gfx::readback::StreamHandle stream = 0;
    std::map<uint32_t, Entry> entries;
    struct TextureEntry { TextureCopy copy; std::vector<uint8_t> bytes; };
    std::map<uint32_t, TextureEntry> textures;
    uint64_t presented = 0;
    std::vector<uint8_t> Read(const aurora::gfx::TextureHandle& texture, uint32_t format,
                              uint32_t width, uint32_t height, uint32_t address, bool quantize) {
        using namespace aurora;
        namespace rb = gfx::readback;
        const auto fmt = *rb::format_info(format);
        const auto size = rb::byte_size(format, width, height);
        const auto sequence = rb::next_sequence(stream);
        if (!sequence) throw std::runtime_error("Aurora display-copy readback stream is invalid");
        auto job = rb::create_job(stream, sequence, rb::JobKind::Copy, {{address, 0, size}}, size);
        const rb::CopyPackParams params{
            .family = uint32_t(fmt.family), .encoding = uint32_t(fmt.encoding),
            .tileWidth = fmt.tileWidth, .tileHeight = fmt.tileHeight, .tileBytes = fmt.tileBytes,
            .tilesX = rb::tiles_x(fmt, width), .width = width, .height = height,
            .srcWidth = texture->size.width, .srcHeight = texture->size.height, .wordCount = size / 4,
            .quantizeRgba6 = quantize ? 1u : 0u,
        };
        job->copies.push_back({texture, gfx::push_uniform(params), size / 4});
        if (!gfx::push_readback_op(job, false)) throw std::runtime_error("Aurora could not record the XFB readback");
        gx::fifo::drain();
        gx::fifo::end_frame();
        gx::texture::end_frame();
        gfx::finish();
        // Submit the copy and readback without presenting. VI, not GXCopyDisp,
        // chooses which RAM buffer becomes visible.
        gfx::end_frame([](wgpu::CommandEncoder& encoder, std::vector<gfx::AfterSubmitCallback> callbacks,
                          const gfx::InterpolationReplay&) {
            const auto commands = encoder.Finish();
            gfx::queue().Submit(1, &commands);
            for (auto& callback : callbacks) callback();
        });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        std::shared_ptr<const rb::Packet> packet;
        do {
            // Dawn queue submissions and polling share the render worker. Polling
            // from the guest thread races queue state while a frame is submitted.
            gfx::render_worker::enqueue_work([] { webgpu::g_instance.ProcessEvents(); });
            aurora_wait_for_frame_worker();
            packet = rb::latest(stream);
            if (packet && packet->sequence == sequence) break;
            if (std::chrono::steady_clock::now() >= deadline)
                throw std::runtime_error("Timed out waiting for GPU display-copy readback");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (true);
        aurora_wait_for_frame_worker();
        const auto* output = packet->find(address);
        if (!output) throw std::runtime_error("GPU display-copy result has no requested destination");
        const auto payload=packet->payload_of(*output);
        return {payload.begin(),payload.end()};
    }

};

void GxRenderer::CopyDisplay(uint32_t value) {
    using namespace aurora;
    namespace rb = gfx::readback;
    if (!initialized_) throw std::runtime_error("GX display copy requires initialized Aurora; run --video");
    if (!write_) throw std::runtime_error("GX display copy has no guest-memory writer");
    auto& state = gx::g_gxState;
    const auto mask = state.bpRegCache[0xFE];
    const auto control = ((state.bpRegCache[0x52] & ~mask) | (value & mask)) & 0xFFFFFF;
    if (!(control&0x4000)) { CopyTexture(value,control); return; }
    const auto copy = XfbCopy::Decode(state.bpRegCache, control);
    if (resolve_(copy.address, copy.bytes).size() != copy.bytes)
        throw std::runtime_error("GX display-copy destination lies outside MEM1/MEM2");
    if ((control & 0x800) && (!state.colorUpdate || !state.depthUpdate ||
        (gx::efb_has_alpha(state.pixelFmt) && !state.alphaUpdate)))
        throw std::runtime_error("GX display-copy partial EFB clear needs attachment preservation");
    if (!rb::supported()) throw std::runtime_error("Aurora GPU copy readback is unavailable");
    if (!xfb_) {
        xfb_ = std::make_shared<XfbState>();
        xfb_->stream = rb::create_stream();
    }
    if (!xfb_->entries.contains(copy.address) && xfb_->entries.size() >= 16)
        throw std::runtime_error("GX display-copy destination cache limit reached");
    // Each RAM destination owns a snapshot; Aurora's default single texture would
    // otherwise overwrite the previous XFB before VI switches to the next one.
    state.displayCopyTexture.reset();
    gx::fifo::handle_bp(value);
    const auto texture = state.displayCopyTexture;
    if (!texture) throw std::runtime_error("Aurora did not create the display-copy texture");
    // Pack the RAM-visible YUYV image on the GPU. This halves readback bytes
    // and avoids a second full-image conversion on the guest execution thread.
    auto bytes=xfb_->Read(texture,rb::XfbYuyv,copy.width,copy.height,copy.address,false);
    write_(copy.address, bytes);
    const bool firstDestination = !xfb_->entries.contains(copy.address);
    xfb_->entries.insert_or_assign(copy.address, XfbState::Entry{copy, texture, std::move(bytes), ++displayCopies_});
    state.displayCopyProduced = false;
    if (!aurora_begin_frame_gx()) throw std::runtime_error("Aurora could not resume GX after display-copy readback");
    gfx::preserve_efb_contents();
    if (firstDestination) std::cout << "[kirby] XFB GPU readback: destination=0x" << std::hex << copy.address << std::dec
              << " size=" << copy.width << 'x' << copy.height << " bytes=" << copy.bytes
              << "; awaiting VI selection" << std::endl;
}

void GxRenderer::CopyTexture(uint32_t value, uint32_t control) {
    using namespace aurora;
    auto& state=gx::g_gxState;
    const auto copy=TextureCopy::Decode(state.bpRegCache,control,state.pixelFmt==GX_PF_Z24);
    const auto memory=resolve_(copy.address,copy.bytes);
    if (memory.size()!=copy.bytes) throw std::runtime_error("GX texture-copy destination lies outside MEM1/MEM2");
    if (!gfx::readback::supported()) throw std::runtime_error("GPU texture readback is unavailable");
    const auto extent=gfx::get_render_target_size();
    if (copy.x+copy.sourceWidth>extent.x || copy.y+copy.sourceHeight>extent.y)
        throw std::runtime_error("GX texture-copy source lies outside the EFB");
    if (!xfb_) { xfb_=std::make_shared<XfbState>(); xfb_->stream=gfx::readback::create_stream(); }
    state.texCopySrc={int(copy.x),int(copy.y),int(copy.sourceWidth),int(copy.sourceHeight)};
    state.texCopyDstWidth=copy.width; state.texCopyDstHeight=copy.height;
    state.texCopyFmt=static_cast<GXTexFmt>(copy.format); state.texCopyDest=memory.data();
    gx::fifo::handle_bp(value);
    const auto texture=state.copyTextures.at(memory.data()).handle;
    auto data=xfb_->Read(texture,copy.format,copy.width,copy.height,copy.address,
        gfx::readback::format_info(copy.format)->rawColor && state.pixelFmt==GX_PF_RGBA6_Z24);
    if (data.size()!=size_t(copy.rowBytes)*copy.rows) throw std::runtime_error("GX texture readback size differs from tile layout");
    for (uint32_t row=0;row<copy.rows;++row)
        write_(copy.address+row*copy.stride,std::span(data).subspan(row*copy.rowBytes,copy.rowBytes));
    // A packed, unscaled RGB8->RGBA8 copy has the same sampled representation on
    // the GPU and in RAM. Retain Aurora's allocation and avoid uploading it again.
    // Other layouts keep the existing RAM path until their sampling is audited.
    const bool retain = copy.format == GX_TF_RGBA8 && state.pixelFmt == GX_PF_RGB8_Z24 &&
        copy.stride == copy.rowBytes && texture->size.width == copy.width && texture->size.height == copy.height &&
        (xfb_->textures.contains(copy.address) || xfb_->textures.size() < 16);
    if (retain) xfb_->textures.insert_or_assign(copy.address,XfbState::TextureEntry{copy,std::move(data)});
    else {
        xfb_->textures.erase(copy.address);
        gx::evict_copy_texture(memory.data());
    }
    if (!aurora_begin_frame_gx()) throw std::runtime_error("Aurora could not resume GX after texture readback");
    gfx::preserve_efb_contents();
    if (++textureCopies_==1) {
        std::cout << "[kirby] first GPU texture copy: address=0x" << std::hex << copy.address
                  << " format=0x" << copy.format << std::dec << " size=" << copy.width << 'x' << copy.height
                  << " row bytes=" << copy.rowBytes << " stride=" << copy.stride << std::endl;
    }
}

void GxRenderer::ValidateCopyTexture(uint32_t address, uint32_t format, uint32_t width,
                                   uint32_t height, uint32_t mipCount) {
    if (!xfb_) return;
    const auto it = xfb_->textures.find(address);
    if (it == xfb_->textures.end()) return;
    const auto& entry = it->second;
    const auto memory = resolve_(address,entry.copy.bytes);
    if (format == entry.copy.format && width == entry.copy.width && height == entry.copy.height &&
        mipCount == 1 && std::ranges::equal(memory,entry.bytes)) return;
    // A guest write or a different interpretation of the RAM invalidates the
    // direct GPU view before Aurora resolves this draw's texture bindings.
    aurora::gx::evict_copy_texture(memory.data());
    xfb_->textures.erase(it);
}

void GxRenderer::PollVideo(const VideoOutput& output) {
    if (!initialized_ || !xfb_ || !output.enabled || !output.height || !output.address) return;
    const XfbState::Entry* selected = nullptr;
    for (const auto& [address, entry] : xfb_->entries) {
        const auto& c = entry.copy;
        const bool frame = output.address == address && output.stride == c.stride && output.height == c.height;
        const bool field = (output.address == address || output.address == address + c.stride) &&
                           output.stride == 2 * c.stride && 2 * output.height == c.height;
        if ((frame || field) && output.width == c.width && !output.xOffset) { selected = &entry; break; }
    }
    if (!selected) throw std::runtime_error("VI selected an XFB without a matching GPU copy/scanout layout");
    const auto memory = resolve_(selected->copy.address, selected->copy.bytes);
    if (!std::ranges::equal(memory, selected->bytes))
        throw std::runtime_error("VI selected a CPU-modified XFB; RAM upload is not implemented");
    if (xfb_->presented == selected->generation) return;
    aurora::gx::g_gxState.displayCopyTexture = selected->texture;
    aurora::gx::g_gxState.displayCopyProduced = true;
    aurora::video::set_black(false);
    aurora_end_frame();
    aurora_wait_for_frame_worker();
    const auto now = SDL_GetTicks();
    if (now >= nextTitleUpdate_) {
        char title[128];
        std::snprintf(title,sizeof(title),"Kirby's Epic Yarn - native development | %.1f FPS",aurora_get_fps());
        SDL_SetWindowTitle(aurora_get_window(),title);
        nextTitleUpdate_ = now + 250;
    }
    xfb_->presented = selected->generation;
    if (++presentations_ == 1)
        std::cout << "[kirby] first VI presentation: XFB=0x" << std::hex
                  << selected->copy.address << std::dec << '\n';
    PollHostEvents();
    if (!aurora_begin_frame()) throw std::runtime_error("Aurora could not begin the next video frame");
    aurora::gfx::preserve_efb_contents();
}

void GxRenderer::CloseXfb() {
    if (!xfb_) return;
    aurora_wait_for_frame_worker();
    aurora::gfx::readback::destroy_stream(xfb_->stream);
    xfb_.reset();
}
}
