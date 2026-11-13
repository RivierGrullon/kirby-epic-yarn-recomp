// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "kirby/graphics/gx_stream.h"
#include <string>
#include <memory>
#include <array>
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
    void Feed(std::span<const uint8_t> bytes) { stream_.Feed(bytes); }
    const GxStream& Stream() const { return stream_; }
    uint64_t RegisterWrites() const { return registerWrites_; }
    uint64_t DisplayCopies() const { return displayCopies_; }
    uint64_t TextureCopies() const { return textureCopies_; }
    uint64_t Presentations() const { return presentations_; }
    void PollVideo(const VideoOutput& output);
    // Call FlushTextureCopies before the CPU reads or writes a deferred destination.
    void SetDeferredTextureCopies(bool enabled);
    // Experimental: cached CPU readers must invalidate before accessing copies.
    void SetGpuResidentTextureCopies(bool enabled);
    // VI presents the GPU image; audited CPU consumers materialize XFB RAM on demand.
    void SetGpuResidentDisplayCopies(bool enabled);
    void WritebackTextureCopiesForRange(uint32_t address, uint64_t size);
    void FlushTextureCopies();
    void FlushTextureCopiesForRange(uint32_t address, uint64_t size);
    uint64_t Readbacks() const { return readbacks_; }
private:
    struct XfbState;
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
};
}
