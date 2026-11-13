// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/graphics/gx_renderer.h"
#include "gx/command_processor.hpp"
#include "gx/pipeline.hpp"
#include "gx/shader_info.hpp"
#include "gx/texture.hpp"
#include <iostream>
#include <algorithm>
#include "gx/regs.hpp"
#include <stdexcept>

namespace Kirby {
struct GxRenderer::TextureState {
    struct Key {
        const void* data = nullptr;
        const void* palette = nullptr;
        std::array<uint32_t,8> layout{};
        bool operator==(const Key&) const = default;
    };
    struct Entry {
        Key key;
        std::vector<uint8_t> bytes, palette;
        uint32_t id = 0, paletteId = 0, version = 0;
        uint64_t used = 0;
        uint64_t validatedEpoch = 0;
    };
    // At most 128 MiB of texture snapshots plus 512 KiB of palette snapshots.
    // Reuse object IDs when evicting so Aurora's object cache stays bounded too.
    std::array<Entry,16> entries;
    uint64_t sequence = 0;
};
void GxRenderer::InvalidateTextureValidation() {
    if (++textureValidationEpoch_ == 0) throw std::runtime_error("Texture validation epoch overflow");
}
void GxRenderer::CacheTextureBinding(unsigned slot,std::span<const uint8_t> memory) {
    using namespace aurora::gx;
    auto& texture=g_gxState.loadedTextures[slot];
    texture.texObjId=0;
    if (!textureSnapshotReuse_ || memory.size()>8*1024*1024) {
        g_gxState.dirty |= DirtyTextures;
        return;
    }
    GXTlutObj_* palette=nullptr;
    if (texture.format()==GX_TF_C4 || texture.format()==GX_TF_C8 || texture.format()==GX_TF_C14X2)
        palette=&g_gxState.loadedTluts[slot];
    const auto paletteBytes=palette ? std::span(static_cast<const uint8_t*>(palette->data),size_t(palette->numEntries)*2)
                                    : std::span<const uint8_t>{};
    if (!textureState_) textureState_=std::make_shared<TextureState>();
    auto& cache=*textureState_;
    const TextureState::Key key{memory.data(),palette ? palette->data : nullptr,
        {texture.format(),texture.width(),texture.height(),texture.mip_count(),texture.mode0,texture.mode1,
         palette ? uint32_t(palette->format) : 0,palette ? uint32_t(palette->numEntries) : 0}};
    auto found=std::ranges::find(cache.entries,key,&TextureState::Entry::key);
    if (found==cache.entries.end())
        found=std::ranges::min_element(cache.entries,{},&TextureState::Entry::used);
    auto& entry=*found;
    if (!entry.id) {
        GXTexObj object{};
        GXInitTexObj(&object,nullptr,1,1,GX_TF_RGBA8,GX_CLAMP,GX_CLAMP,GX_FALSE);
        entry.id=reinterpret_cast<const GXTexObj_*>(&object)->texObjId;
    }
    if (palette && !entry.paletteId) {
        GXTlutObj object{};
        GXInitTlutObj(&object,palette->data,palette->format,palette->numEntries);
        entry.paletteId=reinterpret_cast<const GXTlutObj_*>(&object)->tlutObjId;
    }
    const bool validated = textureBatchValidation_ && entry.key == key &&
                           entry.validatedEpoch == textureValidationEpoch_;
    if (!validated) textureValidationBytes_ += memory.size() + paletteBytes.size();
    if (!validated && (entry.key!=key || !std::ranges::equal(memory,entry.bytes) ||
                       !std::ranges::equal(paletteBytes,entry.palette))) {
        entry.key=key;
        entry.bytes.assign(memory.begin(),memory.end());
        entry.palette.assign(paletteBytes.begin(),paletteBytes.end());
        if (++entry.version==0) throw std::runtime_error("Texture snapshot version overflow");
        g_gxState.dirty |= DirtyTextures;
    }
    entry.validatedEpoch=textureValidationEpoch_;
    entry.used=++cache.sequence;
    texture.texObjId=entry.id;
    texture.texDataVersion=entry.version;
    if (palette) {
        palette->tlutObjId=entry.paletteId;
        palette->tlutDataVersion=entry.version;
    }
}
void GxRenderer::LoadPalette(uint32_t value) {
    using namespace aurora::gx;
    const auto mask = g_gxState.bpRegCache[0xFE];
    const auto control = ((g_gxState.bpRegCache[0x65] & ~mask) | (value & mask)) & 0xFFFFFF;
    const uint32_t destination = (control & 0x3FF) << 9;
    const uint32_t size = ((control >> 10) & 0x7FF) * 32;
    const uint32_t source = (g_gxState.bpRegCache[0x64] & 0xFFFFFF) << 5;
    if (!size || uint64_t(destination)+size > 0x80000)
        throw std::runtime_error("GX palette load exceeds palette TMEM");
    FlushTextureCopiesForRange(source,size);
    const auto memory = resolve_(source, size);
    if (memory.size() != size) throw std::runtime_error("GX palette source lies outside guest RAM");
    if (paletteMemory_.empty()) { paletteMemory_.resize(0x80000); paletteValid_.resize(0x80000); }
    InvalidateTextureValidation();
    std::copy(memory.begin(), memory.end(), paletteMemory_.begin()+destination);
    std::fill_n(paletteValid_.begin()+destination, size, 1);
    fifo::handle_bp(value);
    g_gxState.dirty |= DirtyTextures;
}
void GxRenderer::Draw(std::span<const uint8_t> bytes) {
    using namespace aurora::gx;
    if (!initialized_) throw std::runtime_error("GX draw requires initialized Aurora; run --video");
    const auto format = static_cast<GXVtxFmt>(bytes[0] & 7);
    auto primitive = static_cast<GXPrimitive>(bytes[0] & 0xF8);
    if (primitive == static_cast<GXPrimitive>(0x88)) primitive = GX_QUADS;
    auto& state = g_gxState;
    // Check the referenced prefix on every draw: guest RAM can change without
    // a new CP base. Reuse the current frame's immutable GPU upload when equal.
    const auto count = (unsigned(bytes[1]) << 8) | bytes[2];
    const auto stride = VertexSize(format);
    size_t offset = 3;
    for (unsigned attr = GX_VA_PNMTXIDX; attr <= GX_VA_TEX7; ++attr) {
        const auto desc = state.vtxDesc[attr];
        if (desc == GX_NONE) continue;
        const auto& fmt = state.vtxFmts[format].attrs[attr];
        if (desc == GX_DIRECT) {
            offset += comp_type_size(static_cast<GXAttr>(attr),fmt.type) *
                      comp_cnt_count(static_cast<GXAttr>(attr),fmt.cnt);
            continue;
        }
        if (attr < GX_VA_POS) throw std::runtime_error("GX matrix-index attributes must be direct");
        if (attr == GX_VA_NRM && fmt.cnt != GX_NRM_XYZ)
            throw std::runtime_error("GX indexed NBT normals need a reviewed array layout");
        const auto indexBytes = desc == GX_INDEX8 ? 1u : 2u;
        uint32_t maximum = 0;
        for (unsigned vertex = 0; vertex < count; ++vertex) {
            const auto at = offset + size_t(vertex)*stride;
            if (at+indexBytes > bytes.size()) throw std::runtime_error("GX indexed vertex packet is truncated");
            const uint32_t index = indexBytes == 1 ? bytes[at] : (unsigned(bytes[at]) << 8) | bytes[at+1];
            maximum = std::max(maximum,index);
        }
        auto& array = state.arrays[attr];
        const auto elementBytes = comp_type_size(static_cast<GXAttr>(attr),fmt.type) *
                                  comp_cnt_count(static_cast<GXAttr>(attr),fmt.cnt);
        const uint64_t size = uint64_t(maximum)*array.stride + elementBytes;
        if (size > 16*1024*1024) throw std::runtime_error("GX indexed array exceeds bounded upload capacity");
        const auto address = state.cpRegCache[0xA0+attr-GX_VA_POS] & 0x1FFFFFFF;
        FlushTextureCopiesForRange(address,size);
        const auto memory = resolve_(address,size);
        if (memory.size() != size) throw std::runtime_error("GX indexed array lies outside guest RAM");
        auto& snapshot = indexedSnapshots_[attr];
        const bool unchanged = array.data == memory.data() && !array.le &&
            array.cachedRange.size >= size && snapshot.size() >= size &&
            std::equal(memory.begin(), memory.end(), snapshot.begin());
        if (!unchanged) {
            array.cachedRange = {};
            snapshot.assign(memory.begin(), memory.end());
            // A merged draw keeps the previous draw's array offsets. Uploading
            // new bytes (or a larger prefix) requires its own immediate data.
            state.dirty |= DirtyImmediates;
        }
        array.data = memory.data(); array.size = size; array.le = false;
        offset += indexBytes;
    }
    PipelineConfig config{};
    populate_pipeline_config(config, primitive, format);
    const auto info = build_shader_info(config.shaderConfig);
    for (unsigned i = 0; i < MaxTextures; ++i) {
        if (!info.sampledTextures.test(i)) continue;
        auto& texture = state.loadedTextures[i];
        switch (texture.format()) {
        case GX_TF_I4: case GX_TF_I8: case GX_TF_IA4: case GX_TF_IA8:
        case GX_TF_RGB565: case GX_TF_RGB5A3: case GX_TF_RGBA8: case GX_TF_CMPR: break;
        case GX_TF_C4: case GX_TF_C8: case GX_TF_C14X2: {
            const auto reg = (i < 4 ? 0x98 : 0xB8) + (i & 3);
            const auto tlut = state.bpRegCache[reg];
            const auto offset = (tlut & 0x3FF) << 9;
            const auto entries = texture.format() == GX_TF_C4 ? 16u : texture.format() == GX_TF_C8 ? 256u : 16384u;
            const auto format = (tlut >> 10) & 3;
            if (format > GX_TL_RGB5A3 || uint64_t(offset)+entries*2 > paletteMemory_.size() ||
                !std::all_of(paletteValid_.begin()+offset, paletteValid_.begin()+offset+entries*2,
                             [](uint8_t valid) { return valid != 0; }))
                throw std::runtime_error("GX sampled palette has invalid format or unloaded TMEM entries");
            auto& palette = state.loadedTluts[i];
            palette = {};
            palette.data = paletteMemory_.data()+offset;
            palette.numEntries = entries;
            palette.format = static_cast<GXTlutFmt>(format);
            texture.tlut = static_cast<GXTlut>(i);
            break;
        }
        default: throw std::runtime_error("GX sampled texture format is unsupported");
        }
        const auto size = texture::texture_source_size(texture.format(), texture.width(),
                                                       texture.height(), texture.mip_count());
        if (!size || size > UINT32_MAX) throw std::runtime_error("GX sampled texture has invalid dimensions");
        const auto address = (texture.image3 & 0xFFFFFF) << 5;
        const auto memory = resolve_(address, size);
        if (memory.size() != size) throw std::runtime_error("GX sampled texture lies outside guest RAM");
        ValidateCopyTexture(address,texture.format(),texture.width(),texture.height(),texture.mip_count(),size);
        texture.data = memory.data();
        // Validate once per synchronous batch, and again after renderer RAM or
        // palette writes. Raw CPU edits are checked when the next batch arrives.
        CacheTextureBinding(i,memory);
    }
    const auto result = fifo::process(bytes.data(), bytes.size());
    if (result.bytesProcessed != bytes.size()) throw std::runtime_error("Aurora did not consume the GX draw");
    if (++draws_ == 1)
        std::cout << "[kirby] first GX draw submitted: vertices="
                  << ((unsigned(bytes[1]) << 8) | bytes[2])
                  << " sampled textures=" << info.sampledTextures.count() << '\n';
}
}
