// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>

namespace Kirby {
// Host monotonic timestamps, not guest logic-step or compositor scanout times.
struct FrameMetrics {
    uint64_t sequence = 0, viField = 0;
    uint64_t startNs = 0, sealStartNs = 0, sealEndNs = 0, waitEndNs = 0, finishNs = 0;
    uint64_t surfacePresents = 0, readbacks = 0, validationBytes = 0, draws = 0;
    uint64_t queuedPipelines = 0;
};
}
