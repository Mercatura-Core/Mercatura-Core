// Copyright (c) 2023-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bench/bench.h>
#include <test/util/setup_common.h>
#include <validation.h>

#include <memory>

static void CheckBlockIndex(benchmark::Bench& bench)
{
    auto testing_setup{MakeNoLogFileContext<TestChain100Setup>()};
    // Keep enough additional chain history to exercise CheckBlockIndex()
    // without making MercaHash block generation dominate benchmark setup.
    testing_setup->mineBlocks(100);
    bench.run([&] {
        testing_setup->m_node.chainman->CheckBlockIndex();
    });
}


BENCHMARK(CheckBlockIndex);
