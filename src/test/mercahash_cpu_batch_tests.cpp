// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <boost/test/unit_test.hpp>
#include <mining/cpu_scratchpads.h>
#include <mining/mercahash_cpu_batch.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <memory_resource>
#include <vector>

BOOST_AUTO_TEST_SUITE(mercahash_cpu_batch_tests)

BOOST_AUTO_TEST_CASE(batch_widths_alignment_reuse_and_full_scratchpad_parity)
{
    mining::CpuScratchpads aligned;
    std::array<std::vector<unsigned char>, 4> storage;
    std::array<std::array<unsigned char, 80>, 4> headers{};
    std::array<std::array<unsigned char, 34>, 4> outputs;
    std::array<mercahash::cpu2::Job, 4> jobs;
    std::vector<unsigned char> reference(mercahash::V2_SCRATCHPAD_BYTES);
    for (size_t lane{0}; lane < 4; ++lane) {
        storage[lane].resize(mercahash::V2_SCRATCHPAD_BYTES + 2, 0xa5);
        for (size_t i{0}; i < 80; ++i) headers[lane][i] = (i * 37 + lane * 19) & 255;
        BOOST_CHECK_EQUAL(reinterpret_cast<uintptr_t>(aligned[lane].data()) % 32, 0U);
    }
    for (const bool unaligned : {false, true}) {
        for (size_t width{1}; width <= 4; ++width) {
            for (size_t lane{0}; lane < 4; ++lane) {
                outputs[lane].fill(0xc3);
                jobs[lane] = {headers[lane], unaligned ? std::span{storage[lane]}.subspan(1, mercahash::V2_SCRATCHPAD_BYTES) : aligned[lane], std::span{outputs[lane]}.subspan(1, 32)};
            }
            for (unsigned repeat{0}; repeat < 2; ++repeat) {
                BOOST_REQUIRE_EQUAL(mercahash::cpu2::Batch(std::span{jobs}.first(width)), width);
                for (size_t lane{0}; lane < width; ++lane) {
                    std::array<unsigned char, 32> expected;
                    mercahash::HashV2(headers[lane], reference, expected);
                    BOOST_CHECK(std::equal(expected.begin(), expected.end(), jobs[lane].output.begin()));
                    BOOST_CHECK(std::equal(reference.begin(), reference.end(), jobs[lane].scratch.begin()));
                    BOOST_CHECK_EQUAL(outputs[lane].front(), 0xc3);
                    BOOST_CHECK_EQUAL(outputs[lane].back(), 0xc3);
                    BOOST_CHECK_EQUAL(storage[lane].front(), 0xa5);
                    BOOST_CHECK_EQUAL(storage[lane].back(), 0xa5);
                }
                for (size_t lane{width}; lane < 4; ++lane)
                    BOOST_CHECK(std::all_of(outputs[lane].begin(), outputs[lane].end(), [](auto c) { return c == 0xc3; }));
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(invalid_buffers_overlap_and_preentry_cancellation)
{
    mining::CpuScratchpads scratch;
    std::array<unsigned char, 81> header{};
    std::array<unsigned char, 33> output{};
    mercahash::cpu2::Job good{std::span{header}.first(80), scratch[0], std::span{output}.first(32)};
    auto reject = [](auto jobs) { BOOST_CHECK_THROW(mercahash::cpu2::Batch(jobs), std::invalid_argument); };
    reject(std::span<mercahash::cpu2::Job>{});
    std::array<mercahash::cpu2::Job, 5> too_many; too_many.fill(good); reject(std::span{too_many});
    for (unsigned field{0}; field < 3; ++field) {
        auto bad{good};
        if (field == 0) bad.header = header;
        if (field == 1) bad.scratch = bad.scratch.first(bad.scratch.size() - 1);
        if (field == 2) bad.output = output;
        reject(std::span{&bad, 1});
    }
    std::array<mercahash::cpu2::Job, 2> overlap{good, good}; reject(std::span{overlap});
    auto bad{good}; bad.header = scratch[0].first(80); reject(std::span{&bad, 1});
    bad = good; bad.output = scratch[0].first(32); reject(std::span{&bad, 1});
    bad = good;
    // Valid sized header/output overlap is rejected before any reads.
    bad.header = std::span{header}.first(80); bad.output = std::span{header}.first(32); reject(std::span{&bad, 1});
    output.fill(0xc3);
    scratch[0][0] = 0xa5;
    std::atomic<bool> stop{true};
    BOOST_CHECK_EQUAL(mercahash::cpu2::Batch(std::span{&good, 1}, &stop), 0U);
    BOOST_CHECK_EQUAL(scratch[0][0], 0xa5);
    BOOST_CHECK(std::all_of(output.begin(), output.end(), [](auto c) { return c == 0xc3; }));
}

BOOST_AUTO_TEST_CASE(allocation_failure_and_advice_fallback_cleanup)
{
    struct Resource final : std::pmr::memory_resource {
        unsigned attempts{0}, live{0}, fail_at{0};
        void* do_allocate(size_t bytes, size_t alignment) override {
            if (++attempts == fail_at) throw std::bad_alloc{};
            auto* p{std::pmr::new_delete_resource()->allocate(bytes, alignment)};
            ++live; return p;
        }
        void do_deallocate(void* p, size_t bytes, size_t alignment) override {
            --live; std::pmr::new_delete_resource()->deallocate(p, bytes, alignment);
        }
        bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
    } resource;
    for (unsigned fail{1}; fail <= 4; ++fail) {
        resource.attempts = 0; resource.fail_at = fail;
        BOOST_CHECK_THROW(mining::CpuScratchpads{&resource}, std::bad_alloc);
        BOOST_CHECK_EQUAL(resource.live, 0U);
    }
    resource.fail_at = 0;
    // Failed page hints are ignored; ordinary pages hash normally.
    {
        mining::CpuScratchpads pads{&resource, [](void*, size_t) { return -1; }};
        std::array<unsigned char, 80> header{};
        std::array<unsigned char, 32> output{}, expected{};
        mercahash::cpu2::Job job{header, pads[0], output};
        BOOST_REQUIRE_EQUAL(mercahash::cpu2::Batch(std::span{&job, 1}), 1U);
        mercahash::HashV2(header, pads[1], expected);
        BOOST_CHECK(output == expected);
        BOOST_CHECK_EQUAL(resource.live, 4U);
    }
    BOOST_CHECK_EQUAL(resource.live, 0U);
}
BOOST_AUTO_TEST_SUITE_END()
