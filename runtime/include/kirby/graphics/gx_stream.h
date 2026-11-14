// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace Kirby {
// Reassembles the byte-oriented hardware FIFO before passing complete packets
// to a backend. Guest display lists are bounded and expanded here, not skipped.
class GxStream {
public:
    using Packet = std::function<void(std::span<const uint8_t>)>;
    using VertexSize = std::function<uint32_t(uint8_t)>;
    using Resolve = std::function<std::span<const uint8_t>(uint32_t, uint32_t)>;
    GxStream(Packet packet, VertexSize vertexSize, Resolve resolve);
    void Feed(std::span<const uint8_t> bytes);
    size_t PendingBytes() const { return pending_.size(); }
    uint64_t Packets() const { return packets_; }
    uint64_t DisplayLists() const { return lists_; }
    uint64_t VertexSizeQueries() const { return vertexSizeQueries_; }
    // Diagnostic baseline; neither mode changes the packet byte stream.
    void SetPacketLengthReuse(bool enabled) { reusePacketLength_=enabled; pendingPacketSize_=0; }
private:
    size_t PacketSize(std::span<const uint8_t> bytes);
    void Dispatch(std::span<const uint8_t> packet, unsigned depth, size_t& budget);
    Packet packet_;
    VertexSize vertexSize_;
    Resolve resolve_;
    std::vector<uint8_t> pending_;
    uint64_t packets_ = 0, lists_ = 0;
    bool failed_ = false;
    uint64_t vertexSizeQueries_ = 0;
    size_t pendingPacketSize_ = 0;
    bool reusePacketLength_ = true;
};
uint32_t GxBig32(const uint8_t* bytes);
}
