// Copyright (c) 2015-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <pow.h>
#include <test/util/random.h>
#include <test/util/common.h>
#include <test/util/setup_common.h>
#include <util/chaintype.h>

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

    // Heights 1 through 24 form the first complete 24-block DGW window.
    // Twenty-four blocks contain twenty-three timestamp intervals:
    // 23 * 150 = 3450 seconds.
    auto blocks = BuildDGWChain(
        25,
        START_BITS,
        1'700'000'000,
        consensus.nPowTargetSpacing);

    CBlockHeader next_block;
    next_block.nTime =
        blocks.back().GetBlockTime() + consensus.nPowTargetSpacing;

    constexpr uint32_t EXPECTED_BITS{0x1c0f5546U};

    BOOST_CHECK_EQUAL(
        GetNextWorkRequired(&blocks.back(), &next_block, consensus),
        EXPECTED_BITS);
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

    // Make the timestamp span of the 24-block window exactly equal
    // to the locked 3600-second DGW target timespan so this vector
    // isolates the historical DGWv3 averaging recurrence.
    blocks[1].nTime =
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

    // Mainnet's current inherited powLimit encodes exactly as 0x1d00ffff.
    // Final Mercatura powLimit values are intentionally deferred until
    // the later genesis/initial-difficulty phase.
    constexpr uint32_t POW_LIMIT_BITS{0x1d00ffffU};

    auto blocks = BuildDGWChain(
        25,
        POW_LIMIT_BITS,
        1'700'000'000,
        1000);

    CBlockHeader next_block;
    next_block.nTime = blocks.back().GetBlockTime() + 1000;

    // The 23,000-second historical span exceeds the 10,800-second
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
    constexpr uint32_t NORMAL_DGW_BITS{0x1c0f5546U};

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

    // Check that the largest DGW multiplication cannot overflow.
    // DarkGravityWave() multiplies the averaged target by at most
    // nDGWMaxTimespan before dividing by nDGWTargetTimespan.
    if (!consensus.fPowNoRetargeting) {
        BOOST_REQUIRE(consensus.nDGWMaxTimespan > 0);

        arith_uint256 targ_max{
            UintToArith256(uint256{
                "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"})};
        targ_max /= consensus.nDGWMaxTimespan;

        BOOST_CHECK(UintToArith256(consensus.powLimit) < targ_max);
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
