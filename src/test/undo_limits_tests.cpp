// Copyright (c) 2026 The Mercatura developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <chainparamsbase.h>
#include <consensus/merkle.h>
#include <consensus/tx_check.h>
#include <consensus/validation.h>
#include <hash.h>
#include <key.h>
#include <node/blockstorage.h>
#include <node/miner.h>
#include <pow.h>
#include <script/interpreter.h>
#include <script/solver.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <undo.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

void UpdateCoins(const CTransaction& tx, CCoinsViewCache& inputs, CTxUndo& txundo, int nHeight);

namespace {
CScript LargeScript(size_t size)
{
    CScript script;
    while (script.size() + 525 <= size) script << std::vector<unsigned char>(520, 0) << OP_DROP;
    const auto remaining{size - script.size()};
    if (remaining > 5) script << std::vector<unsigned char>(remaining - 5, 0) << OP_DROP;
    while (script.size() + 1 < size) script << OP_NOP;
    return script << OP_TRUE;
}

/** Supplies coins only as requested, exposing accidental eager input loading. */
struct GeneratedCoins : CCoinsView {
    Coin coin;
    mutable uint64_t reads{0};
    explicit GeneratedCoins(Coin value) : coin{std::move(value)} {}
    std::optional<Coin> GetCoin(const COutPoint& point) const override
    {
        if (point.hash != Txid::FromUint256(uint256::ONE)) return std::nullopt;
        ++reads;
        return coin;
    }
};

CMutableTransaction Spend(uint32_t count)
{
    CMutableTransaction tx;
    for (uint32_t i{0}; i < count; ++i) tx.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), i});
    tx.vout.emplace_back(0, CScript{} << OP_TRUE);
    return tx;
}

struct UndoLimitSetup : TestingSetup {
    const Txid funding{Txid::FromUint256(uint256{42})};
    PoWHashContext pow;

    UndoLimitSetup() : TestingSetup{ChainType::REGTEST, TestOpts{.extra_args = {"-testactivationheight=undo@3"}}} {}

    CBlock Block(const std::vector<CTransactionRef>& transactions = {})
    {
        node::BlockAssembler::Options options;
        options.coinbase_output_script = CScript{} << OP_TRUE;
        options.include_dummy_extranonce = true;
        auto block{node::BlockAssembler{m_node.chainman->ActiveChainstate(), nullptr, options}.CreateNewBlock()->block};
        block.vtx.insert(block.vtx.end(), transactions.begin(), transactions.end());
        node::RegenerateCommitments(block, *m_node.chainman);
        return block;
    }

    void Mine()
    {
        auto block{Block()};
        while (!CheckProofOfWork(block, Params().GetConsensus(), pow)) ++block.nNonce;
        BOOST_REQUIRE(m_node.chainman->ProcessNewBlock(std::make_shared<const CBlock>(block), true, true, nullptr));
    }
};
} // namespace

BOOST_FIXTURE_TEST_SUITE(undo_limits_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(capacity_schedule_and_activation)
{
    ArgsManager args;
    for (const auto network : {ChainType::MAIN, ChainType::REGTEST, ChainType::TESTNET, ChainType::TESTNET4, ChainType::SIGNET}) {
        const auto params{CreateChainParams(args, network)};
        BOOST_CHECK_EQUAL(params->GetConsensus().nBlockUndoHeight,
                          network == ChainType::MAIN || network == ChainType::REGTEST ? 0 : -1);
    }
    const auto main{CreateChainParams(args, ChainType::MAIN)};
    GeneratedCoins source{Coin{CTxOut{0, CScript{} << OP_TRUE}, 1, false}};
    CCoinsViewCache inputs{&source};
    CBlockUndo sample;
    sample.vtxundo.resize(1);
    sample.vtxundo[0].vprevout.push_back(source.coin);
    const uint64_t contribution{GetSerializeSize(sample) - GetSizeOfCompactSize(1)};
    const CTransaction transaction{Spend(1)};
    for (int64_t doubling{0}; doubling <= 10; ++doubling) {
        const int64_t boundary{doubling * static_cast<int64_t>(MERCATURA_BLOCK_CAPACITY_DOUBLING_INTERVAL)};
        for (const int64_t height : {std::max<int64_t>(0, boundary - 1), boundary, boundary + 1}) {
            const uint64_t expected{(4ULL << 20) << std::min<int64_t>(height / MERCATURA_BLOCK_CAPACITY_DOUBLING_INTERVAL, 8)};
            const auto limit{GetMaxBlockUndoBytes(main->GetConsensus(), height)};
            BOOST_CHECK_EQUAL(limit, expected * 2);
            BOOST_CHECK_LT(limit, uint64_t{1} << 32);
            // Counting only: do not materialize the boundary-sized objects.
            for (const auto size : {limit - 1, limit, limit + 1}) {
                SizeComputer counter;
                counter.seek(size);
                BOOST_CHECK_EQUAL(counter.size() <= limit, size != limit + 1);
                uint64_t prior{counter.size() - contribution};
                BOOST_CHECK_EQUAL(CheckTxUndoSize(transaction, inputs, prior, limit), size <= limit);
                BOOST_CHECK_LE(prior, limit);
            }
        }
    }
    Consensus::Params saturated{};
    saturated.nInitialBlockCapacityBytes = std::numeric_limits<uint64_t>::max();
    BOOST_CHECK_EQUAL(GetMaxBlockUndoBytes(saturated, std::numeric_limits<int64_t>::max()), 2ULL << 30);

    SetupChainParamsBaseOptions(args);
    const char* command[]{"test", "-testactivationheight=undo@17"};
    std::string error;
    BOOST_REQUIRE(args.ParseParameters(2, command, error));
    BOOST_CHECK_EQUAL(CreateChainParams(args, ChainType::REGTEST)->GetConsensus().nBlockUndoHeight, 17);
    for (const auto network : {ChainType::MAIN, ChainType::TESTNET, ChainType::TESTNET4, ChainType::SIGNET}) {
        BOOST_CHECK_THROW(CreateChainParams(args, network), std::runtime_error);
    }
    for (const auto height : {"-1", "2147483647", "not-a-height"}) {
        args.ForceSetArg("-testactivationheight", std::string{"undo@"} + height);
        BOOST_CHECK_THROW(CreateChainParams(args, ChainType::REGTEST), std::runtime_error);
    }
}

BOOST_AUTO_TEST_CASE(exact_serialized_accounting)
{
    CCoinsView base;
    CCoinsViewCache view{&base};
    std::vector<CScript> scripts{CScript{}, CScript{} << OP_TRUE, LargeScript(121), LargeScript(122), LargeScript(123), LargeScript(10'000)};
    scripts.push_back(CScript{} << OP_DUP << OP_HASH160 << std::vector<unsigned char>(20, 1) << OP_EQUALVERIFY << OP_CHECKSIG);
    scripts.push_back(CScript{} << OP_HASH160 << std::vector<unsigned char>(20, 2) << OP_EQUAL);
    CKey key;
    key.MakeNewKey(true);
    scripts.push_back(GetScriptForRawPubKey(key.GetPubKey()));
    key.MakeNewKey(false);
    scripts.push_back(GetScriptForRawPubKey(key.GetPubKey()));
    uint32_t id{0};
    for (const auto& counts : {std::vector<uint32_t>{}, {0}, {1}, {2, 3, 1}, {252, 253, 254}}) {
        for (const uint32_t height : {0U, 1U, 63U, 64U, 8'255U, 8'256U, 1'056'831U, 1'056'832U, 135'274'559U, 135'274'560U, 0x7fffffffU}) {
            CBlockUndo undo;
            uint64_t counted{GetSizeOfCompactSize(counts.size())};
            for (const auto count : counts) {
                auto tx{Spend(0)};
                CTxUndo txundo;
                for (uint32_t i{0}; i < count; ++i) {
                    const COutPoint point{Txid::FromUint256(uint256::ONE), id++};
                    const CAmount values[]{0, 1, 9, 10, 100, MAX_MONEY};
                    Coin coin{CTxOut{values[i % std::size(values)], scripts[i % scripts.size()]}, static_cast<int>(height), bool(i % 2)};
                    tx.vin.emplace_back(point);
                    view.AddCoin(point, Coin{coin}, false);
                    txundo.vprevout.push_back(std::move(coin));
                }
                BOOST_REQUIRE(CheckTxUndoSize(CTransaction{tx}, view, counted, std::numeric_limits<uint64_t>::max()));
                undo.vtxundo.push_back(std::move(txundo));
            }
            DataStream encoded;
            encoded << undo;
            BOOST_CHECK_EQUAL(counted, encoded.size());
            BOOST_CHECK_EQUAL(counted, GetSerializeSize(undo));
            CBlockUndo decoded;
            encoded >> decoded;
            BOOST_CHECK_EQUAL(GetSerializeSize(decoded), counted);
        }
    }
    // CompactSize's five-byte boundary, using tiny coins/empty transaction undos.
    for (const uint32_t count : {252U, 253U, 65'535U, 65'536U}) {
        GeneratedCoins source{Coin{CTxOut{0, CScript{} << OP_TRUE}, 1, false}};
        CCoinsViewCache inputs{&source};
        const CTransaction tx{Spend(count)};
        CBlockUndo undo;
        undo.vtxundo.resize(1);
        undo.vtxundo[0].vprevout.assign(count, source.coin);
        uint64_t size{1};
        BOOST_REQUIRE(CheckTxUndoSize(tx, inputs, size, std::numeric_limits<uint64_t>::max()));
        BOOST_CHECK_EQUAL(size, GetSerializeSize(undo));
        undo.vtxundo.assign(count, CTxUndo{});
        size = GetSizeOfCompactSize(count);
        const CTransaction empty{Spend(0)};
        for (uint32_t i{0}; i < count; ++i) BOOST_REQUIRE(CheckTxUndoSize(empty, inputs, size, std::numeric_limits<uint64_t>::max()));
        BOOST_CHECK_EQUAL(size, GetSerializeSize(undo));
    }
    for (const uint64_t code : {0ULL, 127ULL, 128ULL, 16'511ULL, 16'512ULL, 2'113'663ULL, 2'113'664ULL}) {
        const auto amount{static_cast<CAmount>(DecompressAmount(code))};
        BOOST_REQUIRE(MoneyRange(amount));
        GeneratedCoins source{Coin{CTxOut{amount, CScript{} << OP_TRUE}, 1, false}};
        CCoinsViewCache inputs{&source};
        CBlockUndo undo;
        undo.vtxundo.resize(1);
        undo.vtxundo[0].vprevout.push_back(source.coin);
        uint64_t size{1};
        BOOST_REQUIRE(CheckTxUndoSize(CTransaction{Spend(1)}, inputs, size, std::numeric_limits<uint64_t>::max()));
        BOOST_CHECK_EQUAL(size, GetSerializeSize(undo));
    }
}

BOOST_AUTO_TEST_CASE(incremental_rejection_and_integer_boundaries)
{
    GeneratedCoins source{Coin{CTxOut{0, LargeScript(9'957)}, 1'000'000, true}};
    const uint64_t coin_size{GetSerializeSize(Using<TxInUndoFormatter>(source.coin))};
    BOOST_REQUIRE_EQUAL(coin_size, 9'964);
    SizeComputer original;
    WriteCompactSize(original, 20);
    for (unsigned i{0}; i < 20; ++i) {
        WriteCompactSize(original, 24'000);
        original.seek(24'000 * coin_size);
    }
    BOOST_CHECK_EQUAL(original.size(), 4'782'720'061ULL);
    const auto main{CreateChainParams(ArgsManager{}, ChainType::MAIN)};
    const uint64_t limit{GetMaxBlockUndoBytes(main->GetConsensus(), 3'153'600)};
    BOOST_CHECK_EQUAL(limit, 64ULL << 20);
    CCoinsViewCache inputs{&source};
    uint64_t size{GetSizeOfCompactSize(20)};
    // Even the first transaction is stopped before its 24,000 inputs are loaded.
    BOOST_CHECK(!CheckTxUndoSize(CTransaction{Spend(24'000)}, inputs, size, limit));
    BOOST_CHECK_LE(size, limit);
    BOOST_CHECK_EQUAL(source.reads, (limit - 4) / coin_size + 1);
    BOOST_CHECK_LT(source.reads, 7'000);

    GeneratedCoins tiny{Coin{CTxOut{0, CScript{} << OP_TRUE}, 1, false}};
    CCoinsViewCache view{&tiny};
    const CTransaction tx{Spend(1)};
    const uint64_t contribution{1 + GetSerializeSize(Using<TxInUndoFormatter>(tiny.coin))};
    for (const uint64_t budget : {uint64_t{1}, uint64_t{1} << 32, std::numeric_limits<uint64_t>::max()}) {
        for (const uint64_t distance : {uint64_t{0}, contribution - 1, contribution}) {
            size = budget - std::min(budget, distance);
            const bool expected{budget >= distance && distance >= contribution};
            BOOST_CHECK_EQUAL(CheckTxUndoSize(tx, view, size, budget), expected);
            BOOST_CHECK_LE(size, budget);
        }
    }
    size = std::numeric_limits<uint64_t>::max();
    BOOST_CHECK(!CheckTxUndoSize(tx, view, size, 1));
    BOOST_CHECK_EQUAL(size, std::numeric_limits<uint64_t>::max());
}

BOOST_FIXTURE_TEST_CASE(connection_boundaries_and_atomic_rejection, UndoLimitSetup)
{
    Mine();
    auto& chainstate{m_node.chainman->ActiveChainstate()};
    LOCK(cs_main);
    auto& coins{chainstate.CoinsTip()};
    const uint64_t limit{GetMaxBlockUndoBytes(Params().GetConsensus(), 3)};
    const Coin full{CTxOut{0, LargeScript(10'000)}, 1, false};
    CMutableTransaction first;
    first.vin.emplace_back(COutPoint{funding, 10'000});
    first.vout.emplace_back(0, CScript{} << OP_TRUE);
    const auto initial{MakeTransactionRef(first)};
    coins.AddCoin(first.vin[0].prevout, Coin{CTxOut{0, CScript{} << OP_TRUE}, 1, false}, false);
    auto spending{Spend(0)};
    spending.vin.emplace_back(COutPoint{initial->GetHash(), 0}); // Intra-block coin must count too.
    for (uint32_t i{0}; i < 838; ++i) {
        const COutPoint point{funding, i};
        coins.AddCoin(point, Coin{full}, false);
        spending.vin.emplace_back(point);
    }
    // Prefixes, two tiny coins (one created by first), and the last coin's metadata.
    const uint64_t fixed{1 + 1 + 3 + 5 + 5 + 838 * GetSerializeSize(Using<TxInUndoFormatter>(full)) + 5};
    std::vector<CTransactionRef> variants;
    for (int delta : {-1, 0, 1}) {
        const COutPoint point{funding, static_cast<uint32_t>(900 + delta)};
        const size_t script_size{static_cast<size_t>(limit - fixed + delta)};
        coins.AddCoin(point, Coin{CTxOut{0, LargeScript(script_size)}, 1, false}, false);
        auto tx{spending};
        tx.vin.emplace_back(point);
        variants.push_back(MakeTransactionRef(tx));
    }
    // Same otherwise-valid over-budget body is accepted before activation.
    for (const int height : {2, 3, 4}) {
        BOOST_REQUIRE_EQUAL(chainstate.m_chain.Height() + 1, height);
        for (size_t variant{0}; variant < variants.size(); ++variant) {
            const auto block{Block({initial, variants[variant]})};
            const auto previous{coins.GetBestBlock()};
            const auto state{TestBlockValidity(chainstate, block, false, true)};
            const bool expected{height < 3 || variant < 2};
            BOOST_CHECK_EQUAL(state.IsValid(), expected);
            if (!expected) {
                BOOST_CHECK(state.GetResult() == BlockValidationResult::BLOCK_CONSENSUS);
                BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-blk-undo-size");
                BOOST_CHECK(!state.IsError());
            }
            BOOST_CHECK(coins.GetBestBlock() == previous);
            BOOST_CHECK(coins.HaveCoin(first.vin[0].prevout));
            BOOST_CHECK(coins.HaveCoin(spending.vin[1].prevout));
            BOOST_CHECK(!coins.HaveCoin(COutPoint{initial->GetHash(), 0}));
            // Independently reconstruct the small expected undo, including the intra-block height.
            CCoinsViewCache scratch{&coins};
            CBlockUndo undo;
            undo.vtxundo.resize(2);
            UpdateCoins(*initial, scratch, undo.vtxundo[0], height);
            UpdateCoins(*variants[variant], scratch, undo.vtxundo[1], height);
            BOOST_CHECK_EQUAL(GetSerializeSize(undo), limit - 1 + variant);
        }
        Mine();
    }
}

BOOST_FIXTURE_TEST_CASE(connection_stops_loading_inputs_at_budget, UndoLimitSetup)
{
    Mine();
    Mine();
    LOCK(cs_main);
    auto& chainstate{m_node.chainman->ActiveChainstate()};
    GeneratedCoins source{Coin{CTxOut{0, LargeScript(9'957)}, 1, false}};
    CCoinsViewCache view{&source};
    const auto previous{chainstate.CoinsTip().GetBestBlock()};
    view.SetBestBlock(previous);
    const auto block{Block({MakeTransactionRef(Spend(24'000))})};
    const auto hash{block.GetHash()};
    CBlockIndex index{block};
    index.phashBlock = &hash;
    index.pprev = chainstate.m_chain.Tip();
    index.nHeight = 3;
    BlockValidationState state;
    BOOST_CHECK(!chainstate.ConnectBlock(block, state, &index, view, true));
    BOOST_CHECK(state.GetResult() == BlockValidationResult::BLOCK_CONSENSUS);
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-blk-undo-size");
    const uint64_t budget{GetMaxBlockUndoBytes(Params().GetConsensus(), 3)};
    const uint64_t size{GetSerializeSize(Using<TxInUndoFormatter>(source.coin))};
    BOOST_CHECK_EQUAL(source.reads, (budget - 4) / size + 1);
    BOOST_CHECK_LT(source.reads, 900);
    BOOST_CHECK(chainstate.CoinsTip().GetBestBlock() == previous);
    BOOST_CHECK(!chainstate.CoinsTip().HaveCoin(COutPoint{block.vtx[0]->GetHash(), 0}));
}

BOOST_FIXTURE_TEST_CASE(legacy_records_checksums_and_truncation, UndoLimitSetup)
{
    Mine();
    LOCK(cs_main);
    auto& blockman{m_node.chainman->m_blockman};
    const auto block{Block()};
    const auto hash{block.GetHash()};
    CBlockIndex index{block};
    index.phashBlock = &hash;
    index.pprev = m_node.chainman->ActiveTip();
    index.nHeight = 2;
    CBlockUndo undo;
    undo.vtxundo.resize(1);
    undo.vtxundo[0].vprevout.emplace_back(CTxOut{0, LargeScript(9'957)}, 1, false);
    const uint64_t payload{GetSerializeSize(undo)};
    const uint64_t offset{blockman.GetBlockFileInfo(0)->nUndoSize};
    BlockValidationState state;
    BOOST_REQUIRE(blockman.WriteBlockUndo(undo, state, index));
    BOOST_CHECK_EQUAL(index.nUndoPos, offset + node::STORAGE_HEADER_BYTES);
    BOOST_CHECK_EQUAL(blockman.GetBlockFileInfo(0)->nUndoSize, offset + payload + node::UNDO_DATA_DISK_OVERHEAD);
    CBlockUndo decoded;
    BOOST_REQUIRE(blockman.ReadBlockUndo(decoded, index));
    BOOST_CHECK_EQUAL(GetSerializeSize(decoded), payload);
    BOOST_CHECK(decoded.vtxundo[0].vprevout[0].out.scriptPubKey == LargeScript(9'957));
    const auto path{m_args.GetBlocksDirPath() / "rev00000.dat"};
    std::array<std::byte, Obfuscation::KEY_SIZE> key{};
    { AutoFile file{fsbridge::fopen(m_args.GetBlocksDirPath() / "xor.dat", "rb")}; file >> key; }
    {
        AutoFile file{fsbridge::fopen(path, "rb"), Obfuscation{key}};
        file.seek(offset, SEEK_SET);
        MessageStartChars magic;
        uint32_t length;
        uint256 checksum;
        file >> magic >> length >> decoded >> checksum;
        BOOST_CHECK(magic == Params().MessageStart());
        BOOST_CHECK_EQUAL(length, payload);
        BOOST_CHECK(checksum == (HashWriter{} << index.pprev->GetBlockHash() << undo).GetHash());
    }
    const uint64_t checksum_pos{index.nUndoPos + payload};
    {
        AutoFile file{fsbridge::fopen(path, "rb+"), Obfuscation{key}};
        file.seek(checksum_pos, SEEK_SET);
        uint8_t byte;
        file >> byte;
        file.seek(checksum_pos, SEEK_SET);
        file << uint8_t(byte ^ 1);
        BOOST_REQUIRE_EQUAL(file.fclose(), 0);
    }
    BOOST_CHECK(!blockman.ReadBlockUndo(decoded, index));
    fs::resize_file(path, checksum_pos + 31);
    BOOST_CHECK(!blockman.ReadBlockUndo(decoded, index));
    DataStream malformed;
    WriteCompactSize(malformed, MAX_SIZE + 1);
    BOOST_CHECK_THROW(malformed >> decoded, std::ios_base::failure);
    DataStream truncated;
    WriteCompactSize(truncated, 1);
    WriteCompactSize(truncated, 1);
    BOOST_CHECK_THROW(truncated >> decoded, std::ios_base::failure);
}

BOOST_AUTO_TEST_SUITE_END()
