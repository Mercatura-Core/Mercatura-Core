// Copyright (c) 2023-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <addresstype.h>
#include <bench/bench.h>
#include <coins.h>
#include <key.h>
#include <primitives/transaction.h>
#include <pubkey.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <script/sign.h>
#include <script/signingprovider.h>
#include <span.h>
#include <test/util/random.h>
#include <uint256.h>
#include <util/translation.h>

#include <cassert>
#include <map>
#include <vector>

static void SignSchnorrTapTweakBenchmark(benchmark::Bench& bench, bool use_null_merkle_root)
{
    FastRandomContext rng;
    ECC_Context ecc_context{};

    auto key = GenerateRandomKey();
    auto msg = rng.rand256();
    auto merkle_root = use_null_merkle_root ? uint256() : rng.rand256();
    auto aux = rng.rand256();
    std::vector<unsigned char> sig(64);

    bench.minEpochIterations(100).run([&] {
        bool success = key.SignSchnorr(msg, sig, &merkle_root, aux);
        assert(success);
    });
}

static void SignSchnorrWithMerkleRoot(benchmark::Bench& bench)
{
    SignSchnorrTapTweakBenchmark(bench, /*use_null_merkle_root=*/false);
}

static void SignSchnorrWithNullMerkleRoot(benchmark::Bench& bench)
{
    SignSchnorrTapTweakBenchmark(bench, /*use_null_merkle_root=*/true);
}

BENCHMARK(SignSchnorrWithMerkleRoot);
BENCHMARK(SignSchnorrWithNullMerkleRoot);
