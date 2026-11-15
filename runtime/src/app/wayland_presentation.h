// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <memory>
struct SDL_Window;
namespace Kirby {
class PresentationLog;
class WaylandPresentationTrace {
public:
    WaylandPresentationTrace(SDL_Window* window,PresentationLog& log);
    ~WaylandPresentationTrace();
    WaylandPresentationTrace(const WaylandPresentationTrace&)=delete;
    WaylandPresentationTrace& operator=(const WaylandPresentationTrace&)=delete;
    void Poll();
    void Request(uint64_t index);
    void Finish();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
