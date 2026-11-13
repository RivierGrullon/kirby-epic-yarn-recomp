// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "kirby/graphics/frame_metrics.h"
#include <cstdio>
#include <filesystem>
#include <memory>

namespace Kirby {
class FrameLog {
public:
    explicit FrameLog(const std::filesystem::path& path);
    void Write(const FrameMetrics& sample);
private:
    std::unique_ptr<FILE, decltype(&std::fclose)> file_{nullptr,std::fclose};
};
}
