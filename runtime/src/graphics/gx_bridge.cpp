// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/graphics/gx_fifo.h"
#include <bit>

// Both translated constant-address stores and the checked memory slow paths
// use these exports. Upstream implementations are renamed only in their own
// source files; translated objects need no regeneration or recompilation.
extern "C" void GX_HLE_FIFO_WriteBurst(const uint8_t* data, uint32_t length) {
    Kirby::GraphicsFifo().Gather({data, length});
}
extern "C" void GX_HLE_FIFO_Write8(uint8_t value) {
    GX_HLE_FIFO_WriteBurst(&value, 1);
}
extern "C" void GX_HLE_FIFO_Write16(uint16_t value) {
    const uint8_t bytes[]{uint8_t(value >> 8), uint8_t(value)};
    GX_HLE_FIFO_WriteBurst(bytes, 2);
}
extern "C" void GX_HLE_FIFO_Write32(uint32_t value) {
    const uint8_t bytes[]{uint8_t(value >> 24), uint8_t(value >> 16), uint8_t(value >> 8), uint8_t(value)};
    GX_HLE_FIFO_WriteBurst(bytes, 4);
}
extern "C" void GX_HLE_FIFO_WriteFloat(float value) {
    GX_HLE_FIFO_Write32(std::bit_cast<uint32_t>(value));
}
