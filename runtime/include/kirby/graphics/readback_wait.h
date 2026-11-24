// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace Kirby {
// Each GPU copy batch owns one signal. Completion can precede WaitFor, and the
// GPU callback can outlive a timed-out caller through its shared ownership.
class ReadbackWait {
public:
    void Notify() {
        {
            std::lock_guard lock(mutex_);
            complete_ = true;
        }
        changed_.notify_all();
    }
    bool WaitFor(std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, timeout, [this] { return complete_; });
    }
private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool complete_ = false;
};
}
