// SPDX-License-Identifier: GPL-2.0-or-later

#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <random>
#include <stdexcept>
#include <thread>

#include "descriptor_payload.h"

namespace {
void Check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void TestDelayedReads() {
    Vulkan::DescriptorPayload<int, 4, 3> payload;
    std::deque<std::function<void()>> commands;
    size_t waits{};
    auto drain = [&] {
        while (!commands.empty()) {
            auto command = std::move(commands.front());
            commands.pop_front();
            command();
        }
    };
    auto wait = [&] {
        ++waits;
        drain();
    };
    auto retire = [&](auto completion) { commands.emplace_back(std::move(completion)); };
    auto upload = [&](size_t count, int value) {
        payload.Acquire(count, wait, retire);
        for (size_t i = 0; i < count; ++i) {
            payload.Append(value);
        }
        const int* data = payload.Data();
        commands.emplace_back([=] {
            for (size_t i = 0; i < count; ++i) {
                Check(data[i] == value, "Pending descriptor data was overwritten");
            }
        });
    };

    payload.Acquire(0, wait, retire);
    Check(payload.Size() == 0 && waits == 0, "Empty reservation waited");
    upload(4, 1);
    upload(4, 2);
    upload(4, 3);
    Check(waits == 0, "Unused blocks should avoid a worker drain");
    upload(4, 4);
    Check(waits == 1, "Exhausting all pending blocks must drain the worker");
    drain();
    upload(4, 5);
    Check(waits == 1, "Completed blocks should be reused without a drain");
    // Repeated oversized uploads must preserve each pending upload, including
    // when returning to small reservations.
    upload(17, 6);
    upload(23, 7);
    upload(31, 8);
    upload(1, 9);
    drain();

    payload.Acquire(1, wait, retire);
    payload.Append(10);
    bool rejected{};
    try {
        payload.Append(11);
    } catch (const std::length_error&) {
        rejected = true;
    }
    Check(rejected, "Reservation overrun was accepted");
    drain();
}

// Exercise producer/worker publication with real threads, mixed reservation
// sizes, and worker lag. Each queued read verifies the complete original upload.
void TestConcurrentReads() {
    Vulkan::DescriptorPayload<int, 64, 3> payload;
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<std::function<void()>> commands;
    bool stop{};
    std::exception_ptr failure;
    std::thread worker{[&] {
        for (;;) {
            std::function<void()> command;
            {
                std::unique_lock lock{mutex};
                cv.wait(lock, [&] { return stop || !commands.empty(); });
                if (commands.empty()) {
                    return;
                }
                command = std::move(commands.front());
                commands.pop_front();
            }
            try {
                command();
            } catch (...) {
                failure = std::current_exception();
            }
        }
    }};
    auto enqueue = [&](std::function<void()> command) {
        {
            std::scoped_lock lock{mutex};
            commands.push_back(std::move(command));
        }
        cv.notify_one();
    };
    auto wait = [&] {
        std::promise<void> done;
        auto future = done.get_future();
        enqueue([&] { done.set_value(); });
        future.get();
    };
    std::mt19937 random{0x4d4852};
    for (int value = 1; value <= 20000; ++value) {
        const size_t count = random() % 160;
        payload.Acquire(count, wait, enqueue);
        for (size_t i = 0; i < count; ++i) {
            payload.Append(value);
        }
        const int* data = payload.Data();
        enqueue([=] {
            for (size_t i = 0; i < count; ++i) {
                Check(data[i] == value, "Concurrent worker read overwritten descriptors");
            }
        });
    }
    wait();
    {
        std::scoped_lock lock{mutex};
        stop = true;
    }
    cv.notify_one();
    worker.join();
    if (failure) {
        std::rethrow_exception(failure);
    }
}
} // namespace

int main() {
    try {
        TestDelayedReads();
        TestConcurrentReads();
        std::cout << "Descriptor payload lifetime and concurrency tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
