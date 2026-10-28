// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <array>
#include <barrier>
#include <cstdint>
#include <crypto/mercahash.h>
#include <latch>
#include <thread>
#include <vector>

namespace portable_v2 {
#include <test/util/mercahash_gpu_v2.h>
}

// Execute the actual OpenCL source as C++ with explicit workgroup barriers.
// This is host synchronization/arithmetic evidence, never AMD device execution.
namespace amd_emulation {
using ulong = unsigned long long;
thread_local unsigned group_id{0}, lane_id{0}, group_size{8};
thread_local std::barrier<>* group_barrier{nullptr};
unsigned get_group_id(unsigned) { return group_id; }
unsigned get_local_id(unsigned) { return lane_id; }
unsigned get_local_size(unsigned) { return group_size; }
constexpr unsigned CLK_GLOBAL_MEM_FENCE{1}, CLK_LOCAL_MEM_FENCE{2};
void barrier(unsigned) { group_barrier->arrive_and_wait(); }
#undef BITCOIN_MINING_MERCAHASH_GPU_H
#define MH_OPENCL
#define __global
#define __constant constexpr
#define __kernel inline
#define __local static
#include <mining/mercahash_opencl_v2.h>
#undef __local
#undef __kernel
#undef __constant
#undef __global
#undef MH_OPENCL
#undef MH_AMD_SPECIALIZED
#undef MH_VERSION

template <typename F> void Group(unsigned size, unsigned slot, F function)
{
    std::barrier sync{static_cast<std::ptrdiff_t>(size)};
    std::latch start{1};
    bool launch{false};
    std::vector<std::thread> threads;
    auto join = [&] { for (auto& thread : threads) if (thread.joinable()) thread.join(); };
    try {
        for (unsigned lane{0}; lane < size; ++lane) threads.emplace_back([&, lane] {
            start.wait();
            if (!launch) return;
            group_id = slot; lane_id = lane; group_size = size; group_barrier = &sync;
            function();
        });
    } catch (...) { start.count_down(); join(); throw; }
    launch = true; start.count_down(); join();
}
}

BOOST_AUTO_TEST_SUITE(mercahash_opencl_tests)
BOOST_AUTO_TEST_CASE(binding_matches_serial_at_all_supported_group_sizes)
{
    constexpr unsigned contexts{2};
    constexpr unsigned long long guard{0xfedcba9876543210ULL};
    std::vector<unsigned long long> memory(contexts * mercahash::V2_SCRATCHPAD_BYTES / 8 + 2);
    for (size_t i{0}; i < memory.size(); ++i) memory[i] = i * 0x9e3779b97f4a7c15ULL;
    memory.front() = memory.back() = guard;
    std::array<unsigned char, contexts * 80> headers{};
    std::array<unsigned char, contexts * 32> outputs{};
    for (unsigned lanes : {8U, 16U, 32U, 64U}) {
        auto actual{memory}, expected{memory};
        std::array<unsigned long long, contexts * 16> states{}, reference{};
        for (unsigned slot{0}; slot < contexts; ++slot) {
            portable_v2::mh_test_id = slot;
            portable_v2::mh_init(headers.data(), reference.data(), contexts);
            amd_emulation::group_id = slot; amd_emulation::lane_id = 0;
            amd_emulation::mh_v2_init(headers.data(), states.data(), contexts);
            for (unsigned begin : {0U, 14U, 31U, 63U, 4094U}) {
                portable_v2::BindScratchpad(reference.data() + slot * 16, expected.data() + 1, begin, begin + 2, contexts, slot);
                amd_emulation::Group(lanes, slot, [&] {
                    amd_emulation::mh_v2_bind(headers.data(), states.data(), actual.data() + 1, outputs.data(), contexts, begin, begin + 2);
                });
                BOOST_CHECK(states == reference);
                BOOST_CHECK(actual == expected);
                BOOST_CHECK_EQUAL(actual.front(), guard);
                BOOST_CHECK_EQUAL(actual.back(), guard);
            }
        }
    }
}
BOOST_AUTO_TEST_CASE(specialized_stages_match_cpu_and_frozen_vectors_with_scratch_reuse)
{
    constexpr unsigned contexts{2};
    constexpr unsigned long long guard{0xfedcba9876543210ULL};
    std::vector<unsigned long long> scratch(contexts * mercahash::V2_SCRATCHPAD_BYTES / 8 + 2, guard);
    std::vector<unsigned char> cpu_scratch(mercahash::V2_SCRATCHPAD_BYTES);
    std::array<unsigned char, contexts * 80> headers{};
    std::array<unsigned char, contexts * 32> outputs{};
    std::array<unsigned long long, contexts * 16> states{}, reference_states{};
    auto reference_scratch{scratch};
    // Includes zero, ascending, all-ones, nonzero nonces, repeated headers,
    // different resident slots, full and partial batches without clearing memory.
    for (unsigned pattern : {0U, 1U, 2U, 3U, 4U, 5U, 6U, 0U}) {
        const unsigned count{pattern % 2 ? 1U : contexts}, slot{count - 1};
        for (unsigned i{0}; i < 80; ++i) headers[slot * 80 + i] = pattern == 0 ? 0 : pattern == 1 ? i : pattern == 2 ? 255 : (i * 37 + 11) & 255;
        if (pattern >= 4) {
            headers[slot * 80 + 76] = pattern - 3;
            headers[slot * 80 + 77] = headers[slot * 80 + 78] = headers[slot * 80 + 79] = 0;
        }
        portable_v2::mh_test_id = slot;
        portable_v2::mh_init(headers.data(), reference_states.data(), count);
        amd_emulation::group_id = slot; amd_emulation::lane_id = 0;
        amd_emulation::mh_v2_init(headers.data(), states.data(), count);
        BOOST_CHECK(states == reference_states);
        for (unsigned begin{0}; begin < 262144; begin += 7001) {
            const auto end{std::min(begin + 7001, 262144U)};
            portable_v2::mh_stage(headers.data(), reference_states.data(), reference_scratch.data() + 1, outputs.data(), count, 0, begin, end);
            amd_emulation::mh_v2_fill(headers.data(), states.data(), scratch.data() + 1, outputs.data(), count, begin, end);
        }
        BOOST_CHECK(states == reference_states);
        BOOST_CHECK(scratch == reference_scratch);
        amd_emulation::Group(8, slot, [&] {
            // Deliberately split at an irregular non-ARX boundary.
            amd_emulation::mh_v2_bind(headers.data(), states.data(), scratch.data() + 1, outputs.data(), count, 0, 31);
            amd_emulation::barrier(3); // Emulated stream boundary publishes state.
            amd_emulation::mh_v2_bind(headers.data(), states.data(), scratch.data() + 1, outputs.data(), count, 31, 4096);
        });
        portable_v2::BindScratchpad(reference_states.data() + slot * 16, reference_scratch.data() + 1, 0, 31, count, slot);
        portable_v2::BindScratchpad(reference_states.data() + slot * 16, reference_scratch.data() + 1, 31, 4096, count, slot);
        BOOST_CHECK(states == reference_states);
        BOOST_CHECK(scratch == reference_scratch);
        amd_emulation::group_id = slot; amd_emulation::lane_id = 0;
        for (unsigned begin{1}; begin <= 16384; begin += 997) {
            const auto end{std::min(begin + 996, 16384U)};
            portable_v2::mh_stage(headers.data(), reference_states.data(), reference_scratch.data() + 1, outputs.data(), count, 2, begin, end);
            amd_emulation::mh_v2_mix(headers.data(), states.data(), scratch.data() + 1, outputs.data(), count, begin, end);
        }
        BOOST_CHECK(states == reference_states);
        BOOST_CHECK(scratch == reference_scratch);
        portable_v2::mh_stage(headers.data(), reference_states.data(), reference_scratch.data() + 1, outputs.data(), count, 3, 0, 0);
        amd_emulation::mh_v2_final(headers.data(), states.data(), scratch.data() + 1, outputs.data(), count, 0, 0);
        BOOST_CHECK(states == reference_states);
        std::array<unsigned char, 32> expected{};
        mercahash::HashV2(std::span<const unsigned char>{headers.data() + slot * 80, 80}, cpu_scratch, expected);
        BOOST_CHECK_EQUAL_COLLECTIONS(outputs.begin() + slot * 32, outputs.begin() + (slot + 1) * 32, expected.begin(), expected.end());
        // Final sampling is read-only, so scratch guards and private slots persist.
        BOOST_CHECK_EQUAL(scratch.front(), guard);
        BOOST_CHECK_EQUAL(scratch.back(), guard);
        const auto unchanged{outputs};
        amd_emulation::group_id = count;
        amd_emulation::mh_v2_final(headers.data(), states.data(), scratch.data() + 1, outputs.data(), count, 0, 0);
        BOOST_CHECK(outputs == unchanged);
    }
}
BOOST_AUTO_TEST_SUITE_END()
