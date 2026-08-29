// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <crypto/mercahash.h>

#include <crypto/common.h>
#include <crypto/sha3.h>
#include <span.h>

#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <string_view>
#include <vector>

namespace mercahash {
namespace {

constexpr std::string_view STATE_DOMAIN_0{"MercaHash-v1/state/0"};
constexpr std::string_view STATE_DOMAIN_1{"MercaHash-v1/state/1"};
constexpr std::string_view CHECKPOINT_DOMAIN{"MercaHash-v1/checkpoint"};
constexpr std::string_view FINAL_DOMAIN{"MercaHash-v1/final"};

constexpr std::array<std::uint64_t, 8> CONSTANTS{
    0x9E3779B97F4A7C15ULL,
    0xBF58476D1CE4E5B9ULL,
    0x94D049BB133111EBULL,
    0xD6E8FEB86659FD93ULL,
    0xA0761D6478BD642FULL,
    0xE7037ED1A0B428DBULL,
    0x8EBC6AF09C88C6E3ULL,
    0x589965CC75374CC3ULL,
};

constexpr std::array<int, 8> ROTATION_1{
    13, 29, 43, 57, 17, 31, 47, 59,
};

constexpr std::array<int, 8> ROTATION_2{
    37, 53, 11, 23, 41, 7, 19, 61,
};

constexpr std::array<int, 8> CHECKPOINT_ROTATION{
    7, 19, 31, 43, 11, 23, 37, 53,
};

using State = std::array<std::uint64_t, 16>;

std::span<const unsigned char> AsBytes(std::string_view str)
{
    return {
        reinterpret_cast<const unsigned char*>(str.data()),
        str.size(),
    };
}

State InitialState(std::span<const unsigned char> header)
{
    assert(header.size() == HEADER_SIZE);

    std::array<unsigned char, SHA3_512::OUTPUT_SIZE> digest0{};
    std::array<unsigned char, SHA3_512::OUTPUT_SIZE> digest1{};

    SHA3_512{}
        .Write(AsBytes(STATE_DOMAIN_0))
        .Write(header)
        .Finalize(digest0);

    SHA3_512{}
        .Write(AsBytes(STATE_DOMAIN_1))
        .Write(header)
        .Finalize(digest1);

    State state{};

    for (std::size_t i = 0; i < 8; ++i) {
        state[i] = ReadLE64(digest0.data() + i * sizeof(std::uint64_t));
        state[i + 8] = ReadLE64(digest1.data() + i * sizeof(std::uint64_t));
    }

    return state;
}

inline void G(
    std::uint64_t& a,
    std::uint64_t& b,
    std::uint64_t& c,
    std::uint64_t& d)
{
    a += b;
    d = std::rotl(d ^ a, 32);

    c += d;
    b = std::rotl(b ^ c, 24);

    a += b;
    d = std::rotl(d ^ a, 16);

    c += d;
    b = std::rotl(b ^ c, 63);
}

inline void MercaARXColumns(State& state)
{
    G(state[0], state[4], state[8],  state[12]);
    G(state[1], state[5], state[9],  state[13]);
    G(state[2], state[6], state[10], state[14]);
    G(state[3], state[7], state[11], state[15]);
}

inline void MercaARXDiagonals(State& state)
{
    G(state[0], state[5], state[10], state[15]);
    G(state[1], state[6], state[11], state[12]);
    G(state[2], state[7], state[8],  state[13]);
    G(state[3], state[4], state[9],  state[14]);
}

inline void MercaARXRound(State& state)
{
    MercaARXColumns(state);
    MercaARXDiagonals(state);
}

using Line = std::array<std::uint64_t, LINE_WORDS>;

void WriteLine(
    std::span<unsigned char> scratchpad,
    std::size_t line_index,
    const Line& line)
{
    assert(line_index < LINE_COUNT);

    const std::size_t offset = line_index * LINE_BYTES;

    for (std::size_t j = 0; j < LINE_WORDS; ++j) {
        WriteLE64(
            scratchpad.data() + offset + j * sizeof(std::uint64_t),
            line[j]);
    }
}

void FillScratchpad(
    State& state,
    std::span<unsigned char> scratchpad,
    ScratchpadFillMode mode)
{
    assert(scratchpad.size() == SCRATCHPAD_BYTES);

    // Domain-separated transition from initial state into scratchpad fill.
    state[0]  ^= 0x9E3779B97F4A7C15ULL;
    state[5]  ^= 0xBF58476D1CE4E5B9ULL;
    state[10] ^= 0x94D049BB133111EBULL;
    state[15] ^= 0xD6E8FEB86659FD93ULL;

    for (std::size_t i = 0; i < LINE_COUNT; ++i) {
        const std::uint64_t x = static_cast<std::uint64_t>(i);

        state[0] += x;
        state[7] ^= std::rotl(x, 17);
        state[8] += x ^ 0x9E3779B97F4A7C15ULL;
        state[15] ^= std::rotl(x, 41);

        switch (mode) {
        case ScratchpadFillMode::ALTERNATING_HALF_ROUND:
            if ((i & 1) == 0) {
                MercaARXColumns(state);
            } else {
                MercaARXDiagonals(state);
            }
            break;

        case ScratchpadFillMode::TWO_FULL_ROUNDS:
            MercaARXRound(state);
            MercaARXRound(state);
            break;
        }

        Line line{};

        for (std::size_t j = 0; j < LINE_WORDS; ++j) {
            line[j] = state[j] ^ state[j + 8];
        }

        WriteLine(scratchpad, i, line);

        // Feed the newly generated line back into the evolving state.
        state[0]  += line[3];
        state[1]  ^= std::rotl(line[4], 11);
        state[2]  += line[5];
        state[3]  ^= std::rotl(line[6], 23);

        state[4]  += line[7];
        state[5]  ^= std::rotl(line[0], 37);
        state[6]  += line[1];
        state[7]  ^= std::rotl(line[2], 53);

        state[8]  ^= line[5];
        state[9]  += std::rotl(line[6], 7);
        state[10] ^= line[7];
        state[11] += std::rotl(line[0], 19);

        state[12] ^= line[1];
        state[13] += std::rotl(line[2], 31);
        state[14] ^= line[3];
        state[15] += std::rotl(line[4], 47);
    }
}

Line ReadLine(
    std::span<const unsigned char> scratchpad,
    std::size_t line_index)
{
    assert(line_index < LINE_COUNT);

    const std::size_t offset = line_index * LINE_BYTES;
    Line line{};

    for (std::size_t j = 0; j < LINE_WORDS; ++j) {
        line[j] = ReadLE64(
            scratchpad.data() + offset + j * sizeof(std::uint64_t));
    }

    return line;
}

void BindScratchpad(
    State& state,
    std::span<unsigned char> scratchpad,
    std::uint32_t passes,
    Diagnostics* diagnostics)
{
    assert(scratchpad.size() == SCRATCHPAD_BYTES);

    for (std::uint64_t pass = 0; pass < passes; ++pass) {
        state[0] ^= pass + CONSTANTS[4];
        state[8] += (pass + 1) * CONSTANTS[5];
        MercaARXRound(state);

        for (std::size_t block = 0; block < BIND_BLOCK_COUNT; ++block) {
            const std::size_t base_line =
                block * BIND_BLOCK_LINES;

            const Line head =
                ReadLine(scratchpad, base_line);

            const std::uint64_t block_u64 =
                static_cast<std::uint64_t>(block);

            const std::uint64_t reference_seed =
                state[0] ^
                std::rotl(state[7], 19) ^
                state[13] ^
                head[0] ^
                std::rotl(head[7], 31) ^
                (block_u64 * CONSTANTS[1]) ^
                ((pass + 1) * CONSTANTS[6]);

            const std::size_t previous_block =
                block == 0
                    ? BIND_BLOCK_COUNT - 1
                    : block - 1;

            const std::size_t reference_block =
                block == 0
                    ? BIND_BLOCK_COUNT - 1
                    : static_cast<std::size_t>(
                          reference_seed % block_u64);

            if (block != 0) {
                assert(reference_block < block);

                if (diagnostics != nullptr) {
                    const std::uint64_t distance =
                        block_u64 -
                        static_cast<std::uint64_t>(
                            reference_block);

                    if (diagnostics->binding_reference_accesses == 0 ||
                        distance <
                            diagnostics->
                                binding_reference_distance_min) {
                        diagnostics->
                            binding_reference_distance_min =
                                distance;
                    }

                    if (distance >
                        diagnostics->
                            binding_reference_distance_max) {
                        diagnostics->
                            binding_reference_distance_max =
                                distance;
                    }

                    diagnostics->
                        binding_reference_distance_sum +=
                            distance;

                    const std::size_t relative_region =
                        static_cast<std::size_t>(
                            (static_cast<std::uint64_t>(
                                 reference_block) *
                             DIAGNOSTIC_REGIONS) /
                            block_u64);

                    assert(
                        relative_region <
                        DIAGNOSTIC_REGIONS);

                    ++diagnostics->
                        binding_reference_relative_regions[
                            relative_region];

                    std::uint8_t& seen =
                        diagnostics->
                            binding_reference_seen[
                                reference_block];

                    if (seen == 0) {
                        seen = 1;
                        ++diagnostics->
                            binding_reference_unique;
                    }

                    ++diagnostics->
                        binding_reference_accesses;
                }
            }

            const std::size_t reference_rotation =
                static_cast<std::size_t>(
                    (reference_seed >> 17) &
                    (BIND_BLOCK_LINES - 1));

            Line block_accumulator{};

            for (std::size_t line = 0;
                 line < BIND_BLOCK_LINES;
                 ++line) {
                const std::size_t current_line =
                    base_line + line;

                const std::size_t previous_line =
                    previous_block * BIND_BLOCK_LINES +
                    ((line + BIND_BLOCK_LINES - 1) &
                     (BIND_BLOCK_LINES - 1));

                const std::size_t reference_line =
                    reference_block * BIND_BLOCK_LINES +
                    ((line + reference_rotation) &
                     (BIND_BLOCK_LINES - 1));

                const Line current =
                    ReadLine(scratchpad, current_line);

                const Line previous =
                    ReadLine(scratchpad, previous_line);

                const Line reference =
                    ReadLine(scratchpad, reference_line);

                const std::uint64_t line_tag =
                    block_u64 ^
                    (static_cast<std::uint64_t>(line) *
                     CONSTANTS[0]) ^
                    (pass * CONSTANTS[3]);

                Line bound{};

                for (std::size_t j = 0; j < LINE_WORDS; ++j) {
                    const std::uint64_t x =
                        current[j] +
                        previous[(j + 1) & 7];

                    const std::uint64_t y =
                        reference[(j + 3) & 7] +
                        state[(j + line) & 15];

                    bound[j] =
                        x ^
                        std::rotl(y, ROTATION_1[j]) ^
                        std::rotl(
                            line_tag,
                            ROTATION_2[j]);
                }

                WriteLine(scratchpad, current_line, bound);

                for (std::size_t j = 0; j < LINE_WORDS; ++j) {
                    block_accumulator[j] ^=
                        bound[j] +
                        std::rotl(
                            bound[(j + 1) & 7],
                            ROTATION_2[j]);
                }
            }

            for (std::size_t j = 0; j < LINE_WORDS; ++j) {
                state[j] ^=
                    block_accumulator[j] +
                    reference_seed;

                state[j + 8] +=
                    std::rotl(
                        block_accumulator[(j + 3) & 7],
                        ROTATION_1[j]) ^
                    block_u64;
            }

            if ((block + 1) % BIND_ARX_INTERVAL == 0) {
                MercaARXRound(state);
            }
        }

        MercaARXRound(state);
    }
}

struct PrimaryResult {
    std::size_t line_index;
    Line written;
};

PrimaryResult PrimaryStep(
    State& state,
    std::span<unsigned char> scratchpad,
    std::uint64_t step)
{
    const std::uint64_t primary_seed =
        state[0] ^
        std::rotl(state[5], 17) ^
        state[10] ^
        std::rotl(state[15], 41) ^
        (step * CONSTANTS[0]);

    const std::size_t primary_index =
        static_cast<std::size_t>(primary_seed & LINE_MASK);

    const Line memory = ReadLine(scratchpad, primary_index);

    for (std::size_t j = 0; j < LINE_WORDS; ++j) {
        const std::uint64_t x =
            memory[j] ^
            std::rotl(state[j + 8], ROTATION_1[j]) ^
            (step * CONSTANTS[j]);

        state[j] += x;
    }

    for (std::size_t j = 0; j < LINE_WORDS; ++j) {
        const std::uint64_t y =
            state[j] +
            memory[(j + 3) & 7] +
            std::rotl(state[(j + 5) & 7], ROTATION_2[j]);

        state[j + 8] =
            std::rotl(
                state[j + 8] ^ y,
                ROTATION_1[(j + 3) & 7]);
    }

    Line written{};

    for (std::size_t j = 0; j < LINE_WORDS; ++j) {
        written[j] =
            memory[j] ^
            state[j] ^
            std::rotl(state[j + 8], ROTATION_2[j]) ^
            (step * CONSTANTS[(j + 1) & 7]);
    }

    WriteLine(scratchpad, primary_index, written);

    state[0]  ^= written[5];
    state[1]  += std::rotl(written[6], 9);
    state[2]  ^= written[7];
    state[3]  += std::rotl(written[0], 21);

    state[4]  ^= written[1];
    state[5]  += std::rotl(written[2], 33);
    state[6]  ^= written[3];
    state[7]  += std::rotl(written[4], 45);

    state[8]  += written[3];
    state[9]  ^= std::rotl(written[4], 15);
    state[10] += written[5];
    state[11] ^= std::rotl(written[6], 27);

    state[12] += written[7];
    state[13] ^= std::rotl(written[0], 39);
    state[14] += written[1];
    state[15] ^= std::rotl(written[2], 51);

    return {primary_index, written};
}

struct SecondaryResult {
    std::size_t line_index;
    Line written;
    bool forced_distinct;
};

SecondaryResult SecondaryStep(
    State& state,
    std::span<unsigned char> scratchpad,
    std::uint64_t step,
    const PrimaryResult& primary)
{
    const std::uint64_t secondary_seed =
        state[2] ^
        std::rotl(state[7], 23) ^
        state[9] ^
        std::rotl(state[14], 37) ^
        primary.written[step & 7] ^
        (step * CONSTANTS[3]);

    std::size_t secondary_index =
        static_cast<std::size_t>(secondary_seed & LINE_MASK);

    const bool forced_distinct =
        secondary_index == primary.line_index;

    if (forced_distinct) {
        secondary_index ^= 1U;
    }

    const Line memory = ReadLine(scratchpad, secondary_index);

    for (std::size_t j = 0; j < LINE_WORDS; ++j) {
        const std::uint64_t z =
            memory[j] +
            state[(j + 11) & 15] +
            (step ^ CONSTANTS[(j + 5) & 7]);

        state[j + 8] += std::rotl(z, ROTATION_2[j]);
    }

    for (std::size_t j = 0; j < LINE_WORDS; ++j) {
        state[j] =
            std::rotl(
                state[j] ^
                memory[(j + 5) & 7] ^
                state[j + 8],
                ROTATION_1[j]);
    }

    Line written{};

    for (std::size_t j = 0; j < LINE_WORDS; ++j) {
        written[j] =
            memory[j] +
            state[j] +
            std::rotl(state[j + 8], ROTATION_1[(j + 2) & 7]) +
            (step * CONSTANTS[(j + 6) & 7]);
    }

    WriteLine(scratchpad, secondary_index, written);

    state[0]  += written[7];
    state[1]  ^= std::rotl(written[0], 13);
    state[2]  += written[1];
    state[3]  ^= std::rotl(written[2], 25);

    state[4]  += written[3];
    state[5]  ^= std::rotl(written[4], 35);
    state[6]  += written[5];
    state[7]  ^= std::rotl(written[6], 49);

    state[8]  ^= written[2];
    state[9]  += std::rotl(written[3], 17);
    state[10] ^= written[4];
    state[11] += std::rotl(written[5], 29);

    state[12] ^= written[6];
    state[13] += std::rotl(written[7], 43);
    state[14] ^= written[0];
    state[15] += std::rotl(written[1], 55);

    return {secondary_index, written, forced_distinct};
}

void CrossLaneMix(State& state, std::uint64_t step)
{
    state[0] ^= step;
    state[8] += step * CONSTANTS[2];

    MercaARXRound(state);
    MercaARXRound(state);
}

void WriteHashUint64(SHA3_512& hasher, std::uint64_t value)
{
    std::array<unsigned char, sizeof(std::uint64_t)> encoded{};
    WriteLE64(encoded.data(), value);
    hasher.Write(encoded);
}

void Checkpoint(
    State& state,
    std::uint64_t step,
    const PrimaryResult& primary,
    const SecondaryResult& secondary)
{
    SHA3_512 hasher;

    hasher.Write(AsBytes(CHECKPOINT_DOMAIN));

    WriteHashUint64(hasher, step);
    WriteHashUint64(hasher, primary.line_index);
    WriteHashUint64(hasher, secondary.line_index);

    for (const std::uint64_t value : state) {
        WriteHashUint64(hasher, value);
    }

    for (const std::uint64_t value : primary.written) {
        WriteHashUint64(hasher, value);
    }

    for (const std::uint64_t value : secondary.written) {
        WriteHashUint64(hasher, value);
    }

    std::array<unsigned char, SHA3_512::OUTPUT_SIZE> digest{};
    hasher.Finalize(digest);

    for (std::size_t j = 0; j < LINE_WORDS; ++j) {
        const std::uint64_t value =
            ReadLE64(digest.data() + j * sizeof(std::uint64_t));

        state[j] ^= value;
        state[j + 8] +=
            std::rotl(
                ReadLE64(
                    digest.data() +
                    ((j + 3) & 7) * sizeof(std::uint64_t)),
                CHECKPOINT_ROTATION[j]);
    }

    MercaARXRound(state);
}

void MixScratchpad(
    State& state,
    std::span<unsigned char> scratchpad,
    std::uint32_t mix_steps)
{
    for (std::uint64_t step = 1; step <= mix_steps; ++step) {
        const PrimaryResult primary =
            PrimaryStep(state, scratchpad, step);

        if (step % SECONDARY_INTERVAL == 0) {
            const SecondaryResult secondary =
                SecondaryStep(state, scratchpad, step, primary);

            if (step % CROSS_LANE_INTERVAL == 0) {
                CrossLaneMix(state, step);
            }

            if (step % CHECKPOINT_INTERVAL == 0) {
                Checkpoint(state, step, primary, secondary);
            }
        }
    }
}

void RecordDiagnosticIndex(
    std::size_t line_index,
    bool primary,
    Diagnostics& diagnostics,
    std::vector<bool>& seen,
    std::vector<bool>& seen_combined)
{
    assert(line_index < LINE_COUNT);

    if (!seen[line_index]) {
        seen[line_index] = true;

        if (primary) {
            ++diagnostics.primary_unique;
        } else {
            ++diagnostics.secondary_unique;
        }
    }

    if (!seen_combined[line_index]) {
        seen_combined[line_index] = true;
        ++diagnostics.combined_unique;
    }

    const std::size_t region =
        line_index / (LINE_COUNT / DIAGNOSTIC_REGIONS);

    auto& regions =
        primary ? diagnostics.primary_regions
                : diagnostics.secondary_regions;

    ++regions[region];

    auto& bit_ones =
        primary ? diagnostics.primary_bit_ones
                : diagnostics.secondary_bit_ones;

    for (std::size_t bit = 0; bit < LINE_INDEX_BITS; ++bit) {
        if ((line_index >> bit) & 1U) {
            ++bit_ones[bit];
        }
    }
}

void MixScratchpadDiagnostics(
    State& state,
    std::span<unsigned char> scratchpad,
    std::uint32_t mix_steps,
    Diagnostics& diagnostics)
{
    std::vector<bool> seen_primary(LINE_COUNT, false);
    std::vector<bool> seen_secondary(LINE_COUNT, false);
    std::vector<bool> seen_combined(LINE_COUNT, false);

    for (std::uint64_t step = 1; step <= mix_steps; ++step) {
        const PrimaryResult primary =
            PrimaryStep(state, scratchpad, step);

        ++diagnostics.primary_accesses;
        RecordDiagnosticIndex(
            primary.line_index,
            true,
            diagnostics,
            seen_primary,
            seen_combined);

        if (step % SECONDARY_INTERVAL == 0) {
            const SecondaryResult secondary =
                SecondaryStep(state, scratchpad, step, primary);

            ++diagnostics.secondary_accesses;

            if (secondary.forced_distinct) {
                ++diagnostics.forced_secondary_distinct;
            }

            RecordDiagnosticIndex(
                secondary.line_index,
                false,
                diagnostics,
                seen_secondary,
                seen_combined);

            if (step % CROSS_LANE_INTERVAL == 0) {
                CrossLaneMix(state, step);
            }

            if (step % CHECKPOINT_INTERVAL == 0) {
                Checkpoint(state, step, primary, secondary);
            }
        }
    }
}

void FinalSample(
    State& state,
    std::span<const unsigned char> scratchpad)
{
    for (std::uint64_t sample = 0; sample < FINAL_READS; ++sample) {
        const std::uint64_t sample_seed =
            state[0] ^
            std::rotl(state[5], 17) ^
            state[10] ^
            std::rotl(state[15], 41) ^
            (sample * CONSTANTS[0]);

        const std::size_t sample_index =
            static_cast<std::size_t>(sample_seed & LINE_MASK);

        const Line memory = ReadLine(scratchpad, sample_index);

        for (std::size_t j = 0; j < LINE_WORDS; ++j) {
            const std::uint64_t x =
                memory[j] ^
                std::rotl(state[j + 8], ROTATION_1[j]) ^
                (sample + CONSTANTS[j]);

            state[j] += x;
        }

        for (std::size_t j = 0; j < LINE_WORDS; ++j) {
            state[j + 8] =
                std::rotl(
                    state[j + 8] ^
                    state[j] ^
                    memory[(j + 3) & 7],
                    ROTATION_2[j]);
        }

        state[0] ^= static_cast<std::uint64_t>(sample_index);
        state[8] +=
            std::rotl(
                static_cast<std::uint64_t>(sample_index),
                29);

        if ((sample + 1) % 16 == 0) {
            MercaARXRound(state);
        }
    }
}

void WriteHashUint64(SHA3_256& hasher, std::uint64_t value)
{
    std::array<unsigned char, sizeof(std::uint64_t)> encoded{};
    WriteLE64(encoded.data(), value);
    hasher.Write(encoded);
}

void FinalizeHash(
    const State& state,
    std::span<const unsigned char> header,
    std::span<unsigned char> output)
{
    SHA3_256 hasher;

    hasher.Write(AsBytes(FINAL_DOMAIN));
    hasher.Write(header);

    for (const std::uint64_t value : state) {
        WriteHashUint64(hasher, value);
    }

    hasher.Finalize(output);
}

} // namespace

void Hash(
    std::span<const unsigned char> header,
    std::span<unsigned char> scratchpad,
    std::span<unsigned char> output,
    const WorkParams& params)
{
    assert(header.size() == HEADER_SIZE);
    assert(scratchpad.size() == SCRATCHPAD_BYTES);
    assert(output.size() == OUTPUT_SIZE);
    assert(params.mix_steps > 0);

    State state = InitialState(header);

    FillScratchpad(state, scratchpad, params.scratchpad_fill);
    BindScratchpad(
        state,
        scratchpad,
        params.binding_passes,
        nullptr);
    MixScratchpad(state, scratchpad, params.mix_steps);
    FinalSample(state, scratchpad);
    FinalizeHash(state, header, output);
}

void HashV1(
    std::span<const unsigned char> header,
    std::span<unsigned char> scratchpad,
    std::span<unsigned char> output)
{
    static constexpr WorkParams V1_PARAMS{
        V1_MIX_STEPS,
        V1_SCRATCHPAD_FILL,
        V1_BINDING_PASSES,
    };

    Hash(
        header,
        scratchpad,
        output,
        V1_PARAMS);
}

void HashWithDiagnostics(
    std::span<const unsigned char> header,
    std::span<unsigned char> scratchpad,
    std::span<unsigned char> output,
    const WorkParams& params,
    Diagnostics& diagnostics)
{
    assert(header.size() == HEADER_SIZE);
    assert(scratchpad.size() == SCRATCHPAD_BYTES);
    assert(output.size() == OUTPUT_SIZE);
    assert(params.mix_steps > 0);

    diagnostics = {};

    State state = InitialState(header);

    FillScratchpad(state, scratchpad, params.scratchpad_fill);
    BindScratchpad(
        state,
        scratchpad,
        params.binding_passes,
        &diagnostics);
    MixScratchpadDiagnostics(
        state,
        scratchpad,
        params.mix_steps,
        diagnostics);
    FinalSample(state, scratchpad);
    FinalizeHash(state, header, output);
}

} // namespace mercahash
