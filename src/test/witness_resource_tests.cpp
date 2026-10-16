// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <blockencodings.h>
#include <blockchunk.h>
#include <chainparams.h>
#include <consensus/tx_check.h>
#include <consensus/validation.h>
#include <consensus/merkle.h>
#include <core_io.h>
#include <crypto/sha256.h>
#include <key.h>
#include <interfaces/mining.h>
#include <node/blockstorage.h>
#include <node/miner.h>
#include <pow.h>
#include <psbt.h>
#include <rpc/server.h>
#include <script/interpreter.h>
#include <script/signingprovider.h>
#include <script/solver.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <test/util/logging.h>
#include <test/util/net.h>
#include <test/util/txmempool.h>
#include <util/fs.h>
#include <util/time.h>
#include <util/strencodings.h>
#include <validation.h>
#include <validationinterface.h>

#include <boost/test/unit_test.hpp>

namespace {
CMutableTransaction WitnessTx(const std::vector<size_t>& counts)
{
    CMutableTransaction tx;
    tx.vin.resize(counts.size());
    for (size_t i{0}; i < counts.size(); ++i) {
        tx.vin[i].prevout.n = i;
        tx.vin[i].scriptWitness.stack.resize(counts[i]);
    }
    tx.vout.emplace_back(0, CScript{} << OP_TRUE);
    return tx;
}

CBlock ResourceBlock(size_t total)
{
    CMutableTransaction cb;
    cb.vin.resize(1);
    cb.vin[0].scriptSig = CScript{} << OP_0 << OP_0;
    cb.vin[0].scriptWitness.stack = {std::vector<unsigned char>(32)};
    cb.vout.emplace_back(0, CScript{} << OP_TRUE);
    CBlock block;
    block.vtx.push_back(MakeTransactionRef(std::move(cb)));
    std::vector<size_t> counts;
    for (size_t left{total - 1}; left;) {
        const size_t count{std::min<size_t>(left, Consensus::MAX_WITNESS_STACK_ITEMS)};
        counts.push_back(count);
        left -= count;
    }
    block.vtx.push_back(MakeTransactionRef(WitnessTx(counts)));
    return block;
}

bool Reason(const std::ios_base::failure& error, const char* reason)
{
    return std::string{error.what()}.find(reason) != std::string::npos;
}
} // namespace

BOOST_FIXTURE_TEST_SUITE(witness_resource_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(network_and_schedule)
{
    for (const auto network : {ChainType::MAIN, ChainType::TESTNET, ChainType::TESTNET4, ChainType::SIGNET, ChainType::REGTEST}) {
        const auto params = CreateChainParams(m_args, network);
        BOOST_CHECK_EQUAL(Consensus::WitnessLimitsActive(params->GetConsensus(), 0), network == ChainType::MAIN);
        BOOST_CHECK_EQUAL(WitnessParseBudget::ForInspection(params->GetConsensus()).enabled, network == ChainType::MAIN);
    }
    const std::pair<int64_t, uint64_t> boundaries[] = {
        {0, 16384}, {1'051'200, 32768}, {2'102'400, 65536}, {4'204'800, 262144},
        {6'307'200, 1048576}, {7'358'400, 2097152}, {8'409'600, 4194304}};
    for (const auto& [height, limit] : boundaries) {
        BOOST_CHECK_EQUAL(Consensus::GetMaxBlockWitnessItems(Params().GetConsensus(), height), limit);
        if (height) BOOST_CHECK_EQUAL(Consensus::GetMaxBlockWitnessItems(Params().GetConsensus(), height - 1), limit / 2);
    }
    BOOST_CHECK_EQUAL(Consensus::GetMaxBlockWitnessItems(Params().GetConsensus(), std::numeric_limits<int64_t>::max()), 4194304);
    auto future = Params().GetConsensus();
    future.nWitnessResourceHeight = 500;
    BOOST_CHECK(!WitnessParseBudget::ForInspection(future).enabled);
    BOOST_CHECK(!WitnessParseBudget::ForBlock(future, 499).enabled);
    BOOST_CHECK(WitnessParseBudget::ForBlock(future, 500).enabled);
}

BOOST_AUTO_TEST_CASE(input_count_and_preallocation)
{
    for (const size_t count : {1002, 1003, 1004}) {
        CTransaction tx{WitnessTx({count})};
        TxValidationState state;
        BOOST_CHECK_EQUAL(CheckTransactionWitnessResources(tx, state), count <= 1003);
        if (count > 1003) BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-witness-stack-count");
    }
    DataStream prefix;
    WriteCompactSize(prefix, 1004);
    auto budget = WitnessParseBudget::ForBlock(Params().GetConsensus(), 1);
    std::vector<std::vector<unsigned char>> stack;
    BOOST_CHECK_EXCEPTION(prefix >> WitnessParseParams{&budget}(Using<WitnessStackFormatter>(stack)),
        std::ios_base::failure, [](const auto& e) { return Reason(e, "bad-witness-stack-count"); });
    BOOST_CHECK_EQUAL(stack.capacity(), 0);
    BOOST_CHECK_EQUAL(budget.remaining, 16384);

    DataStream noncanonical;
    noncanonical << uint8_t{253} << uint16_t{1};
    BOOST_CHECK_EXCEPTION(noncanonical >> WitnessParseParams{&budget}(Using<WitnessStackFormatter>(stack)),
        std::ios_base::failure, [](const auto& e) { return Reason(e, "non-canonical"); });
    BOOST_CHECK_EQUAL(stack.capacity(), 0);
}

BOOST_AUTO_TEST_CASE(aggregate_roundtrip_and_rollback)
{
    for (const size_t total : {16383, 16384, 16385}) {
        const auto block = ResourceBlock(total);
        BlockValidationState state;
        BOOST_CHECK_EQUAL(CheckBlockWitnessResources(block, state, Params().GetConsensus(), 1), total <= 16384);
        if (total > 16384) BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-blk-witness-items");

        DataStream stream;
        stream << TX_WITH_WITNESS(block);
        const std::vector<std::byte> bytes{stream.begin(), stream.end()};
        CBlock decoded;
        auto budget = WitnessParseBudget::ForBlock(Params().GetConsensus(), 1);
        if (total <= 16384) {
            stream >> WitnessParseParams{&budget}(TX_WITH_WITNESS(decoded));
            BOOST_CHECK_EQUAL(budget.remaining, 16384 - total);
            BOOST_CHECK(stream.empty());
            DataStream again;
            again << TX_WITH_WITNESS(decoded);
            BOOST_CHECK(std::equal(bytes.begin(), bytes.end(), again.begin(), again.end()));
        } else {
            BOOST_CHECK_EXCEPTION(stream >> WitnessParseParams{&budget}(TX_WITH_WITNESS(decoded)),
                std::ios_base::failure, [](const auto& e) { return Reason(e, "bad-blk-witness-items"); });
            BOOST_CHECK_EQUAL(budget.remaining, 16384); // Includes successful preceding coinbase parsing.
            DataStream retry{bytes};
            auto larger = WitnessParseBudget::ForBlock(Params().GetConsensus(), 1'051'200);
            retry >> WitnessParseParams{&larger}(TX_WITH_WITNESS(decoded));
            BOOST_CHECK_EQUAL(larger.remaining, 32768 - total);
        }
    }

    // Multiple occurrences of the same reference are charged separately.
    auto block = ResourceBlock(8001);
    block.vtx.push_back(block.vtx[1]);
    block.vtx.push_back(block.vtx[1]);
    BlockValidationState state;
    BOOST_CHECK(!CheckBlockWitnessResources(block, state, Params().GetConsensus(), 1));

    // Also prove that the parse budget spans separate transactions.
    block = ResourceBlock(8001);
    block.vtx.push_back(MakeTransactionRef(WitnessTx(std::vector<size_t>(9, 1003))));
    DataStream wire;
    wire << TX_WITH_WITNESS(block);
    auto budget = WitnessParseBudget::ForBlock(Params().GetConsensus(), 1);
    CBlock decoded;
    BOOST_CHECK_EXCEPTION(wire >> WitnessParseParams{&budget}(TX_WITH_WITNESS(decoded)),
        std::ios_base::failure, [](const auto& e) { return Reason(e, "bad-blk-witness-items"); });
    BOOST_CHECK_EQUAL(budget.remaining, 16384);
}

BOOST_AUTO_TEST_CASE(header_first_and_legacy_history)
{
    CBlock block;
    block.vtx.push_back(MakeTransactionRef(WitnessTx({1004})));
    DataStream encoded;
    encoded << TX_WITH_WITNESS(block);
    const std::vector<std::byte> bytes{encoded.begin(), encoded.end()};
    CBlock decoded;
    BOOST_CHECK_EXCEPTION(ReadBlockWithWitnessLimits(encoded, decoded, Params().GetConsensus(),
        [](const CBlockHeader&) -> std::optional<int> { return std::nullopt; }),
        std::ios_base::failure, [](const auto& e) { return Reason(e, "unknown-block-parent"); });
    BOOST_CHECK(decoded.vtx.empty());
    BOOST_CHECK_EQUAL(encoded.size(), bytes.size() - 80);

    const auto testnet = CreateChainParams(m_args, ChainType::TESTNET);
    DataStream historical{bytes};
    ReadBlockWithWitnessLimits(historical, decoded, testnet->GetConsensus(),
        [](const CBlockHeader&) -> std::optional<int> { return std::nullopt; });
    BOOST_CHECK_EQUAL(decoded.vtx[0]->vin[0].scriptWitness.stack.size(), 1004);
    BlockValidationState state;
    BOOST_CHECK(CheckBlockWitnessResources(decoded, state, testnet->GetConsensus(), 1));
}

BOOST_AUTO_TEST_CASE(item_size_large)
{
    std::vector<std::vector<unsigned char>> stack{std::vector<unsigned char>(Consensus::MAX_WITNESS_ITEM_BYTES)};
    BOOST_CHECK(Consensus::WitnessStackViolation(stack) == nullptr);
    DataStream stream;
    stream << stack;
    auto budget = WitnessParseBudget::ForBlock(Params().GetConsensus(), 6'307'200);
    std::vector<std::vector<unsigned char>> decoded;
    stream >> WitnessParseParams{&budget}(Using<WitnessStackFormatter>(decoded));
    BOOST_CHECK_EQUAL(decoded[0].size(), Consensus::MAX_WITNESS_ITEM_BYTES);
    stack[0].resize(Consensus::MAX_WITNESS_ITEM_BYTES + 1);
    BOOST_CHECK_EQUAL(Consensus::WitnessStackViolation(stack), "bad-witness-item-size");

    DataStream prefix;
    WriteCompactSize(prefix, 1);
    WriteCompactSize(prefix, Consensus::MAX_WITNESS_ITEM_BYTES + 1);
    auto failed_budget = WitnessParseBudget::ForBlock(Params().GetConsensus(), 1);
    BOOST_CHECK_EXCEPTION(prefix >> WitnessParseParams{&failed_budget}(Using<WitnessStackFormatter>(decoded)),
        std::ios_base::failure, [](const auto& e) { return Reason(e, "size too large"); });
    BOOST_CHECK_EQUAL(failed_budget.remaining, 16384);
}

BOOST_AUTO_TEST_CASE(chunked_file_decoding)
{
    // Exercise the completed chunk-file decoder at the real 4 MiB launch cap.
    constexpr int height{0};
    for (const size_t total : {16384, 16385}) {
        auto block = ResourceBlock(total);
        CMutableTransaction tx{*block.vtx[1]};
        tx.vin[0].scriptWitness.stack[0].resize(4'000'000);
        block.vtx[1] = MakeTransactionRef(std::move(tx));
        DataStream wire;
        wire << TX_WITH_WITNESS(block);
        BOOST_REQUIRE(wire.size() > MAX_BLOCK_SERIALIZED_SIZE);
        BOOST_REQUIRE(wire.size() <= GetMaxBlockCapacityBytes(Params().GetConsensus(), height));
        MercaturaBlockMeta meta;
        meta.block_hash = block.GetHash();
        meta.total_size = wire.size();
        meta.chunk_size = MERCATURA_BLOCK_CHUNK_SIZE;
        meta.chunk_count = GetMercaturaBlockChunkCount(meta.total_size);
        BOOST_REQUIRE(IsMercaturaBlockMetaStructurallyValid(meta));
        AutoFile completed{std::tmpfile()};
        BOOST_REQUIRE(!completed.IsNull());
        for (uint32_t i{0}; i < meta.chunk_count; ++i) {
            const size_t offset{i * MERCATURA_BLOCK_CHUNK_SIZE};
            const size_t size{GetMercaturaBlockChunkPayloadSize(meta.total_size, i)};
            MercaturaBlockChunk chunk;
            chunk.block_hash = meta.block_hash;
            chunk.chunk_index = i;
            const auto bytes = std::span{wire}.subspan(offset, size);
            chunk.data.assign(reinterpret_cast<const unsigned char*>(bytes.data()),
                reinterpret_cast<const unsigned char*>(bytes.data()) + bytes.size());
            DataStream message;
            message << chunk;
            MercaturaBlockChunk received;
            message >> received;
            BOOST_REQUIRE_EQUAL(received.data.size(), size);
            completed.write(std::as_bytes(std::span{received.data}));
        }
        completed.seek(0, SEEK_SET);
        CBlock decoded;
        auto decode = [&] {
            ReadBlockWithWitnessLimits(completed, decoded, Params().GetConsensus(),
                [&](const CBlockHeader& header) -> std::optional<int> {
                    BOOST_REQUIRE(header.GetHash() == meta.block_hash);
                    return height;
                });
        };
        if (total == 16384) {
            BOOST_CHECK_NO_THROW(decode());
            BOOST_CHECK_EQUAL(completed.tell(), meta.total_size);
            BlockValidationState state;
            BOOST_CHECK(CheckBlockWitnessResources(decoded, state, Params().GetConsensus(), height));
        } else {
            BOOST_CHECK_EXCEPTION(decode(), std::ios_base::failure,
                [](const auto& e) { return Reason(e, "bad-blk-witness-items"); });
        }
    }
}

BOOST_AUTO_TEST_CASE(script_envelopes)
{
    const script_verify_flags flags{SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_TAPROOT};
    BaseSignatureChecker checker;
    ScriptError error;
    auto tx = WitnessTx({1003});
    for (int version{3}; version <= 16; ++version) {
        const CScript unknown{CScript{} << CScript::EncodeOP_N(version) << std::vector<unsigned char>(32, 1)};
        BOOST_CHECK(VerifyScript({}, unknown, &tx.vin[0].scriptWitness, flags, checker, &error));
    }

    CKey key;
    key.MakeNewKey(true);
    const XOnlyPubKey internal{key.GetPubKey()};
    for (const auto leaf : {uint8_t{TAPROOT_LEAF_TAPSCRIPT}, uint8_t{0xc2}}) {
        CScript script;
        for (int i{0}; i < 499; ++i) script << OP_2DROP;
        script << OP_DROP;
        TaprootBuilder builder;
        const std::vector<unsigned char> script_bytes{script.begin(), script.end()};
        builder.Add(0, script_bytes, leaf, true).Finalize(internal);
        const auto spend = builder.GetSpendData();
        CScriptWitness witness;
        witness.stack.resize(1000);
        witness.stack[0] = {1};
        witness.stack.push_back(script_bytes);
        witness.stack.push_back(*spend.scripts.at({script_bytes, leaf}).begin());
        witness.stack.push_back({ANNEX_TAG});
        BOOST_CHECK_EQUAL(witness.stack.size(), 1003);
        BOOST_CHECK(Consensus::WitnessStackViolation(witness.stack) == nullptr);
        BOOST_CHECK(VerifyScript({}, GetScriptForDestination(builder.GetOutput()), &witness, flags, checker, &error));
    }
    CScript script{CScript{} << OP_RESERVED}; // OP_SUCCESS80 in Tapscript.
    const std::vector<unsigned char> script_bytes{script.begin(), script.end()};
    TaprootBuilder builder;
    builder.Add(0, script_bytes, TAPROOT_LEAF_TAPSCRIPT, true).Finalize(internal);
    CScriptWitness witness;
    witness.stack.resize(1000);
    witness.stack[0].resize(521); // OP_SUCCESS bypasses the interpreter's 520-byte check.
    witness.stack.push_back(script_bytes);
    witness.stack.push_back(*builder.GetSpendData().scripts.at({script_bytes, TAPROOT_LEAF_TAPSCRIPT}).begin());
    witness.stack.push_back({ANNEX_TAG});
    BOOST_CHECK(VerifyScript({}, GetScriptForDestination(builder.GetOutput()), &witness, flags, checker, &error));
    BOOST_CHECK(Consensus::WitnessStackViolation(witness.stack) == nullptr);
    witness.stack.insert(witness.stack.begin(), std::vector<unsigned char>{});
    BOOST_CHECK_EQUAL(Consensus::WitnessStackViolation(witness.stack), "bad-witness-stack-count");

    const CScript v0_script{CScript{} << OP_TRUE};
    uint256 script_hash;
    CSHA256{}.Write(v0_script.data(), v0_script.size()).Finalize(script_hash.begin());
    const CScript redeem{CScript{} << OP_0 << ToByteVector(script_hash)};
    witness.stack = {std::vector<unsigned char>{v0_script.begin(), v0_script.end()}};
    BOOST_CHECK(VerifyScript({}, redeem, &witness, flags, checker, &error));
    BOOST_CHECK(VerifyScript(CScript{} << std::vector<unsigned char>{redeem.begin(), redeem.end()},
        GetScriptForDestination(ScriptHash{redeem}), &witness, flags, checker, &error));
}

BOOST_AUTO_TEST_CASE(psbt_mainnet_and_historical_decode)
{
    auto tx = WitnessTx({0});
    PartiallySignedTransaction psbt{tx};
    psbt.inputs[0].witness_utxo = CTxOut{0, CScript{} << OP_1 << std::vector<unsigned char>{0x4e, 0x73}};
    psbt.inputs[0].final_script_witness.stack.resize(1003);
    CMutableTransaction result;
    BOOST_CHECK(FinalizeAndExtractPSBT(psbt, result));
    psbt.inputs[0].final_script_witness.stack.resize(1004);
    BOOST_CHECK(!FinalizeAndExtractPSBT(psbt, result));
    DataStream encoded;
    encoded << psbt;
    std::string error;
    PartiallySignedTransaction decoded;
    BOOST_CHECK(!DecodeRawPSBT(decoded, std::span<const std::byte>{encoded.data(), encoded.size()}, error));
    BOOST_CHECK(error.find("bad-witness-stack-count") != std::string::npos);
    SelectParams(ChainType::TESTNET);
    BOOST_CHECK(DecodeRawPSBT(decoded, std::span<const std::byte>{encoded.data(), encoded.size()}, error));
    BOOST_CHECK(FinalizeAndExtractPSBT(decoded, result));
    SelectParams(ChainType::MAIN);
}

BOOST_AUTO_TEST_SUITE_END()

namespace {
struct WitnessChainSetup : TestingSetup {
    WitnessChainSetup() : TestingSetup{ChainType::MAIN} {}
};
} // namespace

BOOST_FIXTURE_TEST_SUITE(witness_resource_tests_chain, WitnessChainSetup)

BOOST_AUTO_TEST_CASE(mainnet_connection_templates_disk_and_reindex)
{
    auto& chainman = *m_node.chainman;
    PoWHashContext pow_context;
    SetMockTime(Params().GenesisBlock().Time());
    const CScript unknown{CScript{} << OP_3 << std::vector<unsigned char>(32, 1)};
    node::BlockAssembler::Options options;
    options.coinbase_output_script = unknown;
    options.include_dummy_extranonce = true;

    auto make_block = [&](const std::vector<CMutableTransaction>& transactions) {
        SetMockTime(GetTime() + 150);
        CBlock block = node::BlockAssembler{chainman.ActiveChainstate(), nullptr, options}.CreateNewBlock()->block;
        for (const auto& tx : transactions) block.vtx.push_back(MakeTransactionRef(tx));
        node::RegenerateCommitments(block, chainman);
        block.fChecked = false;
        block.m_checked_merkle_root = false;
        block.m_checked_witness_commitment = false;
        return block;
    };
    auto solve = [&](CBlock& block) {
        unsigned int attempts{0};
        while (!CheckProofOfWork(block, chainman.GetConsensus(), pow_context) && ++attempts < 10000) ++block.nNonce;
        BOOST_REQUIRE(attempts < 10000);
    };
    CTransactionRef first_coinbase;
    for (int height{1}; height <= COINBASE_MATURITY; ++height) {
        auto block = make_block({});
        solve(block);
        BOOST_REQUIRE(chainman.ProcessNewBlock(std::make_shared<const CBlock>(block), true, true, nullptr));
        BOOST_REQUIRE_EQUAL(chainman.ActiveChain().Height(), height);
        if (height == 1) first_coinbase = block.vtx[0];
    }

    // A real on-chain, matured unknown-version output funds all subsequent spends.
    CMutableTransaction funding;
    funding.vin.emplace_back(COutPoint{first_coinbase->GetHash(), 0});
    const CAmount amount{first_coinbase->vout[0].nValue / 18};
    for (int i{0}; i < 18; ++i) funding.vout.emplace_back(amount, unknown);
    auto funding_block = make_block({funding});
    solve(funding_block);
    BOOST_REQUIRE(chainman.ProcessNewBlock(std::make_shared<const CBlock>(funding_block), true, true, nullptr));
    BOOST_REQUIRE_EQUAL(chainman.ActiveChain().Height(), 101);

    // Templates must skip the 17th count-heavy transaction, even though bytes fit.
    std::vector<CMutableTransaction> template_spends;
    for (uint32_t i{0}; i < 17; ++i) {
        auto spend = WitnessTx({1003});
        spend.vin[0].prevout = COutPoint{funding.GetHash(), i};
        spend.vout[0].nValue = amount - 10000;
        template_spends.push_back(spend);
        LOCK(m_node.mempool->cs);
        TryAddToMempool(*m_node.mempool, TestMemPoolEntryHelper{}.Fee(10000).SigOpsCost(0).FromTx(spend));
    }
    auto templ = node::BlockAssembler{chainman.ActiveChainstate(), m_node.mempool.get(), options}.CreateNewBlock();
    uint64_t template_items{0};
    for (const auto& tx : templ->block.vtx) template_items += GetTransactionWitnessItems(*tx);
    BOOST_CHECK_EQUAL(template_items, 16049);
    BOOST_CHECK_EQUAL(templ->block.vtx.size(), 17);

    std::vector<size_t> counts(16, 1003);
    counts.push_back(335);
    auto spend = WitnessTx(counts);
    for (uint32_t i{0}; i < 17; ++i) spend.vin[i].prevout = COutPoint{funding.GetHash(), i};
    spend.vout[0].nValue = 17 * amount - 10000;
    auto valid = make_block({spend}); // Exactly 16,384 including coinbase.
    // Exercise the actual proposal RPC, with its contextual raw-block decoder.
    m_node.mining = interfaces::MakeMining(m_node, /*wait_loaded=*/false);
    auto proposal = [&](const CBlock& block) {
        DataStream wire;
        wire << TX_WITH_WITNESS(block);
        UniValue request_object{UniValue::VOBJ};
        request_object.pushKV("mode", "proposal");
        request_object.pushKV("data", HexStr(wire));
        JSONRPCRequest request;
        request.context = &m_node;
        request.strMethod = "getblocktemplate";
        request.params = UniValue{UniValue::VARR};
        request.params.push_back(request_object);
        if (RPCIsInWarmup(nullptr)) SetRPCWarmupFinished();
        return tableRPC.execute(request);
    };
    BOOST_CHECK(proposal(valid).isNull());
    {
        LOCK(cs_main);
        BOOST_CHECK(TestBlockValidity(chainman.ActiveChainstate(), valid, false, true).IsValid());
        BOOST_CHECK(TestBlockValidity(chainman.ActiveChainstate(), valid, false, true).IsValid()); // Warm commitment/script caches.
    }
    auto over_spend = spend;
    over_spend.vin.back().scriptWitness.stack.emplace_back();
    auto over = make_block({over_spend});
    BOOST_CHECK_EXCEPTION(proposal(over), UniValue, [](const UniValue& error) {
        const auto& code = error.find_value("code");
        return code.isNum() && code.getValStr() == "-22";
    });
    {
        LOCK(cs_main);
        auto state = TestBlockValidity(chainman.ActiveChainstate(), over, false, true);
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-blk-witness-items");
        BOOST_CHECK(state.GetResult() == BlockValidationResult::BLOCK_CONSENSUS);
        // Direct connection deliberately omits ContextualCheckBlock. The new
        // consensus rule must be independently enforced here during rebuilds.
        const uint256 hash{over.GetHash()};
        CBlockIndex index{over};
        index.phashBlock = &hash;
        index.pprev = chainman.ActiveChain().Tip();
        index.nHeight = index.pprev->nHeight + 1;
        CCoinsViewCache view{&chainman.ActiveChainstate().CoinsTip()};
        BlockValidationState direct;
        BOOST_CHECK(!chainman.ActiveChainstate().ConnectBlock(over, direct, &index, view, true));
        BOOST_CHECK_EQUAL(direct.GetRejectReason(), "bad-blk-witness-items");
        over.fChecked = true;
        over.m_checked_witness_commitment = true;
        state = TestBlockValidity(chainman.ActiveChainstate(), over, false, true);
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-blk-witness-items");
        direct = BlockValidationState{};
        BOOST_CHECK(!chainman.ActiveChainstate().ConnectBlock(over, direct, &index, view, true));
        BOOST_CHECK_EQUAL(direct.GetRejectReason(), "bad-blk-witness-items");
    }
    const auto mempool_result = chainman.ProcessTransaction(MakeTransactionRef(over_spend), true);
    BOOST_CHECK_EQUAL(mempool_result.m_state.GetRejectReason(), "tx-witness-budget");
    BOOST_CHECK(mempool_result.m_state.GetResult() == TxValidationResult::TX_MEMPOOL_POLICY);
    auto bad_shape = WitnessTx({1004});
    const auto malformed_result = chainman.ProcessTransaction(MakeTransactionRef(bad_shape), true);
    BOOST_CHECK_EQUAL(malformed_result.m_state.GetRejectReason(), "bad-witness-stack-count");

    // Exercise the real CMPCTBLOCK/BLOCKTXN handlers. Coinbase is prefilled,
    // one 1,003-item transaction is in the mempool, and the missing transaction
    // consumes the remaining 15,380 items. An unauthenticated alternate witness
    // with 15,381 items must fail during parsing, before reconstruction/validation.
    auto missing_counts = counts;
    missing_counts.erase(missing_counts.begin());
    auto newly_supplied = WitnessTx(missing_counts);
    for (uint32_t i{0}; i < 16; ++i) newly_supplied.vin[i].prevout = COutPoint{funding.GetHash(), i + 1};
    newly_supplied.vout[0].nValue = 16 * amount - 10000;
    auto mixed = make_block({template_spends[0], newly_supplied});
    solve(mixed);
    {
        LOCK(NetEventsInterface::g_msgproc_mutex);
        auto& connman = static_cast<ConnmanTestMsg&>(*m_node.connman);
        CNode peer{0, nullptr, CAddress{}, 0, 0, CAddress{}, "", ConnectionType::OUTBOUND_FULL_RELAY, false, 0};
        connman.Handshake(peer, true, ServiceFlags(NODE_NETWORK | NODE_WITNESS),
            ServiceFlags(NODE_NETWORK | NODE_WITNESS), PROTOCOL_VERSION, true);
        auto deliver = [&](CSerializedNetMsg message) {
            connman.FlushSendBuffer(peer);
            BOOST_REQUIRE(connman.ReceiveMsgFrom(peer, std::move(message)));
            peer.fPauseSend = false;
            connman.ProcessMessagesOnce(peer);
        };
        {
            ASSERT_DEBUG_LOG("sending getblocktxn");
            deliver(NetMsg::Make(NetMsgType::CMPCTBLOCK, CBlockHeaderAndShortTxIDs{mixed, 0}));
        }
        newly_supplied.vin.back().scriptWitness.stack.emplace_back();
        BlockTransactions response;
        response.blockhash = mixed.GetHash();
        response.txn = {MakeTransactionRef(newly_supplied)};
        {
            ASSERT_DEBUG_LOG("Exception 'bad-blk-witness-items");
            deliver(NetMsg::Make(NetMsgType::BLOCKTXN, response));
        }
        BOOST_CHECK_EQUAL(chainman.ActiveChain().Height(), 101);
        BOOST_CHECK(!WITH_LOCK(cs_main, return chainman.m_blockman.LookupBlockIndex(mixed.GetHash())->nStatus & BLOCK_FAILED_VALID));
        m_node.peerman->FinalizeNode(peer);
    }

    // Compact reconstruction charges the extra-pool cached transaction as well.
    CBlockHeaderAndShortTxIDs compact{valid, 0};
    PartiallyDownloadedBlock partial{m_node.mempool.get()};
    const std::vector<std::pair<Wtxid, CTransactionRef>> extra{{valid.vtx[1]->GetWitnessHash(), valid.vtx[1]}};
    BOOST_REQUIRE(partial.InitData(compact, extra, 16384) == READ_STATUS_OK);
    BOOST_REQUIRE(partial.WitnessItemsRemaining().has_value());
    BOOST_CHECK_EQUAL(*partial.WitnessItemsRemaining(), 0);
    CBlock reconstructed;
    BOOST_CHECK(partial.FillBlock(reconstructed, {}, true) == READ_STATUS_OK);
    PartiallyDownloadedBlock missing{m_node.mempool.get()};
    BOOST_REQUIRE(missing.InitData(compact, {}, 16384) == READ_STATUS_OK);
    BOOST_REQUIRE(missing.WitnessItemsRemaining().has_value());
    auto missing_budget = WitnessParseBudget::ForBlock(Params().GetConsensus(), 102);
    missing_budget.remaining = *missing.WitnessItemsRemaining();
    DataStream missing_wire;
    missing_wire << TX_WITH_WITNESS(std::vector<CTransactionRef>{valid.vtx[1]});
    std::vector<CTransactionRef> response;
    missing_wire >> WitnessParseParams{&missing_budget}(TX_WITH_WITNESS(response));
    BOOST_CHECK_EQUAL(missing_budget.remaining, 0);
    BOOST_CHECK(missing.FillBlock(reconstructed, response, true) == READ_STATUS_OK);

    // An unauthenticated over-count alternate witness must not poison the valid header.
    valid.fChecked = valid.m_checked_merkle_root = valid.m_checked_witness_commitment = false;
    solve(valid);
    auto mutated = valid;
    mutated.vtx[1] = MakeTransactionRef(over_spend); // Same txid; unchanged coinbase commitment.
    {
        LOCK(cs_main);
        const auto state = TestBlockValidity(chainman.ActiveChainstate(), mutated, false, true);
        BOOST_CHECK(state.GetResult() == BlockValidationResult::BLOCK_MUTATED);
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-witness-merkle-match");
    }
    mutated.fChecked = mutated.m_checked_merkle_root = mutated.m_checked_witness_commitment = false;
    BOOST_CHECK(!chainman.ProcessNewBlock(std::make_shared<const CBlock>(mutated), true, true, nullptr));
    valid.fChecked = valid.m_checked_merkle_root = valid.m_checked_witness_commitment = false;
    BOOST_REQUIRE(chainman.ProcessNewBlock(std::make_shared<const CBlock>(valid), true, true, nullptr));
    BOOST_REQUIRE_EQUAL(chainman.ActiveChain().Height(), 102);
    const uint256 expected_tip{valid.GetHash()};

    CBlock disk;
    const CBlockIndex* index{WITH_LOCK(cs_main, return chainman.ActiveChain().Tip())};
    BOOST_REQUIRE(chainman.m_blockman.ReadBlock(disk, *index));
    BOOST_CHECK(disk.GetHash() == expected_tip);
    BOOST_CHECK_EQUAL(GetTransactionWitnessItems(*disk.vtx[1]), 16383);

    // Level four disconnects/reconnects real blocks, exercising direct ConnectBlock.
    {
        LOCK(cs_main);
        CVerifyDB verifier{chainman.GetNotifications()};
        BOOST_CHECK(verifier.VerifyDB(chainman.ActiveChainstate(), chainman.GetConsensus(),
            chainman.ActiveChainstate().CoinsTip(), 4, 2) == VerifyDBResult::SUCCESS);
    }

    // Recreate the in-memory index and coins view, then genuinely import the on-disk chain.
    chainman.ActiveChainstate().ForceFlushStateToDisk();
    m_node.validation_signals->SyncWithValidationInterfaceQueue();
    m_node.mining.reset();
    m_node.peerman.reset();
    m_node.connman.reset();
    m_node.chainman.reset();
    m_make_chainman();
    LoadVerifyActivateChainstate();
    BOOST_REQUIRE_EQUAL(m_node.chainman->ActiveChain().Height(), 0);
    FlatFilePos position{0, 0};
    std::multimap<uint256, FlatFilePos> unknown_parents;
    AutoFile file{m_node.chainman->m_blockman.OpenBlockFile(position, /*fReadOnly=*/true)};
    BOOST_REQUIRE(!file.IsNull());
    m_node.chainman->LoadExternalBlockFile(file, &position, &unknown_parents);
    BlockValidationState activation;
    BOOST_REQUIRE(m_node.chainman->ActiveChainstate().ActivateBestChain(activation));
    BOOST_CHECK_EQUAL(m_node.chainman->ActiveChain().Height(), 102);
    BOOST_CHECK(m_node.chainman->ActiveChain().Tip()->GetBlockHash() == expected_tip);
    {
        LOCK(cs_main);
        BOOST_CHECK(m_node.chainman->ActiveChainstate().CoinsTip().HaveCoin(COutPoint{valid.vtx[1]->GetHash(), 0}));
        BOOST_CHECK(!m_node.chainman->ActiveChainstate().CoinsTip().HaveCoin(COutPoint{funding.GetHash(), 0}));
    }
}

BOOST_AUTO_TEST_SUITE_END()

namespace {
struct WitnessNetSetup : TestingSetup {
    WitnessNetSetup() : TestingSetup{ChainType::MAIN} {}
};
} // namespace

BOOST_FIXTURE_TEST_SUITE(witness_resource_tests_net, WitnessNetSetup)

BOOST_AUTO_TEST_CASE(ordinary_block_ingestion)
{
    LOCK(NetEventsInterface::g_msgproc_mutex);
    auto& connman = static_cast<ConnmanTestMsg&>(*m_node.connman);
    CNode peer{0, nullptr, CAddress{}, 0, 0, CAddress{}, "", ConnectionType::OUTBOUND_FULL_RELAY, false, 0};
    connman.Handshake(peer, true, ServiceFlags(NODE_NETWORK | NODE_WITNESS),
        ServiceFlags(NODE_NETWORK | NODE_WITNESS), PROTOCOL_VERSION, true);
    auto send_block = [&](const CBlock& block) {
        connman.FlushSendBuffer(peer);
        BOOST_REQUIRE(connman.ReceiveMsgFrom(peer, NetMsg::Make(NetMsgType::BLOCK, TX_WITH_WITNESS(block))));
        peer.fPauseSend = false;
        connman.ProcessMessagesOnce(peer);
    };
    auto submit = [&](const CBlock& block) {
        DataStream wire;
        wire << TX_WITH_WITNESS(block);
        JSONRPCRequest request;
        request.context = &m_node;
        request.strMethod = "submitblock";
        request.params = UniValue{UniValue::VARR};
        request.params.push_back(HexStr(wire));
        if (RPCIsInWarmup(nullptr)) SetRPCWarmupFinished();
        return tableRPC.execute(request);
    };

    // Neither an uncommitted over-budget body nor an unknown-parent body may
    // poison the indexed parent. The message handler must reject before validation.
    auto over = ResourceBlock(16385);
    over.hashPrevBlock = Params().GenesisBlock().GetHash();
    BOOST_CHECK_EXCEPTION(submit(over), UniValue, [](const UniValue& error) {
        const auto& code = error.find_value("code");
        return code.isNum() && code.getValStr() == "-22";
    });
    {
        ASSERT_DEBUG_LOG("bad-blk-witness-items");
        send_block(over);
    }
    BOOST_CHECK_EQUAL(m_node.chainman->ActiveChain().Height(), 0);
    BOOST_CHECK(!WITH_LOCK(cs_main, return m_node.chainman->m_blockman.LookupBlockIndex(over.GetHash())));
    auto unknown = over;
    unknown.hashPrevBlock = uint256{1};
    send_block(unknown);
    BOOST_CHECK_EQUAL(m_node.chainman->ActiveChain().Height(), 0);

    SetMockTime(Params().GenesisBlock().Time() + std::chrono::seconds{150});
    node::BlockAssembler::Options options;
    options.coinbase_output_script = CScript{} << OP_TRUE;
    options.include_dummy_extranonce = true;
    auto valid = node::BlockAssembler{m_node.chainman->ActiveChainstate(), nullptr, options}.CreateNewBlock()->block;
    node::RegenerateCommitments(valid, *m_node.chainman);
    PoWHashContext context;
    unsigned attempts{0};
    while (!CheckProofOfWork(valid, Params().GetConsensus(), context) && ++attempts < 10000) ++valid.nNonce;
    BOOST_REQUIRE(attempts < 10000);
    send_block(valid);
    BOOST_CHECK_EQUAL(m_node.chainman->ActiveChain().Height(), 1);
    BOOST_CHECK_EQUAL(submit(valid).get_str(), "duplicate");
    m_node.peerman->FinalizeNode(peer);
}

BOOST_AUTO_TEST_SUITE_END()
