#include "engine/ParallelFor.h"

#include <algorithm>
#include <atomic>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace OpenYAMM::Engine
{
void parallelFor(size_t count, const std::function<void(size_t)> &task)
{
    const size_t hardwareThreads = std::max(1u, std::thread::hardware_concurrency());
    const size_t threads = std::min({count, hardwareThreads, MaxParallelForThreads});

    if (threads <= 1)
    {
        for (size_t index = 0; index < count; ++index)
        {
            task(index);
        }
        return;
    }

    std::atomic<size_t> next = 0;
    std::exception_ptr firstException;
    std::mutex exceptionMutex;
    const auto run = [&]()
    {
        for (size_t index = next.fetch_add(1); index < count; index = next.fetch_add(1))
        {
            try
            {
                task(index);
            }
            catch (...)
            {
                const std::lock_guard lock(exceptionMutex);
                if (!firstException)
                {
                    firstException = std::current_exception();
                }
            }
        }
    };

    // std::thread rather than std::jthread: libc++ (the Android NDK's) keeps jthread behind its experimental flag.
    std::vector<std::thread> workers;
    workers.reserve(threads - 1);
    for (size_t worker = 1; worker < threads; ++worker)
    {
        workers.emplace_back(run);
    }
    run();
    for (std::thread &worker : workers)
    {
        worker.join();
    }

    if (firstException)
    {
        std::rethrow_exception(firstException);
    }
}
}
