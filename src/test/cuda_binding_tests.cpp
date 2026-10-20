// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.

#include <boost/test/unit_test.hpp>
#include <array>
#include <barrier>
#include <cstdint>
#include <latch>
#include <thread>
#include <vector>

// Compile the unchanged portable baseline and the imported CUDA code in separate
// namespaces. This models warp ordering/shuffles on the host, not a GPU driver.
namespace serial_binding {
#include <mining/mercahash_gpu.h>
}
#undef MH_GLOBAL
#undef MH_CONSTANT
#undef MH_INLINE
#undef MH_KERNEL
#undef MH_ID
#undef MH_LANE
#undef MH_GROUP_SYNC

namespace cuda_binding {
constexpr unsigned LANES{32};
struct Dimension { unsigned x{0}; };
thread_local Dimension blockIdx{}, threadIdx{};
struct Warp {
    std::barrier<> barrier{LANES};
    std::array<unsigned, LANES> values{};
    unsigned Shuffle(unsigned lane, unsigned value, unsigned delta)
    {
        values[lane] = value;
        barrier.arrive_and_wait();
        const auto result{values[lane ^ delta]};
        barrier.arrive_and_wait(); // No lane may overwrite a still-needed value.
        return result;
    }
};
thread_local Warp* current_warp{nullptr};
void SyncWarp() { current_warp->barrier.arrive_and_wait(); }
unsigned ShuffleXor(unsigned mask, unsigned value, unsigned delta)
{
    // The optimized binding reduction always involves a complete warp.
    if (mask != 0xffffffffu) std::terminate();
    return current_warp->Shuffle(threadIdx.x, value, delta);
}
#define MH_CUDA
#define __device__
#define __constant__ constexpr
#define __forceinline__ inline
#define __global__ inline
#define __syncwarp SyncWarp
#define __shfl_xor_sync ShuffleXor
#define mh_init cuda_binding_test_init
#define mh_fill cuda_binding_test_fill
#define mh_bind cuda_binding_test_bind
#define mh_mix cuda_binding_test_mix
#define mh_final cuda_binding_test_final
#include <mining/mercahash_gpu_specialized.h>
#undef mh_final
#undef mh_mix
#undef mh_bind
#undef mh_fill
#undef mh_init
#undef __shfl_xor_sync
#undef __syncwarp
#undef __global__
#undef __forceinline__
#undef __constant__
#undef __device__
#undef MH_CUDA
}

#undef BITCOIN_MINING_MERCAHASH_GPU_H
#undef MH_VERSION
#define MH_VERSION 2
namespace serial_binding_v2 {
#include <mining/mercahash_gpu.h>
}
#undef BITCOIN_MINING_MERCAHASH_GPU_SPECIALIZED_H
#define MH_CUDA
#define __device__
#define __constant__ constexpr
#define __forceinline__ inline
#define __global__ inline
#define __syncwarp SyncWarp
#define __shfl_xor_sync ShuffleXor
#define mh_init cuda_binding_test_init_v2
#define mh_fill cuda_binding_test_fill_v2
#define mh_bind cuda_binding_test_bind_v2
#define mh_mix cuda_binding_test_mix_v2
#define mh_final cuda_binding_test_final_v2
namespace cuda_binding { namespace v2 {
#include <mining/mercahash_gpu_specialized.h>
} }
#undef mh_final
#undef mh_mix
#undef mh_bind
#undef mh_fill
#undef mh_init
#undef __shfl_xor_sync
#undef __syncwarp
#undef __global__
#undef __forceinline__
#undef __constant__
#undef __device__
#undef MH_CUDA
#undef MH_VERSION

BOOST_AUTO_TEST_SUITE(cuda_binding_tests)
void CheckBinding(bool v2)
{
    constexpr unsigned contexts{2};
    constexpr uint64_t guard{0xfedcba9876543210ULL};
    std::array<unsigned char, contexts * 80> headers{};
    for (size_t i{0}; i < headers.size(); ++i) headers[i] = (i * 37 + 11) & 255;
    std::array<unsigned long long, contexts * 16> actual_states{}, expected_states{};
    std::array<unsigned char, contexts * 32> outputs{};
    std::vector<unsigned long long> actual(contexts * size_t{v2 ? 262144U : 2097152U} * 8 + 2);
    for (size_t i{0}; i < actual.size(); ++i) actual[i] = i * 0x9e3779b97f4a7c15ULL;
    actual.front() = actual.back() = guard;
    auto expected{actual};

    // Exercise the single-lane fill and its persisted state across the exact
    // production 16,384-line boundary, in two private context slots.
    for (unsigned slot{0}; slot < contexts; ++slot) {
        serial_binding::mh_test_id = slot;
        serial_binding_v2::mh_test_id = slot;
        if (v2) serial_binding_v2::mh_init(headers.data(), expected_states.data(), contexts);
        else serial_binding::mh_init(headers.data(), expected_states.data(), contexts);
        cuda_binding::blockIdx.x = slot;
        cuda_binding::threadIdx.x = 0;
        if (v2) cuda_binding::v2::cuda_binding_test_init_v2(headers.data(), actual_states.data(), contexts);
        else cuda_binding::cuda_binding_test_init(headers.data(), actual_states.data(), contexts);
        for (unsigned begin : {0U, 16384U}) {
            if (v2) serial_binding_v2::mh_stage(headers.data(), expected_states.data(), expected.data() + 1,
                                     outputs.data(), contexts, 0, begin, begin + 16384);
            else serial_binding::mh_stage(headers.data(), expected_states.data(), expected.data() + 1,
                                     outputs.data(), contexts, 0, begin, begin + 16384);
            if (v2) cuda_binding::v2::cuda_binding_test_fill_v2(headers.data(), actual_states.data(), actual.data() + 1,
                                  outputs.data(), contexts, begin, begin + 16384);
            else cuda_binding::cuda_binding_test_fill(headers.data(), actual_states.data(), actual.data() + 1,
                                  outputs.data(), contexts, begin, begin + 16384);
        }
    }
    BOOST_CHECK(actual == expected);
    BOOST_CHECK(actual_states == expected_states);

    // Cover the first/last blocks, the 16-block ARX boundary, adjacent-block
    // publication, and the 64-block launch boundary. Untouched memory contains
    // distinct data so a wrong reference line or context offset is observable.
    const std::array<unsigned, 5> starts{0, 14, 31, 63, v2 ? 4094U : 32766U};
    for (unsigned slot{0}; slot < contexts; ++slot) {
        for (unsigned begin : starts) {
            if (v2) serial_binding_v2::BindScratchpad(expected_states.data() + slot * 16,
                                          expected.data() + 1, begin, begin + 2, contexts, slot);
            else serial_binding::BindScratchpad(expected_states.data() + slot * 16,
                                          expected.data() + 1, begin, begin + 2, contexts, slot);
        }
        cuda_binding::Warp warp;
        std::latch start{1};
        bool launch{false};
        std::vector<std::thread> threads;
        threads.reserve(cuda_binding::LANES);
        auto join_threads = [&] {
            for (auto& thread : threads) {
                if (thread.joinable()) thread.join();
            }
        };
        try {
            for (unsigned lane{0}; lane < cuda_binding::LANES; ++lane) {
                threads.emplace_back([&, lane] {
                    start.wait();
                    if (!launch) return;
                    cuda_binding::current_warp = &warp;
                    cuda_binding::threadIdx.x = lane;
                    cuda_binding::blockIdx.x = slot;
                    for (unsigned begin : starts) {
                        if (v2) cuda_binding::v2::cuda_binding_test_bind_v2(headers.data(), actual_states.data(), actual.data() + 1,
                                              outputs.data(), contexts, begin, begin + 2);
                        else cuda_binding::cuda_binding_test_bind(headers.data(), actual_states.data(), actual.data() + 1,
                                              outputs.data(), contexts, begin, begin + 2);
                        // A stream-ordered kernel boundary publishes leader state.
                        cuda_binding::SyncWarp();
                    }
                });
            }
        } catch (...) {
            start.count_down(); // Release any threads created before the failure.
            join_threads();
            throw;
        }
        launch = true;
        start.count_down();
        join_threads(); // Join before comparing any scratchpad or state words.
        BOOST_CHECK(actual_states == expected_states);
        BOOST_CHECK(actual == expected);
        BOOST_CHECK_EQUAL(actual.front(), guard);
        BOOST_CHECK_EQUAL(actual.back(), guard);
    }
}
BOOST_AUTO_TEST_CASE(imported_fill_and_warp_binding_match_serial_arithmetic) { CheckBinding(false); }
BOOST_AUTO_TEST_CASE(v2_fill_and_warp_binding_match_serial_arithmetic) { CheckBinding(true); }
BOOST_AUTO_TEST_SUITE_END()
