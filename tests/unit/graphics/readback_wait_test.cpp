// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/graphics/readback_wait.h"
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;
void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
int main() {
    try {
        Kirby::ReadbackWait completed;
        completed.Notify();
        Check(completed.WaitFor(0ms), "completion before waiting must not be lost");
        Check(completed.WaitFor(0ms), "completion remains observable for subsequent checks");
        Kirby::ReadbackWait pending;
        Check(!pending.WaitFor(1ms), "an incomplete readback must still time out");
        std::promise<void> entered;
        auto waiter = std::async(std::launch::async, [&] {
            entered.set_value();
            return pending.WaitFor(10s);
        });
        entered.get_future().wait();
        pending.Notify();
        Check(waiter.wait_for(1s) == std::future_status::ready,
              "GPU completion must wake the waiter without waiting for its polling deadline");
        Check(waiter.get(), "a completion notification must be observed");
        // Exercise both notification orderings repeatedly. No polling or spinning.
        for (unsigned i=0; i<256; ++i) {
            Kirby::ReadbackWait signal;
            std::jthread producer([&] { signal.Notify(); });
            Check(signal.WaitFor(1s), "concurrent completion must not lose a wakeup");
        }
        std::cout << "Readback completion: early notification, timeout, wakeup and 256 races passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
