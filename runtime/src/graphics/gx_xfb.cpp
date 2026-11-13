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
        bool pending = false;
    };
    aurora::gfx::readback::StreamHandle stream = 0;
    std::map<uint32_t, Entry> entries;
    struct TextureEntry { TextureCopy copy; std::vector<uint8_t> bytes; uint64_t validatedEpoch = 0; };
    std::map<uint32_t, TextureEntry> textures;
    struct PendingTexture {
        TextureCopy copy;
        aurora::gfx::TextureHandle texture;
        std::vector<uint8_t> before, bytes;
    };
    std::vector<PendingTexture> pending;
    uint64_t presented = 0;
    std::vector<uint8_t> Read(const aurora::gfx::TextureHandle& texture, uint32_t format,
                              uint32_t width, uint32_t height, bool quantize, bool includePending=true) {
        using namespace aurora;
        namespace rb = gfx::readback;
        const auto sequence = rb::next_sequence(stream);
        if (!sequence) throw std::runtime_error("Aurora display-copy readback stream is invalid");
        struct Source { gfx::TextureHandle texture; uint32_t format, width, height; bool quantize; };
        std::vector<Source> sources;
        if (includePending) for (const auto& entry : pending)
            sources.push_back({entry.texture,entry.copy.format,entry.copy.width,entry.copy.height,false});
        sources.push_back({texture,format,width,height,quantize});
        std::vector<rb::Output> outputs;
        uint32_t total = 0;
        for (const auto& source : sources) {
            const auto size = rb::byte_size(source.format,source.width,source.height);
            if (size > 32*1024*1024-total) throw std::runtime_error("GX readback batch exceeds 32 MiB");
            // Keys identify commands, not destinations: RAM ranges can alias.
            outputs.push_back({outputs.size()+1,total,size}); total += size;
        }
        auto job = rb::create_job(stream,sequence,rb::JobKind::Copy,outputs,total);
        for (size_t i=0;i<sources.size();++i) {
            const auto& source = sources[i];
            const auto fmt = *rb::format_info(source.format);
            const rb::CopyPackParams params{
                .family = uint32_t(fmt.family), .encoding = uint32_t(fmt.encoding),
                .tileWidth = fmt.tileWidth, .tileHeight = fmt.tileHeight, .tileBytes = fmt.tileBytes,
                .tilesX = rb::tiles_x(fmt,source.width), .width = source.width, .height = source.height,
                .srcWidth = source.texture->size.width, .srcHeight = source.texture->size.height,
                .wordCount = outputs[i].payloadSize/4, .outWord = outputs[i].payloadOffset/4,
                .quantizeRgba6 = source.quantize ? 1u : 0u,
            };
            job->copies.push_back({source.texture,gfx::push_uniform(params),params.wordCount});
        }
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
        for (size_t i=0;includePending && i<pending.size();++i) {
            const auto* output = packet->find(i+1);
            if (!output) throw std::runtime_error("GPU texture-copy batch result is missing");
            const auto payload = packet->payload_of(*output);
            pending[i].bytes.assign(payload.begin(),payload.end());
        }
        const auto* output = packet->find(sources.size());
        if (!output) throw std::runtime_error("GPU display-copy result has no requested destination");
        const auto payload=packet->payload_of(*output);
        return {payload.begin(),payload.end()};
    }

};

void GxRenderer::SetDeferredTextureCopies(bool enabled) {
    if (!enabled || gpuResidentTextureCopies_) FlushTextureCopies();
    gpuResidentTextureCopies_=false;
    deferredTextureCopies_ = enabled;
}
void GxRenderer::SetGpuResidentTextureCopies(bool enabled) {
    FlushTextureCopies();
    deferredTextureCopies_=gpuResidentTextureCopies_=enabled;
}
void GxRenderer::SetGpuResidentDisplayCopies(bool enabled) {
    if (!enabled) FlushDisplayCopiesForRange(0,0x20000000ull);
    gpuResidentDisplayCopies_=enabled;
}
void GxRenderer::FlushDisplayCopiesForRange(uint32_t address,uint64_t size) {
    if (!size || !xfb_) return;
    const uint32_t physical=address & 0x1FFFFFFF;
    for (auto& [destination,entry] : xfb_->entries) {
        const auto& copy=entry.copy;
        if (!entry.pending || (size<=0x20000000ull-physical &&
            (physical>=uint64_t(destination)+copy.bytes || destination>=uint64_t(physical)+size))) continue;
        if (!std::ranges::equal(resolve_(destination,copy.bytes),entry.bytes))
            throw std::runtime_error("CPU modified a resident XFB before its completion fence");
        ++readbacks_;
        entry.bytes=xfb_->Read(entry.texture,aurora::gfx::readback::XfbYuyv,copy.width,copy.height,false,false);
        write_(destination,entry.bytes);
        entry.pending=false;
        if (!aurora_begin_frame_gx()) throw std::runtime_error("Aurora could not resume GX after XFB fence");
        aurora::gfx::preserve_efb_contents();
    }
}
void GxRenderer::WritebackTextureCopiesForRange(uint32_t address,uint64_t size) {
    FlushDisplayCopiesForRange(address,size);
    if (!gpuResidentTextureCopies_) { FlushTextureCopiesForRange(address,size); return; }
    if (!size || !xfb_) return;
    const uint32_t physical=address & 0x1FFFFFFF;
    if (size>0x20000000ull-physical)
        throw std::runtime_error("GPU-resident cache writeback crosses a guest alias boundary");
    struct Written { uint32_t address; std::vector<uint8_t> bytes; };
    std::vector<Written> written;
    for (auto it=xfb_->pending.begin();it!=xfb_->pending.end();) {
        const auto& copy=it->copy;
        const auto start=std::max(physical,copy.address);
        const auto end=std::min(uint64_t(physical)+size,uint64_t(copy.address)+copy.bytes);
        if (start>=end) { ++it; continue; }
        if (start==copy.address && end==uint64_t(copy.address)+copy.bytes) {
            // CPU writeback supplies this entire destination; no GPU bytes survive.
            it=xfb_->pending.erase(it);
        } else {
            const auto bytes=resolve_(start,end-start);
            written.push_back({start,{bytes.begin(),bytes.end()}});
            // Only the explicitly published cache lines may have changed.
            std::copy(bytes.begin(),bytes.end(),it->before.begin()+start-copy.address);
            ++it;
        }
    }
    if (!written.empty()) {
        FlushTextureCopies();
        for (const auto& entry : written) write_(entry.address,entry.bytes);
    }
    for (auto it=xfb_->textures.begin();it!=xfb_->textures.end();) {
        const auto& copy=it->second.copy;
        if (physical<uint64_t(copy.address)+copy.bytes && copy.address<uint64_t(physical)+size) {
            aurora::gx::evict_copy_texture(resolve_(copy.address,copy.bytes).data());
            it=xfb_->textures.erase(it);
        } else ++it;
    }
}
void GxRenderer::FlushTextureCopiesForRange(uint32_t address, uint64_t size) {
    FlushDisplayCopiesForRange(address,size);
    FlushPendingTextureCopiesForRange(address,size);
}
void GxRenderer::FlushPendingTextureCopiesForRange(uint32_t address, uint64_t size) {
    if (!size || !xfb_ || xfb_->pending.empty()) return;
    const uint32_t physical=address & 0x1FFFFFFF;
    // A cache loop can wrap the PPC address space. Complete everything for
    // spans crossing an alias boundary rather than missing a wrapped overlap.
    if (size>0x20000000ull-physical || std::ranges::any_of(xfb_->pending,[&](const auto& entry) {
        return physical<uint64_t(entry.copy.address)+entry.copy.bytes &&
               entry.copy.address<uint64_t(physical)+size;
    })) FlushTextureCopies();
}
void GxRenderer::CommitTextureCopies() {
    for (const auto& entry : xfb_->pending) {
        const auto& copy = entry.copy;
        if (!std::ranges::equal(resolve_(copy.address,copy.bytes),entry.before))
            throw std::runtime_error("CPU wrote a deferred GX copy before its completion fence");
        if (entry.bytes.size()!=size_t(copy.rowBytes)*copy.rows)
            throw std::runtime_error("GX texture batch result differs from tile layout");
        for (uint32_t row=0;row<copy.rows;++row)
            write_(copy.address+row*copy.stride,std::span(entry.bytes).subspan(row*copy.rowBytes,copy.rowBytes));
        xfb_->textures.insert_or_assign(copy.address,XfbState::TextureEntry{copy,entry.bytes});
    }
    xfb_->pending.clear();
}
void GxRenderer::FlushTextureCopies() {
    if (!xfb_ || xfb_->pending.empty()) return;
    auto last = std::move(xfb_->pending.back());
    xfb_->pending.pop_back();
    ++readbacks_;
    last.bytes = xfb_->Read(last.texture,last.copy.format,last.copy.width,last.copy.height,false);
    xfb_->pending.push_back(std::move(last));
    CommitTextureCopies();
    if (!aurora_begin_frame_gx()) throw std::runtime_error("Aurora could not resume GX after copy fence");
    aurora::gfx::preserve_efb_contents();
}

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
    // Replacing the exact same layout makes its previous GPU image obsolete.
    // Other overlapping views need RAM coherence before either view is reused.
    bool resident=gpuResidentDisplayCopies_;
    for (const auto& [address,entry] : xfb_->entries) {
        const auto& old=entry.copy;
        if (copy.address<uint64_t(address)+old.bytes && address<uint64_t(copy.address)+copy.bytes &&
            (address!=copy.address || old.width!=copy.width || old.height!=copy.height || old.stride!=copy.stride))
            resident=false;
    }
    if (resident) {
        if (auto it=xfb_->entries.find(copy.address);it!=xfb_->entries.end()) it->second.pending=false;
    }
    FlushDisplayCopiesForRange(copy.address,copy.bytes);
    // Complete an aliased texture destination before replacing its RAM with YUYV.
    if (gpuResidentTextureCopies_) FlushPendingTextureCopiesForRange(copy.address,copy.bytes);
    else if (resident) FlushTextureCopies();
    // Each RAM destination owns a snapshot; Aurora's default single texture would
    // otherwise overwrite the previous XFB before VI switches to the next one.
    state.displayCopyTexture.reset();
    gx::fifo::handle_bp(value);
    const auto texture = state.displayCopyTexture;
    if (!texture) throw std::runtime_error("Aurora did not create the display-copy texture");
    // Pack the RAM-visible YUYV image on the GPU. This halves readback bytes
    // and avoids a second full-image conversion on the guest execution thread.
    std::vector<uint8_t> bytes;
    if (resident) {
        const auto memory=resolve_(copy.address,copy.bytes);
        bytes.assign(memory.begin(),memory.end());
    } else {
        ++readbacks_;
        bytes=xfb_->Read(texture,rb::XfbYuyv,copy.width,copy.height,false,!gpuResidentTextureCopies_);
        if (!gpuResidentTextureCopies_) CommitTextureCopies();
        write_(copy.address, bytes);
    }
    const bool firstDestination = !xfb_->entries.contains(copy.address);
    xfb_->entries.insert_or_assign(copy.address, XfbState::Entry{copy, texture, std::move(bytes), ++displayCopies_,resident});
    state.displayCopyProduced = false;
    if (!resident) {
        if (!aurora_begin_frame_gx()) throw std::runtime_error("Aurora could not resume GX after display-copy readback");
        gfx::preserve_efb_contents();
    }
    if (firstDestination) std::cout << "[kirby] XFB " << (resident ? "GPU resident" : "GPU readback")
              << ": destination=0x" << std::hex << copy.address << std::dec
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
    FlushDisplayCopiesForRange(copy.address,copy.bytes);
    // Complete packed overwrites make the previous resident RAM value obsolete.
    // Partial/strided overwrites must materialize bytes outside the new copy.
    if (gpuResidentTextureCopies_ && copy.stride==copy.rowBytes)
        std::erase_if(xfb_->pending,[&](const auto& entry) {
            return copy.address<=entry.copy.address &&
                uint64_t(copy.address)+copy.bytes>=uint64_t(entry.copy.address)+entry.copy.bytes;
        });
    if (xfb_->pending.size() >= 16 || std::ranges::any_of(xfb_->pending,[&](const auto& entry) {
        return uint64_t(copy.address)<uint64_t(entry.copy.address)+entry.copy.bytes &&
               uint64_t(entry.copy.address)<uint64_t(copy.address)+copy.bytes;
    })) FlushTextureCopies();
    state.texCopySrc={int(copy.x),int(copy.y),int(copy.sourceWidth),int(copy.sourceHeight)};
    state.texCopyDstWidth=copy.width; state.texCopyDstHeight=copy.height;
    state.texCopyFmt=static_cast<GXTexFmt>(copy.format); state.texCopyDest=memory.data();
    gx::fifo::handle_bp(value);
    const auto texture=state.copyTextures.at(memory.data()).handle;
    // Direct GPU sampling is verified only for packed RGB8->RGBA8 copies.
    const bool retain = copy.format == GX_TF_RGBA8 && state.pixelFmt == GX_PF_RGB8_Z24 &&
        copy.stride == copy.rowBytes && texture->size.width == copy.width && texture->size.height == copy.height &&
        (xfb_->textures.contains(copy.address) || xfb_->textures.size() < 16);
    if (deferredTextureCopies_ && retain) {
        std::vector<uint8_t> before(memory.begin(),memory.end());
        xfb_->textures.insert_or_assign(copy.address,XfbState::TextureEntry{copy,before});
        xfb_->pending.push_back({copy,texture,std::move(before),{}});
    } else {
        ++readbacks_;
        auto data=xfb_->Read(texture,copy.format,copy.width,copy.height,
            gfx::readback::format_info(copy.format)->rawColor && state.pixelFmt==GX_PF_RGBA6_Z24);
        CommitTextureCopies();
        if (data.size()!=size_t(copy.rowBytes)*copy.rows) throw std::runtime_error("GX texture readback size differs from tile layout");
        for (uint32_t row=0;row<copy.rows;++row)
            write_(copy.address+row*copy.stride,std::span(data).subspan(row*copy.rowBytes,copy.rowBytes));
        if (retain) xfb_->textures.insert_or_assign(copy.address,XfbState::TextureEntry{copy,std::move(data)});
        else {
            xfb_->textures.erase(copy.address);
            gx::evict_copy_texture(memory.data());
        }
        if (!aurora_begin_frame_gx()) throw std::runtime_error("Aurora could not resume GX after texture readback");
        gfx::preserve_efb_contents();
    }
    if (++textureCopies_==1) {
        std::cout << "[kirby] first GPU texture copy: address=0x" << std::hex << copy.address
                  << " format=0x" << copy.format << std::dec << " size=" << copy.width << 'x' << copy.height
                  << " row bytes=" << copy.rowBytes << " stride=" << copy.stride << std::endl;
    }
}

void GxRenderer::ValidateCopyTexture(uint32_t address, uint32_t format, uint32_t width,
                                   uint32_t height, uint32_t mipCount, uint32_t bytes) {
    if (!xfb_) return;
    FlushDisplayCopiesForRange(address,bytes);
    // A subview, format reinterpretation, or older overlapping texture cannot
    // sample a pending copy through its exact GPU cache entry. Complete RAM
    // first, including when the requested address has no cache entry at all.
    if (std::ranges::any_of(xfb_->pending,[&](const auto& entry) {
        const auto& copy = entry.copy;
        const bool overlap = uint64_t(address)<uint64_t(copy.address)+copy.bytes &&
                             uint64_t(copy.address)<uint64_t(address)+bytes;
        return overlap && (address!=copy.address || format!=copy.format ||
            width!=copy.width || height!=copy.height || mipCount!=1);
    })) FlushTextureCopies();
    const auto it = xfb_->textures.find(address);
    if (it == xfb_->textures.end()) return;
    auto& entry = it->second;
    const auto memory = resolve_(address,entry.copy.bytes);
    if (format == entry.copy.format && width == entry.copy.width && height == entry.copy.height &&
        mipCount == 1 && ((textureSnapshotReuse_ && textureBatchValidation_ &&
                          entry.validatedEpoch == textureValidationEpoch_) || std::ranges::equal(memory,entry.bytes))) {
        entry.validatedEpoch=textureValidationEpoch_;
        return;
    }
    // A guest write or a different interpretation of the RAM invalidates the
    // direct GPU view before Aurora resolves this draw's texture bindings.
    FlushTextureCopies();
    aurora::gx::evict_copy_texture(memory.data());
    xfb_->textures.erase(address);
}

void GxRenderer::PollVideo(const VideoOutput& output) {
    if (!initialized_ || !xfb_ || !output.enabled || !output.height || !output.address) return;
    const auto stamp = [this] { return presentationObserver_ ? SDL_GetTicksNS() : uint64_t(0); };
    FrameMetrics sample{};
    sample.startNs=stamp();
    const XfbState::Entry* selected = nullptr;
    for (const auto& [address, entry] : xfb_->entries) {
        const auto& c = entry.copy;
        const bool frame = output.address == address && output.stride == c.stride && output.height == c.height;
        const bool field = (output.address == address || output.address == address + c.stride) &&
                           output.stride == 2 * c.stride && 2 * output.height == c.height;
        if ((frame || field) && output.width == c.width && !output.xOffset) { selected = &entry; break; }
    }
    if (!selected) throw std::runtime_error("VI selected an XFB without a matching GPU copy/scanout layout");
    FlushPendingTextureCopiesForRange(selected->copy.address,selected->copy.bytes);
    const auto memory = resolve_(selected->copy.address, selected->copy.bytes);
    if (!std::ranges::equal(memory, selected->bytes))
        throw std::runtime_error("VI selected a CPU-modified XFB; RAM upload is not implemented");
    if (xfb_->presented == selected->generation) return;
    aurora::gx::g_gxState.displayCopyTexture = selected->texture;
    aurora::gx::g_gxState.displayCopyProduced = true;
    aurora::video::set_black(false);
    sample.sealStartNs=stamp();
    aurora_end_frame();
    sample.sealEndNs=stamp();
    aurora_wait_for_frame_worker();
    sample.waitEndNs=stamp();
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
    if (presentationObserver_) {
        sample.finishNs=stamp();
        AuroraPresentTiming timing{};
        aurora_get_present_timing(&timing);
        sample.sequence=presentations_; sample.viField=output.field;
        sample.surfacePresents=timing.totalPresentCount;
        sample.readbacks=readbacks_; sample.validationBytes=textureValidationBytes_; sample.draws=draws_;
        sample.queuedPipelines=aurora_get_queued_pipeline_count();
        presentationObserver_(sample);
    }
}

void GxRenderer::CloseXfb() {
    if (!xfb_) return;
    aurora_wait_for_frame_worker();
    aurora::gfx::readback::destroy_stream(xfb_->stream);
    xfb_.reset();
}
}
