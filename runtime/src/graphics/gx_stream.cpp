// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/graphics/gx_stream.h"
#include <stdexcept>
#include <sstream>

namespace Kirby {
namespace { constexpr size_t MaxBytes = 4 * 1024 * 1024; }
uint32_t GxBig32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
GxStream::GxStream(Packet packet, VertexSize vertexSize, Resolve resolve)
    : packet_(std::move(packet)), vertexSize_(std::move(vertexSize)), resolve_(std::move(resolve)) {}
size_t GxStream::PacketSize(std::span<const uint8_t> bytes) {
    if (bytes.empty()) return 0;
    switch (bytes[0]) {
    case 0x00: case 0x48: return 1;
    case 0x08: return 6;
    case 0x10:
        if (bytes.size() < 5) return 0;
        return 5 + size_t((GxBig32(bytes.data() + 1) >> 16) + 1) * 4;
    case 0x20: case 0x28: case 0x30: case 0x38: case 0x61: return 5;
    case 0x40: return 9;
    default:
        if (bytes[0] >= 0x80 && bytes[0] <= 0xBF) {
            if (bytes.size() < 3) return 0;
            const uint32_t count = (uint32_t(bytes[1]) << 8) | bytes[2];
            ++vertexSizeQueries_;
            const auto stride = vertexSize_(bytes[0] & 7);
            if (!stride && count) throw std::runtime_error("GX draw has no vertex attributes");
            const uint64_t size = 3 + uint64_t(count) * stride;
            if (size > MaxBytes) throw std::runtime_error("GX vertex packet exceeds bounded decoder capacity");
            return size;
        }
        std::ostringstream error;
        error << "Unsupported GX opcode 0x" << std::hex << unsigned(bytes[0]);
        throw std::runtime_error(error.str());
    }
}
void GxStream::Dispatch(std::span<const uint8_t> packet, unsigned depth, size_t& budget) {
    if (packet.size() > budget) throw std::runtime_error("GX display-list expansion exceeds bounded decoder capacity");
    budget -= packet.size();
    if (packet[0] == 0x40) {
        if (depth == 16) throw std::runtime_error("GX display-list nesting exceeds 16 levels");
        const auto address = GxBig32(packet.data() + 1), size = GxBig32(packet.data() + 5);
        if ((address & 31) || (size & 31) || size > MaxBytes)
            throw std::runtime_error("GX display-list address or size is invalid");
        if (size) {
            const auto source = resolve_(address, size);
            if (source.size() != size) throw std::runtime_error("GX display list lies outside guest RAM");
            // Decode a stable snapshot even if the backend changes guest memory.
            const std::vector<uint8_t> copy(source.begin(), source.end());
            std::span<const uint8_t> remaining(copy);
            while (!remaining.empty()) {
                const auto length = PacketSize(remaining);
                if (!length || length > remaining.size()) throw std::runtime_error("Truncated GX command in display list");
                Dispatch(remaining.first(length), depth + 1, budget);
                remaining = remaining.subspan(length);
            }
        }
        ++lists_;
    } else packet_(packet);
    ++packets_;
}
void GxStream::Feed(std::span<const uint8_t> bytes) {
    if (failed_) throw std::runtime_error("GX stream cannot resume after a command failure");
    try {
        if (bytes.size() > MaxBytes - pending_.size()) throw std::runtime_error("GX stream pending data exceeds bounded decoder capacity");
        pending_.insert(pending_.end(), bytes.begin(), bytes.end());
        size_t offset = 0, budget = MaxBytes;
        while (offset < pending_.size()) {
            const auto remaining = std::span<const uint8_t>(pending_).subspan(offset);
            // Once its header is known, an incomplete FIFO command cannot be
            // preceded by another CP format change. Keep its length until the
            // payload arrives instead of deriving the same stride per fragment.
            if (!reusePacketLength_ || !pendingPacketSize_) pendingPacketSize_=PacketSize(remaining);
            const auto length = pendingPacketSize_;
            if (!length || length > remaining.size()) break;
            pendingPacketSize_=0;
            Dispatch(remaining.first(length), 0, budget);
            offset += length;
        }
        pending_.erase(pending_.begin(), pending_.begin() + offset);
    } catch (...) {
        // Some earlier packets may already have reached the backend. Do not
        // retry this batch and duplicate their effects after an exception.
        failed_ = true;
        throw;
    }
}
}
