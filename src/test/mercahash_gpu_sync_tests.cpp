// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <array>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace gpu_sync_emulation
{
constexpr unsigned LANES{32};
constexpr auto WAIT_LIMIT{std::chrono::seconds{10}};
struct Cancelled {};

// This models the ordering contract of __syncwarp, not GPU execution. Holding
// every other lane before ReadLine exposes a missing read-before-write barrier.
class Warp
{
    std::mutex m_mutex;
    std::condition_variable m_cv;
    unsigned m_arrivals{0}, m_generation{0};
    bool m_first_barrier{false}, m_release_readers{false}, m_aborted{false};

  public:
    void Abort()
    {
        std::lock_guard lock{m_mutex};
        m_aborted = true;
        m_cv.notify_all();
    }
    bool Aborted()
    {
        std::lock_guard lock{m_mutex};
        return m_aborted;
    }
    void WaitToRead()
    {
        std::unique_lock lock{m_mutex};
        if (!m_cv.wait_for(lock, WAIT_LIMIT, [&] { return m_release_readers || m_aborted; })) {
            m_aborted = true;
            m_cv.notify_all();
        }
        if (m_aborted)
            throw Cancelled{};
    }
    bool WaitForFirstBarrier()
    {
        std::unique_lock lock{m_mutex};
        if (!m_cv.wait_for(lock, WAIT_LIMIT, [&] { return m_first_barrier || m_aborted; })) {
            m_aborted = true;
            m_cv.notify_all();
        }
        return m_first_barrier && !m_aborted;
    }
    void ReleaseReaders()
    {
        std::lock_guard lock{m_mutex};
        m_release_readers = true;
        m_cv.notify_all();
    }
    void Sync(unsigned lane)
    {
        std::unique_lock lock{m_mutex};
        if (m_aborted)
            throw Cancelled{};
        if (lane == 0 && !m_first_barrier) {
            m_first_barrier = true;
            m_cv.notify_all();
        }
        const unsigned generation{m_generation};
        if (++m_arrivals == LANES) {
            m_arrivals = 0;
            ++m_generation;
            m_cv.notify_all();
            return;
        }
        if (!m_cv.wait_for(lock, WAIT_LIMIT, [&] { return m_generation != generation || m_aborted; })) {
            m_aborted = true;
            m_cv.notify_all();
        }
        if (m_aborted)
            throw Cancelled{};
    }
};

struct Dimension {
    unsigned x{0};
};
const Dimension blockIdx{};
thread_local Dimension threadIdx{};
thread_local Warp *current_warp{nullptr};
void EmulateSyncWarp()
{
    current_warp->Sync(threadIdx.x);
}

// Compile the actual CUDA branch as ordinary C++, with no vendor SDK. The
// unused kernel entrypoints are inline and uniquely named in this test.
#define MH_CUDA
#define __device__
#define __constant__ constexpr
#define __forceinline__ inline
#define __global__ inline
#define __syncwarp EmulateSyncWarp
#define mh_init gpu_sync_unused_init
#define mh_stage gpu_sync_unused_stage
#include <mining/mercahash_gpu.h>
#undef mh_stage
#undef mh_init
#undef __syncwarp
#undef __global__
#undef __forceinline__
#undef __constant__
#undef __device__
#undef MH_CUDA

struct JoinOnExit {
    Warp &warp;
    std::vector<std::thread> &threads;
    ~JoinOnExit()
    {
        bool pending{false};
        for (auto &thread : threads)
            pending |= thread.joinable();
        if (pending)
            warp.Abort();
        for (auto &thread : threads)
            if (thread.joinable())
                thread.join();
    }
};
} // namespace gpu_sync_emulation

BOOST_AUTO_TEST_SUITE(mercahash_gpu_sync_tests)
BOOST_AUTO_TEST_CASE(cooperative_store_waits_for_every_old_line_reader)
{
    using namespace gpu_sync_emulation;
    std::array<mh_u64, 8> scratch{};
    for (unsigned word{0}; word < scratch.size(); ++word)
        scratch[word] = 100 + word;
    const auto original{scratch};
    auto expected{original};
    for (auto &word : expected)
        word += original[0] + 17;
    Warp warp;
    std::vector<std::thread> threads;
    threads.reserve(LANES);
    JoinOnExit cleanup{warp, threads};
    bool all_threads_started{true};
    try {
        for (unsigned lane{0}; lane < LANES; ++lane) {
            threads.emplace_back([&, lane] {
                current_warp = &warp;
                threadIdx.x = lane;
                try {
                    if (lane != 0)
                        warp.WaitToRead();
                    const Line old{ReadLine(scratch.data(), 0, 1, 0)};
                    Line replacement{old};
                    for (auto &word : replacement.v)
                        word += old.v[0] + 17;
                    WriteLine(scratch.data(), 0, replacement, 1, 0);
                } catch (...) {
                    warp.Abort();
                }
            });
        }
    } catch (...) {
        all_threads_started = false;
        warp.Abort();
    }
    const bool first_barrier_seen{all_threads_started && warp.WaitForFirstBarrier()};
    // Lane 0 is blocked at its first barrier; all other lanes remain outside
    // ReadLine. Without the pre-store barrier, word 0 is already overwritten.
    const bool untouched{first_barrier_seen && scratch == original};
    if (first_barrier_seen)
        warp.ReleaseReaders();
    else
        warp.Abort();
    for (auto &thread : threads)
        if (thread.joinable())
            thread.join();

    // Never assert before releasing and joining the worker threads.
    BOOST_CHECK(all_threads_started);
    BOOST_CHECK(first_barrier_seen);
    BOOST_CHECK(untouched);
    BOOST_CHECK(!warp.Aborted());
    BOOST_CHECK_EQUAL_COLLECTIONS(scratch.begin(), scratch.end(), expected.begin(), expected.end());
}
BOOST_AUTO_TEST_SUITE_END()
