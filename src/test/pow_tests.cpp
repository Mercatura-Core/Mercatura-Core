// Copyright (c) 2015-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <crypto/mercahash.h>
#include <pow.h>
#include <script/script.h>
#include <streams.h>
#include <test/util/random.h>
#include <test/util/common.h>
#include <test/util/setup_common.h>
#include <util/chaintype.h>
#include <util/strencodings.h>

#include <array>
#include <span>
#include <vector>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(pow_tests, BasicTestingSetup)

static std::vector<CBlockIndex> BuildDGWChain(
    int count,
    uint32_t nbits,
    int64_t start_time,
    int64_t spacing)
{
    std::vector<CBlockIndex> blocks(count);

    for (int i = 0; i < count; ++i) {
        blocks[i].pprev = i == 0 ? nullptr : &blocks[i - 1];
        blocks[i].nHeight = i;
        blocks[i].nTime = start_time + i * spacing;
        blocks[i].nBits = nbits;
    }

    return blocks;
}

BOOST_AUTO_TEST_CASE(dgw_parameters)
{
    const auto main_params =
        CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& consensus = main_params->GetConsensus();

    BOOST_CHECK_EQUAL(consensus.nPowTargetSpacing, 150);
    BOOST_CHECK_EQUAL(consensus.nDGWPastBlocks, 24);
    BOOST_CHECK_EQUAL(consensus.nDGWTargetTimespan, 3600);
    BOOST_CHECK_EQUAL(consensus.nDGWMinTimespan, 1200);
    BOOST_CHECK_EQUAL(consensus.nDGWMaxTimespan, 10800);
}

BOOST_AUTO_TEST_CASE(dgw_startup_holds_launch_difficulty)
{
    const auto main_params =
        CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& consensus = main_params->GetConsensus();

    constexpr uint32_t START_BITS{0x1c0ffff0U};

    auto blocks = BuildDGWChain(
        24,
        START_BITS,
        1'700'000'000,
        consensus.nPowTargetSpacing);

    CBlockHeader next_block;
    next_block.nTime =
        blocks.back().GetBlockTime() + consensus.nPowTargetSpacing;

    BOOST_CHECK_EQUAL(
        GetNextWorkRequired(&blocks.back(), &next_block, consensus),
        START_BITS);
}

BOOST_AUTO_TEST_CASE(dgw_first_activation_nominal_spacing)
{
    const auto main_params =
        CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& consensus = main_params->GetConsensus();

    constexpr uint32_t START_BITS{0x1c0ffff0U};

    // Height 24 is the newest of the first complete 24-target DGW
    // window. Elapsed time is measured from height 24 back to height 0,
    // giving exactly 24 * 150 = 3600 seconds.
    auto blocks = BuildDGWChain(
        25,
        START_BITS,
        1'700'000'000,
        consensus.nPowTargetSpacing);

    CBlockHeader next_block;
    next_block.nTime =
        blocks.back().GetBlockTime() + consensus.nPowTargetSpacing;

    BOOST_CHECK_EQUAL(
        GetNextWorkRequired(&blocks.back(), &next_block, consensus),
        START_BITS);
}

BOOST_AUTO_TEST_CASE(dgw_nominal_spacing_has_no_systematic_drift)
{
    const auto main_params =
        CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& consensus = main_params->GetConsensus();

    constexpr uint32_t START_BITS{0x1c0ffff0U};

    // Every possible full DGW window in this synthetic chain contains
    // exactly 24 intervals of 150 seconds and 24 identical targets.
    auto blocks = BuildDGWChain(
        100,
        START_BITS,
        1'700'000'000,
        consensus.nPowTargetSpacing);

    for (size_t height = 24; height < blocks.size(); ++height) {
        CBlockHeader next_block;
        next_block.nTime =
            blocks[height].GetBlockTime() +
            consensus.nPowTargetSpacing;

        BOOST_CHECK_EQUAL(
            GetNextWorkRequired(
                &blocks[height],
                &next_block,
                consensus),
            START_BITS);
    }
}

BOOST_AUTO_TEST_CASE(dgw_averages_24_targets_excluding_time_anchor)
{
    const auto main_params =
        CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& consensus = main_params->GetConsensus();

    constexpr uint32_t START_BITS{0x1c0ffff0U};
    constexpr uint32_t ANCHOR_BITS{0x1c07fff8U};

    auto blocks = BuildDGWChain(
        25,
        START_BITS,
        1'700'000'000,
        consensus.nPowTargetSpacing);

    // Height 0 supplies only the H-24 timestamp endpoint. Its difficulty
    // must not participate in the 24-target average of heights 24..1.
    blocks[0].nBits = ANCHOR_BITS;

    CBlockHeader next_block;
    next_block.nTime =
        blocks.back().GetBlockTime() +
        consensus.nPowTargetSpacing;

    BOOST_CHECK_EQUAL(
        GetNextWorkRequired(
            &blocks.back(),
            &next_block,
            consensus),
        START_BITS);
}

BOOST_AUTO_TEST_CASE(dgw_lower_timespan_clamp)
{
    const auto main_params =
        CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& consensus = main_params->GetConsensus();

    constexpr uint32_t START_BITS{0x1c0ffff0U};

    auto blocks = BuildDGWChain(
        25,
        START_BITS,
        1'700'000'000,
        1);

    CBlockHeader next_block;
    next_block.nTime = blocks.back().GetBlockTime() + 1;

    // Actual window span is below 1200 seconds, so the locked lower
    // clamp applies: target * 1200 / 3600 == target / 3.
    constexpr uint32_t EXPECTED_BITS{0x1c055550U};

    BOOST_CHECK_EQUAL(
        GetNextWorkRequired(&blocks.back(), &next_block, consensus),
        EXPECTED_BITS);
}

BOOST_AUTO_TEST_CASE(dgw_lower_timespan_boundary)
{
    const auto main_params =
        CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& consensus = main_params->GetConsensus();

    constexpr uint32_t START_BITS{0x1c0ffff0U};
    constexpr uint32_t CLAMPED_BITS{0x1c055550U};
    constexpr uint32_t ABOVE_CLAMP_BITS{0x1c055673U};

    const auto calculate_next_work = [&](int64_t final_time_adjustment) {
        // 25 blocks at 50-second spacing give an exact H to H-24
        // timespan of 1200 seconds.
        auto blocks = BuildDGWChain(
            25,
            START_BITS,
            1'700'000'000,
            50);

        blocks.back().nTime += final_time_adjustment;

        CBlockHeader next_block;
        next_block.nTime =
            blocks.back().GetBlockTime() +
            consensus.nPowTargetSpacing;

        return GetNextWorkRequired(
            &blocks.back(),
            &next_block,
            consensus);
    };

    // 1199 seconds is below the lower bound and must clamp to 1200.
    BOOST_CHECK_EQUAL(
        calculate_next_work(-1),
        CLAMPED_BITS);

    // Exactly 1200 seconds is the lower-boundary value itself.
    BOOST_CHECK_EQUAL(
        calculate_next_work(0),
        CLAMPED_BITS);

    // 1201 seconds is above the lower bound and must not be clamped.
    BOOST_CHECK_EQUAL(
        calculate_next_work(1),
        ABOVE_CLAMP_BITS);
}

BOOST_AUTO_TEST_CASE(dgw_upper_timespan_clamp)
{
    const auto main_params =
        CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& consensus = main_params->GetConsensus();

    constexpr uint32_t START_BITS{0x1c0ffff0U};

    auto blocks = BuildDGWChain(
        25,
        START_BITS,
        1'700'000'000,
        1000);

    CBlockHeader next_block;
    next_block.nTime = blocks.back().GetBlockTime() + 1000;

    // Actual window span exceeds 10800 seconds, so the locked upper
    // clamp applies: target * 10800 / 3600 == target * 3.
    constexpr uint32_t EXPECTED_BITS{0x1c2fffd0U};

    BOOST_CHECK_EQUAL(
        GetNextWorkRequired(&blocks.back(), &next_block, consensus),
        EXPECTED_BITS);
}

BOOST_AUTO_TEST_CASE(dgw_upper_timespan_boundary)
{
    const auto main_params =
        CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& consensus = main_params->GetConsensus();

    constexpr uint32_t START_BITS{0x1c0ffff0U};
    constexpr uint32_t BELOW_CLAMP_BITS{0x1c2ffeacU};
    constexpr uint32_t CLAMPED_BITS{0x1c2fffd0U};

    const auto calculate_next_work = [&](int64_t final_time_adjustment) {
        // 25 blocks at 450-second spacing give an exact H to H-24
        // timespan of 10800 seconds.
        auto blocks = BuildDGWChain(
            25,
            START_BITS,
            1'700'000'000,
            450);

        blocks.back().nTime += final_time_adjustment;

        CBlockHeader next_block;
        next_block.nTime =
            blocks.back().GetBlockTime() +
            consensus.nPowTargetSpacing;

        return GetNextWorkRequired(
            &blocks.back(),
            &next_block,
            consensus);
    };

    // 10799 seconds is below the upper bound and must not be clamped.
    BOOST_CHECK_EQUAL(
        calculate_next_work(-1),
        BELOW_CLAMP_BITS);

    // Exactly 10800 seconds is the upper-boundary value itself.
    BOOST_CHECK_EQUAL(
        calculate_next_work(0),
        CLAMPED_BITS);

    // 10801 seconds is above the upper bound and must clamp to 10800.
    BOOST_CHECK_EQUAL(
        calculate_next_work(1),
        CLAMPED_BITS);
}

BOOST_AUTO_TEST_CASE(dgw_alternating_fast_slow_timestamps)
{
    const auto main_params =
        CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& consensus = main_params->GetConsensus();

    constexpr uint32_t START_BITS{0x1c0ffff0U};
    constexpr uint32_t EXPECTED_BITS{0x1c133320U};

    auto blocks = BuildDGWChain(
        25,
        START_BITS,
        1'700'000'000,
        0);

    // Construct exactly 24 historical intervals alternating between
    // 60-second fast blocks and 300-second slow blocks.
    //
    // There are 12 of each:
    //
    //     12 * 60 + 12 * 300 = 4320 seconds
    //
    // This remains inside the 1200..10800 DGW clamp range, so with
    // identical historical targets the exact expected target is:
    //
    //     START_TARGET * 4320 / 3600
    //       = START_TARGET * 6 / 5
    //
    // whose canonical compact representation is 0x1c133320.
    for (int i = 1; i <= 24; ++i) {
        const int64_t interval =
            (i % 2 == 1) ? 60 : 300;

        blocks[i].nTime =
            blocks[i - 1].GetBlockTime() + interval;
    }

    CBlockHeader next_block;
    next_block.nTime =
        blocks.back().GetBlockTime() +
        consensus.nPowTargetSpacing;

    BOOST_CHECK_EQUAL(
        blocks.back().GetBlockTime() -
            blocks.front().GetBlockTime(),
        4320);

    BOOST_CHECK_EQUAL(
        GetNextWorkRequired(
            &blocks.back(),
            &next_block,
            consensus),
        EXPECTED_BITS);
}

BOOST_AUTO_TEST_CASE(dgw_historical_averaging_recurrence)
{
    const auto main_params =
        CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& consensus = main_params->GetConsensus();

    constexpr uint32_t TARGET_A{0x1c0ffff0U};
    constexpr uint32_t TARGET_B{0x1c07fff8U};

    auto blocks = BuildDGWChain(
        25,
        TARGET_A,
        1'700'000'000,
        consensus.nPowTargetSpacing);

    // Alternate two targets across the 24-block DGW window.
    // The newest block (height 24) uses TARGET_A.
    for (int i = 1; i <= 24; ++i) {
        blocks[i].nBits = (i % 2 == 0) ? TARGET_A : TARGET_B;
    }

    // Height 0 is the timestamp anchor immediately before the 24
    // averaged targets at heights 1..24. Set H - (H-24) to exactly
    // the locked 3600-second timespan so this vector isolates only
    // the established DGWv3 averaging recurrence.
    blocks[0].nTime =
        blocks.back().GetBlockTime() - consensus.nDGWTargetTimespan;

    CBlockHeader next_block;
    next_block.nTime =
        blocks.back().GetBlockTime() + consensus.nPowTargetSpacing;

    // This hard-coded result locks DGWv3's established recurrence:
    //
    //     avg = (previous_avg * count + target) / (count + 1)
    //
    // It intentionally differs from a conventional arithmetic mean.
    constexpr uint32_t EXPECTED_BITS{0x1c0c28e9U};

    BOOST_CHECK_EQUAL(
        GetNextWorkRequired(&blocks.back(), &next_block, consensus),
        EXPECTED_BITS);
}

BOOST_AUTO_TEST_CASE(dgw_pow_limit_ceiling)
{
    const auto main_params =
        CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& consensus = main_params->GetConsensus();

    // During MercaHash development, mainnet uses an intentionally easy
    // temporary PoW ceiling so genesis and consensus tests are practical.
    // Final launch difficulty and powLimit will be benchmarked separately.
    constexpr uint32_t POW_LIMIT_BITS{0x207fffffU};

    auto blocks = BuildDGWChain(
        25,
        POW_LIMIT_BITS,
        1'700'000'000,
        1000);

    CBlockHeader next_block;
    next_block.nTime = blocks.back().GetBlockTime() + 1000;

    // The 24,000-second historical span exceeds the 10,800-second
    // maximum. The 3x adjustment would make the target easier than
    // powLimit, so DGW must cap it back to powLimit.
    BOOST_CHECK_EQUAL(
        GetNextWorkRequired(&blocks.back(), &next_block, consensus),
        POW_LIMIT_BITS);
}

BOOST_AUTO_TEST_CASE(testnet_min_difficulty_delay)
{
    const auto test_params =
        CreateChainParams(*m_node.args, ChainType::TESTNET);
    const auto& consensus = test_params->GetConsensus();

    constexpr uint32_t START_BITS{0x1c0ffff0U};

    auto blocks = BuildDGWChain(
        25,
        START_BITS,
        1'700'000'000,
        consensus.nPowTargetSpacing);

    CBlockHeader exactly_two_spacings;
    exactly_two_spacings.nTime =
        blocks.back().GetBlockTime() +
        consensus.nPowTargetSpacing * 2;

    // Exactly 300 seconds does not trigger the exception because the
    // consensus rule uses a strict greater-than comparison.
    constexpr uint32_t NORMAL_DGW_BITS{START_BITS};

    BOOST_CHECK_EQUAL(
        GetNextWorkRequired(
            &blocks.back(),
            &exactly_two_spacings,
            consensus),
        NORMAL_DGW_BITS);

    CBlockHeader delayed;
    delayed.nTime =
        blocks.back().GetBlockTime() +
        consensus.nPowTargetSpacing * 2 + 1;

    const uint32_t pow_limit_bits =
        UintToArith256(consensus.powLimit).GetCompact();

    BOOST_CHECK_EQUAL(
        GetNextWorkRequired(&blocks.back(), &delayed, consensus),
        pow_limit_bits);
}

BOOST_AUTO_TEST_CASE(regtest_no_retarget)
{
    const auto regtest_params =
        CreateChainParams(*m_node.args, ChainType::REGTEST);
    const auto& consensus = regtest_params->GetConsensus();

    constexpr uint32_t START_BITS{0x2070ffffU};

    auto blocks = BuildDGWChain(
        30,
        START_BITS,
        1'700'000'000,
        1);

    CBlockHeader next_block;
    next_block.nTime = blocks.back().GetBlockTime() + 100000;

    BOOST_CHECK(consensus.fPowNoRetargeting);

    BOOST_CHECK_EQUAL(
        GetNextWorkRequired(&blocks.back(), &next_block, consensus),
        START_BITS);
}

BOOST_AUTO_TEST_CASE(mercahash_v1_pow_bridge)
{
    // Construct a canonical CBlockHeader from the permanent incremental
    // 80-byte MercaHash-v1 input vector.
    std::array<unsigned char, mercahash::HEADER_SIZE> raw_header{};

    for (std::size_t i = 0; i < raw_header.size(); ++i) {
        raw_header[i] =
            static_cast<unsigned char>(i);
    }

    CBlockHeader header;

    SpanReader reader{
        std::span<const unsigned char>{
            raw_header.data(),
            raw_header.size()}};

    reader >> header;

    BOOST_CHECK(reader.empty());

    // Serializing the reconstructed header must reproduce exactly the same
    // canonical 80 bytes.
    std::vector<unsigned char> serialized_header;

    VectorWriter{
        serialized_header,
        0,
        header,
    };

    BOOST_REQUIRE_EQUAL(
        serialized_header.size(),
        mercahash::HEADER_SIZE);

    BOOST_CHECK_EQUAL_COLLECTIONS(
        serialized_header.begin(),
        serialized_header.end(),
        raw_header.begin(),
        raw_header.end());

    std::vector<unsigned char> scratchpad(
        mercahash::SCRATCHPAD_BYTES);

    const uint256 pow_hash =
        GetPoWHash(
            header,
            scratchpad);

    const std::vector<unsigned char> expected =
        ParseHex(
            "2321712af21502878986c0c4f21d79e1"
            "7e2281619e3958f11e3c634c9f17f7d8");

    BOOST_REQUIRE_EQUAL(
        expected.size(),
        uint256::size());

    // uint256 stores the raw MercaHash bytes exactly as produced.
    BOOST_CHECK_EQUAL_COLLECTIONS(
        pow_hash.begin(),
        pow_hash.end(),
        expected.begin(),
        expected.end());

    // Block identity remains the separate SHA256d GetHash() path.
    BOOST_CHECK(pow_hash != header.GetHash());

    // The reusable production context must produce the identical
    // MercaHash-v1 result without reallocating its scratchpad per hash.
    PoWHashContext context;

    const uint256 context_hash =
        context.GetHash(header);

    BOOST_CHECK_EQUAL_COLLECTIONS(
        context_hash.begin(),
        context_hash.end(),
        expected.begin(),
        expected.end());

    BOOST_CHECK(context_hash == pow_hash);

    // Reusing the same context must remain deterministic.
    const uint256 repeated_context_hash =
        context.GetHash(header);

    BOOST_CHECK(repeated_context_hash == context_hash);
}

BOOST_AUTO_TEST_CASE(mercahash_development_genesis_miner)
{
    static constexpr uint32_t DEV_BITS{0x207fffffU};

    const uint256 dev_pow_limit{
        "7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};

    const auto mine_genesis =
        [&](ChainType chain_type, const char* network_name) {
            const auto params{
                CreateChainParams(
                    *m_node.args,
                    chain_type)};

            CBlock genesis{
                params->GenesisBlock()};

            Consensus::Params dev_consensus{
                params->GetConsensus()};

            dev_consensus.powLimit =
                dev_pow_limit;

            genesis.nBits = DEV_BITS;
            genesis.nNonce = 0;

            PoWHashContext pow_context;

            while (!CheckProofOfWork(
                genesis,
                dev_consensus,
                pow_context)) {
                ++genesis.nNonce;
            }

            const uint256 pow_hash{
                pow_context.GetHash(genesis)};

            BOOST_TEST_MESSAGE(
                "========================================");

            BOOST_TEST_MESSAGE(
                network_name
                << " development genesis");

            BOOST_TEST_MESSAGE(
                "nTime: "
                << genesis.nTime);

            BOOST_TEST_MESSAGE(
                "nBits: "
                << strprintf("%08x", genesis.nBits));

            BOOST_TEST_MESSAGE(
                "nNonce: "
                << genesis.nNonce);

            BOOST_TEST_MESSAGE(
                "block id: "
                << genesis.GetHash().GetHex());

            BOOST_TEST_MESSAGE(
                "MercaHash: "
                << pow_hash.GetHex());

            BOOST_TEST_MESSAGE(
                "merkle root: "
                << genesis.hashMerkleRoot.GetHex());

            BOOST_REQUIRE(
                CheckProofOfWorkImpl(
                    pow_hash,
                    genesis.nBits,
                    dev_consensus));
        };

    mine_genesis(
        ChainType::MAIN,
        "mainnet");

    mine_genesis(
        ChainType::TESTNET,
        "testnet");

    mine_genesis(
        ChainType::TESTNET4,
        "testnet4");

    mine_genesis(
        ChainType::SIGNET,
        "signet");
}

BOOST_AUTO_TEST_CASE(mercahash_genesis_pow)
{
    const auto check_genesis =
        [&](ChainType chain_type, const char* network_name) {
            const auto params{
                CreateChainParams(
                    *m_node.args,
                    chain_type)};

            const CBlock& genesis{
                params->GenesisBlock()};

            PoWHashContext pow_context;

            const uint256 pow_hash{
                pow_context.GetHash(genesis)};

            const bool valid{
                CheckProofOfWorkImpl(
                    pow_hash,
                    genesis.nBits,
                    params->GetConsensus())};

            BOOST_TEST_MESSAGE(
                network_name
                << " genesis block id: "
                << genesis.GetHash().GetHex());

            BOOST_TEST_MESSAGE(
                network_name
                << " genesis MercaHash: "
                << pow_hash.GetHex());

            BOOST_TEST_MESSAGE(
                network_name
                << " genesis nNonce: "
                << genesis.nNonce);

            BOOST_TEST_MESSAGE(
                network_name
                << " genesis nBits: "
                << strprintf("%08x", genesis.nBits));

            BOOST_TEST_MESSAGE(
                network_name
                << " MercaHash PoW valid: "
                << (valid ? "YES" : "NO"));

            BOOST_CHECK_MESSAGE(
                valid,
                network_name
                    << " genesis does not satisfy MercaHash-v1");
        };

    check_genesis(
        ChainType::MAIN,
        "mainnet");

    check_genesis(
        ChainType::TESTNET,
        "testnet");

    check_genesis(
        ChainType::TESTNET4,
        "testnet4");

    check_genesis(
        ChainType::SIGNET,
        "signet");

    check_genesis(
        ChainType::REGTEST,
        "regtest");
}

BOOST_AUTO_TEST_CASE(mercahash_genesis_vectors)
{
    struct GenesisVector {
        ChainType chain_type;
        const char* network_name;
        uint32_t time;
        uint32_t nonce;
        uint32_t bits;
        int32_t version;
        uint256 block_id;
        uint256 merkle_root;
        uint256 pow_hash;
    };

    const std::array<GenesisVector, 4> vectors{{
        {
            ChainType::MAIN,
            "main",
            1788680812U,
            1U,
            0x207fffffU,
            1,
            uint256{"cd797c78731d68a82b664b3e359a2e69508ea873fe5747b686488589cc7d6f15"},
            uint256{"b0c99eda385fbc3314f2e877cb2a563ad9d22d6dd18f80fac9653c2f65a8caa6"},
            uint256{"5c448423ce7720ab03860668416887a60b00c827edf31c57c48f9f12fd407afe"},
        },
        {
            ChainType::TESTNET,
            "testnet",
            1788566400U,
            2U,
            0x207fffffU,
            1,
            uint256{"0cee25abd571760687efbebbe8741873dc187ce46afe082282a47b2455320d73"},
            uint256{"98bd90b0fdc8794f4fdfbadc78a068345abb119e0cbb47a6aa2cfc39ebfee891"},
            uint256{"06f5e56273c1d90c43e1540c5582e8e94e34e1bb378fd8932e721b1c62df7f8a"},
        },
        {
            ChainType::SIGNET,
            "signet",
            1788566400U,
            1U,
            0x207fffffU,
            1,
            uint256{"eebe2b23469b0d91056cc9240387ba7ee601ce138160da3c508142e039e1f36b"},
            uint256{"b750c920894b9f96090f1d5de2ccfe46a1a0745ad19d46633956c4192d156cca"},
            uint256{"48039c73237474db04057290c45c574840f8561aa19e4c4e13418f9a6b9dbcc6"},
        },
        {
            ChainType::REGTEST,
            "regtest",
            1788566400U,
            1U,
            0x207fffffU,
            1,
            uint256{"8e2308efb3a16b126e69444329cc0ed81bea0596e99db1032ccd750e7028f685"},
            uint256{"f72f87b6fde6ba17583954c5c0ffec58909e4f49752ea7f125e4ecaf488ab83c"},
            uint256{"5529a31aab49396a10b4a925d923b32241d330728e5cff33d39cbc6301769d11"},
        },
    }};

    for (const auto& vector : vectors) {
        const auto params{
            CreateChainParams(*m_node.args, vector.chain_type)};

        const CBlock& genesis{
            params->GenesisBlock()};

        BOOST_TEST_CONTEXT(vector.network_name) {
            BOOST_CHECK_EQUAL(genesis.nTime, vector.time);
            BOOST_CHECK_EQUAL(genesis.nNonce, vector.nonce);
            BOOST_CHECK_EQUAL(genesis.nBits, vector.bits);
            BOOST_CHECK_EQUAL(genesis.nVersion, vector.version);
            BOOST_CHECK(genesis.hashPrevBlock.IsNull());

            BOOST_CHECK_EQUAL(genesis.GetHash(), vector.block_id);
            BOOST_CHECK_EQUAL(genesis.hashMerkleRoot, vector.merkle_root);

            PoWHashContext pow_context;
            const uint256 pow_hash{
                pow_context.GetHash(genesis)};

            BOOST_CHECK_EQUAL(pow_hash, vector.pow_hash);

            BOOST_CHECK(
                CheckProofOfWork(
                    genesis,
                    params->GetConsensus(),
                    pow_context));

            BOOST_REQUIRE_EQUAL(genesis.vtx.size(), 1U);

            const CTransaction& coinbase{
                *genesis.vtx.front()};

            BOOST_CHECK(coinbase.IsCoinBase());
            BOOST_REQUIRE_EQUAL(coinbase.vout.size(), 1U);

            const CTxOut& output{
                coinbase.vout.front()};

            BOOST_CHECK_EQUAL(output.nValue, 50 * COIN);
            BOOST_CHECK(output.scriptPubKey.IsUnspendable());
            BOOST_REQUIRE(!output.scriptPubKey.empty());
            BOOST_CHECK_EQUAL(output.scriptPubKey[0], OP_RETURN);
        }
    }
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_negative_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    nBits = UintToArith256(consensus.powLimit).GetCompact(true);
    hash = uint256{1};
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_overflow_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits{~0x00800000U};
    hash = uint256{1};
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_too_easy_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    arith_uint256 nBits_arith = UintToArith256(consensus.powLimit);
    nBits_arith *= 2;
    nBits = nBits_arith.GetCompact();
    hash = uint256{1};
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_biger_hash_than_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    arith_uint256 hash_arith = UintToArith256(consensus.powLimit);
    nBits = hash_arith.GetCompact();
    hash_arith *= 2; // hash > nBits
    hash = ArithToUint256(hash_arith);
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_target_boundary)
{
    const auto consensus =
        CreateChainParams(*m_node.args, ChainType::REGTEST)->GetConsensus();

    const unsigned int nBits =
        UintToArith256(consensus.powLimit).GetCompact();

    arith_uint256 target;
    target.SetCompact(nBits);

    // Exact target is valid.
    BOOST_CHECK(
        CheckProofOfWork(
            ArithToUint256(target),
            nBits,
            consensus));

    // One above the exact target is invalid.
    arith_uint256 above_target = target;
    above_target += 1;

    BOOST_CHECK(
        !CheckProofOfWork(
            ArithToUint256(above_target),
            nBits,
            consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_zero_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    arith_uint256 hash_arith{0};
    nBits = hash_arith.GetCompact();
    hash = ArithToUint256(hash_arith);
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(GetBlockProofEquivalentTime_test)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    std::vector<CBlockIndex> blocks(10000);
    for (int i = 0; i < 10000; i++) {
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = i;
        blocks[i].nTime = 1269211443 + i * chainParams->GetConsensus().nPowTargetSpacing;
        blocks[i].nBits = 0x207fffff; /* target 0x7fffff000... */
        blocks[i].nChainWork = i ? blocks[i - 1].nChainWork + GetBlockProof(blocks[i - 1]) : arith_uint256(0);
    }

    for (int j = 0; j < 1000; j++) {
        CBlockIndex *p1 = &blocks[m_rng.randrange(10000)];
        CBlockIndex *p2 = &blocks[m_rng.randrange(10000)];
        CBlockIndex *p3 = &blocks[m_rng.randrange(10000)];

        int64_t tdiff = GetBlockProofEquivalentTime(*p1, *p2, *p3, chainParams->GetConsensus());
        BOOST_CHECK_EQUAL(tdiff, p1->GetBlockTime() - p2->GetBlockTime());
    }
}

void sanity_check_chainparams(const ArgsManager& args, ChainType chain_type)
{
    const auto chainParams = CreateChainParams(args, chain_type);
    const auto consensus = chainParams->GetConsensus();

    // hash genesis is correct
    BOOST_CHECK_EQUAL(consensus.hashGenesisBlock, chainParams->GenesisBlock().GetHash());

    // DGW's nominal window must exactly match its configured block spacing.
    BOOST_CHECK_EQUAL(
        consensus.nDGWTargetTimespan,
        consensus.nDGWPastBlocks * consensus.nPowTargetSpacing);

    // genesis nBits is positive, doesn't overflow and is lower than powLimit
    arith_uint256 pow_compact;
    bool neg, over;
    pow_compact.SetCompact(chainParams->GenesisBlock().nBits, &neg, &over);
    BOOST_CHECK(!neg && pow_compact != 0);
    BOOST_CHECK(!over);
    BOOST_CHECK(UintToArith256(consensus.powLimit) >= pow_compact);

    // DGW uses overflow-safe quotient/remainder scaling, so powLimit is not
    // artificially constrained by a target * timespan intermediate value.
    // Still verify that all retarget timing constants are internally valid.
    if (!consensus.fPowNoRetargeting) {
        BOOST_CHECK(consensus.nDGWTargetTimespan > 0);
        BOOST_CHECK(consensus.nDGWMinTimespan > 0);
        BOOST_CHECK(
            consensus.nDGWMaxTimespan >=
            consensus.nDGWMinTimespan);
    }
}

BOOST_AUTO_TEST_CASE(ChainParams_MAIN_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::MAIN);
}

BOOST_AUTO_TEST_CASE(ChainParams_REGTEST_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::REGTEST);
}

BOOST_AUTO_TEST_CASE(ChainParams_TESTNET_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::TESTNET);
}

BOOST_AUTO_TEST_CASE(ChainParams_TESTNET4_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::TESTNET4);
}

BOOST_AUTO_TEST_CASE(ChainParams_SIGNET_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::SIGNET);
}

BOOST_AUTO_TEST_SUITE_END()
