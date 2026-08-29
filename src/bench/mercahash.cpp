// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <bench/bench.h>
#include <crypto/mercahash.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

void RunMercaHashBench(
    benchmark::Bench& bench,
    std::uint32_t mix_steps,
    mercahash::ScratchpadFillMode fill_mode,
    std::uint32_t binding_passes = 0)
{
    std::array<unsigned char, mercahash::HEADER_SIZE> header{};
    for (std::size_t i = 0; i < header.size(); ++i) {
        header[i] = static_cast<unsigned char>(i);
    }

    std::vector<unsigned char> scratchpad(mercahash::SCRATCHPAD_BYTES);
    std::array<unsigned char, mercahash::OUTPUT_SIZE> output{};

    const mercahash::WorkParams params{
        mix_steps,
        fill_mode,
        binding_passes,
    };

    bench
        .epochs(3)
        .epochIterations(1)
        .unit("hash")
        .run([&] {
            mercahash::Hash(header, scratchpad, output, params);
        });
}

void MercaHash_Alt_1(benchmark::Bench& bench)
{
    RunMercaHashBench(
        bench,
        1,
        mercahash::ScratchpadFillMode::ALTERNATING_HALF_ROUND);
}

void MercaHash_Alt_131072(benchmark::Bench& bench)
{
    RunMercaHashBench(
        bench,
        131072,
        mercahash::ScratchpadFillMode::ALTERNATING_HALF_ROUND);
}

void MercaHash_Alt_196608(benchmark::Bench& bench)
{
    RunMercaHashBench(
        bench,
        196608,
        mercahash::ScratchpadFillMode::ALTERNATING_HALF_ROUND);
}

void MercaHash_Alt_262144(benchmark::Bench& bench)
{
    RunMercaHashBench(
        bench,
        262144,
        mercahash::ScratchpadFillMode::ALTERNATING_HALF_ROUND);
}

void MercaHash_Alt_1_Bind1(benchmark::Bench& bench)
{
    RunMercaHashBench(
        bench,
        1,
        mercahash::ScratchpadFillMode::ALTERNATING_HALF_ROUND,
        1);
}

void MercaHash_Alt_131072_Bind1(benchmark::Bench& bench)
{
    RunMercaHashBench(
        bench,
        131072,
        mercahash::ScratchpadFillMode::ALTERNATING_HALF_ROUND,
        1);
}

void MercaHash_Alt_1_Bind2(benchmark::Bench& bench)
{
    RunMercaHashBench(
        bench,
        1,
        mercahash::ScratchpadFillMode::ALTERNATING_HALF_ROUND,
        2);
}

void MercaHash_Alt_131072_Bind2(benchmark::Bench& bench)
{
    RunMercaHashBench(
        bench,
        131072,
        mercahash::ScratchpadFillMode::ALTERNATING_HALF_ROUND,
        2);
}

void MercaHash_TwoFull_1(benchmark::Bench& bench)
{
    RunMercaHashBench(
        bench,
        1,
        mercahash::ScratchpadFillMode::TWO_FULL_ROUNDS);
}

} // namespace

BENCHMARK(MercaHash_Alt_1);
BENCHMARK(MercaHash_Alt_131072);
BENCHMARK(MercaHash_Alt_196608);
BENCHMARK(MercaHash_Alt_262144);
BENCHMARK(MercaHash_Alt_1_Bind1);
BENCHMARK(MercaHash_Alt_131072_Bind1);
BENCHMARK(MercaHash_Alt_1_Bind2);
BENCHMARK(MercaHash_Alt_131072_Bind2);
BENCHMARK(MercaHash_TwoFull_1);
