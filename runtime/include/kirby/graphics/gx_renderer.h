// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "kirby/graphics/gx_stream.h"
#include "kirby/graphics/frame_metrics.h"
#include <string>
#include <memory>
#include <array>
#include <bitset>
#include "kirby/devices/video_si.h"

namespace Kirby {
class GxRenderer {
public:
    using WriteMemory = std::function<void(uint32_t, std::span<const uint8_t>)>;
    explicit GxRenderer(GxStream::Resolve resolve, WriteMemory write = {});
    ~GxRenderer();
    GxRenderer(const GxRenderer&) = delete;
    GxRenderer& operator=(const GxRenderer&) = delete;
    void OpenWindow(bool startHidden = false, bool vsync = true);
    // Guest execution is suspended while this synchronous batch is decoded.
    void Feed(std::span<const uint8_t> bytes) { InvalidateTextureValidation(); stream_.Feed(bytes); }
    const GxStream& Stream() const { return stream_; }
    uint64_t RegisterWrites() const { return registerWrites_; }
    uint64_t DisplayCopies() const { return displayCopies_; }
    uint64_t TextureCopies() const { return textureCopies_; }
    uint64_t Presentations() const { return presentations_; }
    void PollVideo(const VideoOutput& output);
    void ObservePresentation(std::function<void(const FrameMetrics&)> observer) { presentationObserver_=std::move(observer); }
    // Call FlushTextureCopies before the CPU reads or writes a deferred destination.
    void SetDeferredTextureCopies(bool enabled);
    // Experimental: cached CPU readers must invalidate before accessing copies.
    void SetGpuResidentTextureCopies(bool enabled);
    // VI presents the GPU image; audited CPU consumers materialize XFB RAM on demand.
    void SetGpuResidentDisplayCopies(bool enabled);
    void SetTextureSnapshotReuse(bool enabled) { textureSnapshotReuse_=enabled; }
    // Diagnostic control for comparing identical scenes with per-draw validation.
    void SetTextureBatchValidation(bool enabled) { textureBatchValidation_=enabled; InvalidateTextureValidation(); }
    void WritebackTextureCopiesForRange(uint32_t address, uint64_t size);
    void FlushTextureCopies();
    void FlushTextureCopiesForRange(uint32_t address, uint64_t size);
    uint64_t Readbacks() const { return readbacks_; }
    uint64_t TextureValidationBytes() const { return textureValidationBytes_; }
    uint64_t ShaderAnalyses() const { return shaderAnalyses_; }
    void SetShaderAnalysisReuse(bool enabled) { shaderAnalysisReuse_=enabled; sampledFormat_=0xFF; }
    // CPU bytes copied for deferred-copy validation, excluding GPU readback payloads.
    uint64_t DeferredSnapshotBytes() const { return deferredSnapshotBytes_; }
    void SetDeferredSnapshotSharing(bool enabled) { deferredSnapshotSharing_=enabled; }
    struct TextureCacheMetrics {
        uint64_t lookups=0, keyMisses=0, evictions=0, contentChanges=0, snapshotBytes=0;
    };
    const TextureCacheMetrics& TextureCacheStats() const { return textureCacheMetrics_; }
private:
    struct XfbState;
    struct TextureState;
    void CacheTextureBinding(unsigned slot, std::span<const uint8_t> memory);
    void InvalidateTextureValidation();
    void CopyDisplay(uint32_t value);
    void CommitTextureCopies();
    void FlushDisplayCopiesForRange(uint32_t address, uint64_t size);
    void FlushPendingTextureCopiesForRange(uint32_t address, uint64_t size);
    void CopyTexture(uint32_t value, uint32_t control);
    void ValidateCopyTexture(uint32_t address, uint32_t format, uint32_t width, uint32_t height, uint32_t mipCount, uint32_t bytes);
    void Draw(std::span<const uint8_t> bytes);
    void LoadPalette(uint32_t value);
    void CloseXfb();
    void Packet(std::span<const uint8_t> bytes);
    uint32_t VertexSize(uint8_t format) const;
    void ArrayBase(uint8_t reg, uint32_t value);
    GxStream::Resolve resolve_;
    WriteMemory write_;
    GxStream stream_;
    bool initialized_ = false;
    bool deferredTextureCopies_ = false;
    bool gpuResidentTextureCopies_ = false;
    bool gpuResidentDisplayCopies_ = false;
    uint64_t readbacks_ = 0;
    std::string userPath_, cachePath_;
    uint64_t registerWrites_ = 0, draws_ = 0;
    uint64_t displayCopies_ = 0, presentations_ = 0, textureCopies_ = 0;
    uint64_t nextTitleUpdate_ = 0;
    std::shared_ptr<XfbState> xfb_;
    std::vector<uint8_t> paletteMemory_, paletteValid_;
    std::array<std::vector<uint8_t>, 21> indexedSnapshots_;
    std::shared_ptr<TextureState> textureState_;
    bool textureSnapshotReuse_ = false;
    uint64_t textureValidationEpoch_ = 0;
    uint64_t textureValidationBytes_ = 0;
    bool textureBatchValidation_ = true;
    std::function<void(const FrameMetrics&)> presentationObserver_;
    uint64_t shaderAnalyses_ = 0;
    uint64_t sampledTexturesEpoch_ = 0;
    std::bitset<8> sampledTextures_;
    uint8_t sampledFormat_ = 0xFF, sampledPrimitive_ = 0xFF;
    bool shaderAnalysisReuse_ = true;
    uint64_t deferredSnapshotBytes_ = 0;
    bool deferredSnapshotSharing_ = true;
    TextureCacheMetrics textureCacheMetrics_;
};
}
