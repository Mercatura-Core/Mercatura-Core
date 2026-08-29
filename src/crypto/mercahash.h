// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef BITCOIN_CRYPTO_MERCAHASH_H
#define BITCOIN_CRYPTO_MERCAHASH_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace mercahash {

static constexpr std::size_t HEADER_SIZE = 80;
static constexpr std::size_t OUTPUT_SIZE = 32;

static constexpr std::size_t SCRATCHPAD_BYTES = 128ULL * 1024 * 1024;
static constexpr std::size_t LINE_BYTES = 64;
static constexpr std::size_t LINE_WORDS = 8;
static constexpr std::size_t LINE_COUNT = SCRATCHPAD_BYTES / LINE_BYTES;
static constexpr std::uint64_t LINE_MASK = LINE_COUNT - 1;

static constexpr std::uint32_t SECONDARY_INTERVAL = 16;
static constexpr std::uint32_t CROSS_LANE_INTERVAL = 64;
static constexpr std::uint32_t CHECKPOINT_INTERVAL = 1024;
static constexpr std::uint32_t FINAL_READS = 256;

static constexpr std::size_t BIND_BLOCK_BYTES = 4096;
static constexpr std::size_t BIND_BLOCK_LINES = BIND_BLOCK_BYTES / LINE_BYTES;
static constexpr std::size_t BIND_BLOCK_WORDS =
    BIND_BLOCK_BYTES / sizeof(std::uint64_t);
static constexpr std::size_t BIND_BLOCK_COUNT =
    SCRATCHPAD_BYTES / BIND_BLOCK_BYTES;
static constexpr std::uint64_t BIND_BLOCK_MASK = BIND_BLOCK_COUNT - 1;
static constexpr std::size_t BIND_ARX_INTERVAL = 16;

static_assert(LINE_BYTES == LINE_WORDS * sizeof(std::uint64_t));
static_assert((LINE_COUNT & (LINE_COUNT - 1)) == 0);
static_assert(LINE_COUNT == 2'097'152);
static_assert(LINE_MASK == 0x1FFFFF);
static_assert(CROSS_LANE_INTERVAL % SECONDARY_INTERVAL == 0);
static_assert(CHECKPOINT_INTERVAL % CROSS_LANE_INTERVAL == 0);

static_assert(BIND_BLOCK_BYTES % LINE_BYTES == 0);
static_assert(BIND_BLOCK_LINES == 64);
static_assert(BIND_BLOCK_WORDS == 512);
static_assert(BIND_BLOCK_COUNT == 32'768);
static_assert((BIND_BLOCK_COUNT & (BIND_BLOCK_COUNT - 1)) == 0);
static_assert(BIND_BLOCK_MASK == 0x7FFF);
static_assert(BIND_BLOCK_COUNT % BIND_ARX_INTERVAL == 0);

static constexpr std::size_t DIAGNOSTIC_REGIONS = 64;
static constexpr std::size_t LINE_INDEX_BITS = 21;

static_assert(LINE_COUNT % DIAGNOSTIC_REGIONS == 0);

struct Diagnostics {
    std::uint64_t primary_accesses{0};
    std::uint64_t primary_unique{0};
    std::uint64_t secondary_accesses{0};
    std::uint64_t secondary_unique{0};
    std::uint64_t combined_unique{0};
    std::uint64_t forced_secondary_distinct{0};

    std::uint64_t binding_reference_accesses{0};
    std::uint64_t binding_reference_unique{0};
    std::uint64_t binding_reference_distance_sum{0};
    std::uint64_t binding_reference_distance_min{0};
    std::uint64_t binding_reference_distance_max{0};

    std::array<std::uint64_t, DIAGNOSTIC_REGIONS>
        binding_reference_relative_regions{};

    // Diagnostic-only bookkeeping used to count unique binding references.
    std::array<std::uint8_t, BIND_BLOCK_COUNT>
        binding_reference_seen{};

    std::array<std::uint64_t, DIAGNOSTIC_REGIONS> primary_regions{};
    std::array<std::uint64_t, DIAGNOSTIC_REGIONS> secondary_regions{};

    std::array<std::uint64_t, LINE_INDEX_BITS> primary_bit_ones{};
    std::array<std::uint64_t, LINE_INDEX_BITS> secondary_bit_ones{};
};

enum class ScratchpadFillMode : std::uint8_t {
    ALTERNATING_HALF_ROUND,
    TWO_FULL_ROUNDS,
};

/**
 * Frozen MercaHash-v1 consensus workload.
 *
 * Production proof-of-work code must use HashV1() rather than supplying
 * benchmark-time WorkParams directly.
 */
static constexpr std::uint32_t V1_MIX_STEPS = 131'072;
static constexpr ScratchpadFillMode V1_SCRATCHPAD_FILL =
    ScratchpadFillMode::ALTERNATING_HALF_ROUND;
static constexpr std::uint32_t V1_BINDING_PASSES = 1;

/**
 * Benchmark-time MercaHash work parameters.
 *
 * These values remain available for benchmark and development comparisons.
 * They allow alternate workloads to be benchmarked without changing the
 * fixed production HashV1() workload.
 */
struct WorkParams {
    std::uint32_t mix_steps;
    ScratchpadFillMode scratchpad_fill;

    /**
     * Benchmark-time memory-binding passes.
     *
     * Zero preserves the unbound development baseline. Alternate values of
     * one or more remain development-only comparison settings.
     */
    std::uint32_t binding_passes{0};
};

/**
 * Compute MercaHash over exactly one canonical 80-byte block header.
 *
 * scratchpad must provide exactly SCRATCHPAD_BYTES writable bytes.
 * The caller owns and may reuse the scratchpad between hash calls.
 *
 * Allocation is intentionally outside this function so benchmark timing
 * measures MercaHash itself rather than 128 MiB allocator overhead.
 */
void Hash(
    std::span<const unsigned char> header,
    std::span<unsigned char> scratchpad,
    std::span<unsigned char> output,
    const WorkParams& params);

/**
 * Compute the frozen MercaHash-v1 proof-of-work function.
 *
 * This is the production entry point. Its workload parameters are fixed
 * internally and must not be supplied by consensus callers.
 */
void HashV1(
    std::span<const unsigned char> header,
    std::span<unsigned char> scratchpad,
    std::span<unsigned char> output);

/**
 * Benchmark/development-only diagnostic execution.
 *
 * Produces the same MercaHash output as Hash(), while recording scratchpad
 * addressing statistics. This is not intended for consensus validation.
 */
void HashWithDiagnostics(
    std::span<const unsigned char> header,
    std::span<unsigned char> scratchpad,
    std::span<unsigned char> output,
    const WorkParams& params,
    Diagnostics& diagnostics);

} // namespace mercahash

#endif // BITCOIN_CRYPTO_MERCAHASH_H
