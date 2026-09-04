#include "loglens/gui/log_load_worker.hpp"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

int main() {
    loglens::LogLoadWorker worker;
    std::atomic<bool> start{false};
    std::vector<std::thread> callers;
    for (std::uint64_t thread = 0; thread < 4; ++thread) {
        callers.emplace_back([&worker, &start, thread] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (std::uint64_t index = 1; index <= 20000; ++index) {
                const std::uint64_t job = index + thread * 20000;
                worker.selectJob(job);
                worker.setFollowing(job, (index & 1U) != 0U);
            }
        });
    }
    start.store(true, std::memory_order_release);
    for (std::thread& caller : callers) caller.join();
    worker.selectJob(0);
    worker.setFollowing(0, false);
    std::cout << "cross-thread cancellation controls completed\n";
    return 0;
}
