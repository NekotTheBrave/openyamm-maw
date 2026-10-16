#pragma once

#include <cstddef>
#include <functional>

namespace OpenYAMM::Engine
{
// The most threads parallelFor uses, the calling thread included. Load-time decode work stops scaling well beyond
// this, and each running task holds its decoded data.
constexpr size_t MaxParallelForThreads = 8;

// Runs task(index) for every index in [0, count) on the calling thread and, when the host has more hardware threads,
// on up to MaxParallelForThreads - 1 workers. Returns when every task has finished; the first exception a task threw
// is rethrown then (the remaining tasks still run). Tasks must not touch shared state without their own
// synchronization, and must not call bgfx.
void parallelFor(size_t count, const std::function<void(size_t)> &task);
}
