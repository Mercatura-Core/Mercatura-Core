// Copyright (c) 2026 The Mercatura developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Standalone investigation fixture; link with the ordinary unit-test objects.
// This verifies existing behavior. It does not implement an MCA-A-004 fix.
#include <arith_uint256.h>
#include <chainparams.h>
#include <consensus/tx_check.h>
#include <consensus/tx_verify.h>
#include <hash.h>
#include <node/blockstorage.h>
#include <policy/policy.h>
#include <pow.h>
#include <script/interpreter.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <undo.h>
#include <validation.h>
#include <boost/test/unit_test.hpp>
#include <cstdio>
#include <limits>

static CScript LargeScript()
{
    CScript script;
    for (int i = 0; i < 19; ++i) script << std::vector<unsigned char>(520, 0) << OP_DROP;
    return script << OP_TRUE;
}

static uint64_t UndoBytes(uint64_t inputs, uint64_t coin_size)
{
    const uint64_t transactions = (inputs + 23999) / 24000;
    SizeComputer size;
    WriteCompactSize(size, transactions);
    for (uint64_t i = 0; i < transactions; ++i) {
        const uint64_t count = std::min<uint64_t>(24000, inputs - i * 24000);
        WriteCompactSize(size, count);
        size.seek(count * coin_size);
    }
    return size.size();
}

BOOST_FIXTURE_TEST_CASE(b5_construction_and_sizes, BasicTestingSetup)
{
    const auto script = LargeScript();
    BOOST_REQUIRE_EQUAL(script.size(), 9957);
    BOOST_CHECK(!script.IsUnspendable());
    const CScriptWitness witness;
    ScriptError error;
    BOOST_REQUIRE(VerifyScript({}, script, &witness, STANDARD_SCRIPT_VERIFY_FLAGS, BaseSignatureChecker{}, &error));
    auto maximum = script;
    while (maximum.size() < MAX_SCRIPT_SIZE) maximum << OP_NOP;
    BOOST_REQUIRE(VerifyScript({}, maximum, &witness, STANDARD_SCRIPT_VERIFY_FLAGS, BaseSignatureChecker{}, &error));
    maximum << OP_NOP;
    BOOST_CHECK(!VerifyScript({}, maximum, &witness, STANDARD_SCRIPT_VERIFY_FLAGS, BaseSignatureChecker{}, &error));
    BOOST_CHECK(error == SCRIPT_ERR_SCRIPT_SIZE);

    const Coin coin{CTxOut{0, script}, 1000000, true};
    BOOST_CHECK(!coin.IsSpent());
    const uint64_t coin_size = GetSerializeSize(Using<TxInUndoFormatter>(coin));
    BOOST_CHECK_EQUAL(coin_size, 9964);
    DataStream encoded;
    encoded << Using<TxInUndoFormatter>(coin);
    Coin recovered;
    encoded >> Using<TxInUndoFormatter>(recovered);
    BOOST_CHECK(recovered.out.scriptPubKey == script);
    BOOST_CHECK_EQUAL(recovered.out.nValue, 0);
    BOOST_CHECK_EQUAL(recovered.nHeight, 1000000);

    CMutableTransaction spending;
    for (uint32_t i = 0; i < 24000; ++i) {
        spending.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), i});
    }
    spending.vout.emplace_back(0, CScript{} << OP_TRUE);
    const CTransaction tx{spending};
    TxValidationState tx_state;
    BOOST_REQUIRE_MESSAGE(CheckTransaction(tx, tx_state), tx_state.ToString());
    const uint64_t transaction_size = GetSerializeSize(TX_NO_WITNESS(tx));
    BOOST_CHECK_EQUAL(transaction_size, 984022);
    BOOST_CHECK_EQUAL(GetLegacySigOpCount(tx), 0);
    BOOST_CHECK_EQUAL(GetTransactionWitnessItems(tx), 0);

    CMutableTransaction funding;
    funding.vin.resize(1);
    funding.vin[0].scriptSig = CScript{} << int64_t{1000000};
    funding.vout.assign(100, CTxOut{0, script});
    const CTransaction funder{funding};
    TxValidationState funding_state;
    BOOST_REQUIRE_MESSAGE(CheckTransaction(funder, funding_state), funding_state.ToString());
    CCoinsView empty_view;
    CCoinsViewCache view{&empty_view};
    const COutPoint point{funder.GetHash(), 0};
    view.AddCoin(point, Coin{funder.vout[0], 1000000, true}, false);
    CMutableTransaction one;
    one.vin.emplace_back(point);
    one.vout.emplace_back(0, CScript{} << OP_TRUE);
    CAmount fee{-1};
    TxValidationState input_state;
    BOOST_REQUIRE(Consensus::CheckTxInputs(CTransaction{one}, input_state, view, 3153600, fee));
    BOOST_CHECK_EQUAL(fee, 0);
    BOOST_REQUIRE(VerifyScript(one.vin[0].scriptSig, view.AccessCoin(point).out.scriptPubKey,
        &witness, STANDARD_SCRIPT_VERIFY_FLAGS, BaseSignatureChecker{}, &error));

    ArgsManager args;
    const auto main = CreateChainParams(args, ChainType::MAIN);
    BOOST_CHECK_EQUAL(GetMaxBlockCapacityBytes(main->GetConsensus(), 3153599), 16ULL << 20);
    BOOST_CHECK_EQUAL(GetMaxBlockCapacityBytes(main->GetConsensus(), 3153600), 32ULL << 20);
    funding.vin[0].scriptSig = CScript{} << int64_t{3153600};
    funding.vout.assign(1, CTxOut{0, CScript{} << OP_TRUE});
    const uint64_t block_size = 80 + GetSizeOfCompactSize(21) + GetSerializeSize(TX_NO_WITNESS(CTransaction{funding})) + 20 * transaction_size;
    const uint64_t undo_size = UndoBytes(480000, coin_size);
    BOOST_CHECK_EQUAL(block_size, 19680586);
    BOOST_CHECK_EQUAL(undo_size, 4782720061ULL);
    BOOST_CHECK_GT(block_size, 16ULL << 20);
    BOOST_CHECK_LT(block_size, 32ULL << 20);
    BOOST_CHECK_GT(undo_size, std::numeric_limits<uint32_t>::max());

    // Conservative bound: max raw script + script code + amount + height + legacy dummy.
    constexpr uint64_t max_coin = 10000 + 2 + 10 + 5 + 1;
    const uint64_t upper16 = ((16ULL << 20) / 41) * (max_coin + 5) + 5;
    BOOST_CHECK_LT(upper16, std::numeric_limits<uint32_t>::max());
    uint64_t low{1}, high{480000};
    while (low < high) {
        const uint64_t middle = low + (high - low) / 2;
        if (UndoBytes(middle, coin_size) <= std::numeric_limits<uint32_t>::max()) low = middle + 1;
        else high = middle;
    }
    BOOST_CHECK_LE(UndoBytes(low - 1, coin_size), std::numeric_limits<uint32_t>::max());
    BOOST_CHECK_GT(UndoBytes(low, coin_size), std::numeric_limits<uint32_t>::max());
    BOOST_TEST_MESSAGE("script=" << script.size() << " input_undo=" << coin_size
        << " tx=" << transaction_size << " funding_tx=" << GetSerializeSize(TX_NO_WITNESS(funder))
        << " block=" << block_size << " undo=" << undo_size << " first_overflow_inputs=" << low
        << " undo_below=" << UndoBytes(low - 1, coin_size) << " undo_above=" << UndoBytes(low, coin_size)
        << " upper_bound_16MiB=" << upper16 << " sizeof_Coin=" << sizeof(Coin));
}

BOOST_FIXTURE_TEST_CASE(b5_small_legacy_undo_io, RegTestingSetup)
{
    CBlockHeader header = Params().GenesisBlock();
    header.hashPrevBlock = Params().GenesisBlock().GetHash();
    header.nVersion = 0x20000000;
    header.nTime += 150;
    PoWHashContext miner;
    bool solved{false};
    for (int i = 0; i < 64; ++i, ++header.nNonce) {
        if (CheckProofOfWork(header, Params().GetConsensus(), miner, 1)) { solved = true; break; }
    }
    BOOST_REQUIRE(solved);
    BlockValidationState accepted;
    BOOST_REQUIRE(m_node.chainman->ProcessNewBlockHeaders(std::span{&header, 1}, true, accepted));
    auto& blockman = m_node.chainman->m_blockman;
    CBlockUndo undo;
    undo.vtxundo.resize(1);
    undo.vtxundo[0].vprevout.emplace_back(CTxOut{0, LargeScript()}, 1, false);
    const uint64_t payload = GetSerializeSize(undo);
    CBlockIndex* index;
    {
        LOCK(cs_main);
        index = blockman.LookupBlockIndex(header.GetHash());
        BOOST_REQUIRE(index);
        BlockValidationState written;
        BOOST_REQUIRE(blockman.WriteBlockUndo(undo, written, *index));
        BOOST_CHECK_EQUAL(index->nUndoPos, node::STORAGE_HEADER_BYTES);
    }
    CBlockUndo recovered;
    BOOST_REQUIRE(blockman.ReadBlockUndo(recovered, *index));
    BOOST_REQUIRE_EQUAL(recovered.vtxundo.size(), 1);
    BOOST_REQUIRE_EQUAL(recovered.vtxundo[0].vprevout.size(), 1);
    BOOST_CHECK(recovered.vtxundo[0].vprevout[0].out.scriptPubKey == LargeScript());
    const auto path = m_args.GetBlocksDirPath() / "rev00000.dat";
    std::array<std::byte, Obfuscation::KEY_SIZE> key{};
    { AutoFile keyfile{fsbridge::fopen(m_args.GetBlocksDirPath() / "xor.dat", "rb")}; keyfile >> key; }
    {
        AutoFile record{fsbridge::fopen(path, "rb"), Obfuscation{key}};
        MessageStartChars magic;
        uint32_t length;
        CBlockUndo decoded;
        uint256 checksum;
        record >> magic >> length >> decoded >> checksum;
        BOOST_CHECK(magic == Params().MessageStart());
        BOOST_CHECK_EQUAL(length, payload);
        BOOST_CHECK(checksum == (HashWriter{} << index->pprev->GetBlockHash() << undo).GetHash());
    }
    {
        LOCK(cs_main);
        CBlockIndex second{header};
        second.pprev = index->pprev;
        second.nHeight = 1;
        second.nFile = 0;
        BlockValidationState written;
        BOOST_REQUIRE(blockman.WriteBlockUndo(undo, written, second));
        BOOST_CHECK_EQUAL(second.nUndoPos, payload + node::UNDO_DATA_DISK_OVERHEAD + node::STORAGE_HEADER_BYTES);
        BOOST_CHECK_EQUAL(blockman.GetBlockFileInfo(0)->nUndoSize, 2 * (payload + node::UNDO_DATA_DISK_OVERHEAD));
        CBlockUndo decoded;
        BOOST_REQUIRE(blockman.ReadBlockUndo(decoded, second));
    }
    FILE* file = fsbridge::fopen(path, "rb+");
    BOOST_REQUIRE(file);
    const uint64_t checksum_pos = WITH_LOCK(cs_main, return index->GetUndoPos().nPos) + payload;
    BOOST_REQUIRE_EQUAL(std::fseek(file, checksum_pos, SEEK_SET), 0);
    const int byte = std::fgetc(file);
    BOOST_REQUIRE(byte != EOF);
    BOOST_REQUIRE_EQUAL(std::fseek(file, checksum_pos, SEEK_SET), 0);
    BOOST_REQUIRE(std::fputc(byte ^ 1, file) != EOF);
    BOOST_REQUIRE_EQUAL(std::fclose(file), 0);
    BOOST_CHECK(!blockman.ReadBlockUndo(recovered, *index));
    fs::resize_file(path, checksum_pos + 31);
    BOOST_CHECK(!blockman.ReadBlockUndo(recovered, *index));
    BOOST_TEST_MESSAGE("legacy_payload=" << payload << " payload_offset=8 checksum/truncation rejected");
}

BOOST_FIXTURE_TEST_CASE(b5_counting_boundaries_and_malformed_vectors, BasicTestingSetup)
{
    constexpr uint64_t limit = std::numeric_limits<uint32_t>::max();
    for (const uint64_t value : {limit - 1, limit, limit + 1}) {
        SizeComputer size;
        size.seek(value);
        BOOST_CHECK_EQUAL(size.size(), value);
        BOOST_CHECK_EQUAL(std::in_range<uint32_t>(size.size()), value <= limit);
    }
    DataStream malformed;
    WriteCompactSize(malformed, MAX_SIZE + 1);
    CBlockUndo undo;
    BOOST_CHECK_THROW(malformed >> undo, std::ios_base::failure);
    DataStream truncated;
    WriteCompactSize(truncated, 1);
    WriteCompactSize(truncated, 1);
    BOOST_CHECK_THROW(truncated >> undo, std::ios_base::failure);
}
