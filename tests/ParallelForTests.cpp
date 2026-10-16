#include "doctest/doctest.h"

#include "engine/ParallelFor.h"

#include <atomic>
#include <stdexcept>
#include <vector>

using namespace OpenYAMM;

TEST_CASE("parallelFor runs every index once and rethrows the first failure after all tasks")
{
    std::vector<std::atomic<int>> calls(1000);
    Engine::parallelFor(calls.size(), [&calls](size_t index)
    {
        ++calls[index];
    });
    for (const std::atomic<int> &count : calls)
    {
        CHECK_EQ(count.load(), 1);
    }

    std::atomic<int> finished = 0;
    CHECK_THROWS_AS(Engine::parallelFor(64, [&finished](size_t index)
    {
        ++finished;
        if (index == 7)
        {
            throw std::runtime_error("task failed");
        }
    }), std::runtime_error);
    CHECK_EQ(finished.load(), 64);

    Engine::parallelFor(0, [](size_t)
    {
        FAIL("no task runs for an empty range");
    });
}
