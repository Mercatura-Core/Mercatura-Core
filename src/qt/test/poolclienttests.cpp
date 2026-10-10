// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <chainparams.h>
#include <consensus/merkle.h>
#include <hash.h>
#include <mining/solo.h>
#include <node/kernel_notifications.h>
#include <pool/job.h>
#include <qt/poolclient.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <util/strencodings.h>
#include <util/translation.h>
#include <validation.h>

#include <QCoreApplication>
#include <QSslSocket>
#include <QTest>

#include <random>
#include <set>
#include <thread>

const std::function<void(const std::string&)> G_TEST_LOG_FUN{};
const std::function<std::vector<const char*>()> G_TEST_COMMAND_LINE_ARGUMENTS{};
const std::function<std::string()> G_TEST_GET_FULL_NAME{};

namespace {
pool::Job Fixture(bool transaction = false, const std::function<void(CMutableTransaction&)>& mutate = {}, int recipients = 3, bool warmup = false, unsigned char warmup_program = 1)
{
    UniValue t{UniValue::VOBJ};
    t.pushKV("version", 0x20000000);
    t.pushKV("height", 1);
    t.pushKV("previousblockhash", std::string(64, '1'));
    t.pushKV("bits", recipients > 3 ? "1e0fffff" : "207fffff");
    arith_uint256 target;
    target.SetCompact(recipients > 3 ? 0x1e0fffff : 0x207fffff);
    t.pushKV("target", target.GetHex());
    t.pushKV("curtime", 1700000000);
    t.pushKV("mintime", 1699999999);
    // Above 2^53: must survive JSON without QJson's floating point conversion.
    t.pushKV("coinbasevalue", int64_t{9007199254740993});
    t.pushKV("coinbaseaux", UniValue{UniValue::VOBJ});
    t.pushKV("transactions", UniValue{UniValue::VARR});
    t.pushKV("sizelimit", 1048576);
    t.pushKV("default_witness_commitment", "6a24aa21a9ed" + HexStr(Hash(uint256{}, uint256{})));
    if (transaction) {
        CMutableTransaction tx;
        tx.vin.emplace_back();
        tx.vin[0].prevout.n = 0;
        tx.vin[0].prevout.hash = Txid::FromUint256(uint256::FromHex(std::string(64, '3')).value());
        tx.vout.emplace_back(1, CScript{} << OP_2 << std::vector<unsigned char>(32, 3));
        tx.vin[0].scriptWitness.stack = {{1, 2, 3}};
        if (mutate) mutate(tx);
        auto ref{MakeTransactionRef(tx)};
        std::vector<unsigned char> bytes;
        VectorWriter{bytes, 0, TX_WITH_WITNESS(tx)};
        UniValue entry{UniValue::VOBJ};
        entry.pushKV("data", HexStr(bytes));
        entry.pushKV("txid", ref->GetHash().GetHex());
        entry.pushKV("hash", ref->GetWitnessHash().GetHex());
        UniValue transactions{UniValue::VARR};
        transactions.push_back(entry);
        t.pushKV("transactions", transactions);
        CBlock block;
        block.vtx = {MakeTransactionRef(CMutableTransaction{}), ref};
        t.pushKV("default_witness_commitment", "6a24aa21a9ed" + HexStr(Hash(BlockWitnessMerkleRoot(block), uint256{})));
    }
    pool::Window w;
    for (int i{1}; i <= recipients; ++i) {
        CScript script;
        std::vector<unsigned char> program(32, i);
        if (recipients > 3) program[1] = i >> 8;
        if (warmup) program.assign(32, warmup_program);
        script << OP_2 << program;
        w.weights[HexStr(script)] = pool::Number{1};
    }
    w.total = pool::Number{static_cast<uint64_t>(recipients)};
    w.oldest = warmup ? 0 : 1;
    w.oldest_used = pool::Number{warmup ? 0U : 1U};
    const auto limit{pool::WorkScore(pool::Number::Parse(target.GetHex(), true)) * pool::Number{2}};
    return pool::BuildJob(t, w, warmup ? 0 : recipients, limit, "regtest", std::string(64, '2'), 1, 1, std::string(64, 'f'), 1700000000, 45, warmup);
}
void Reseal(UniValue& job, UniValue& manifest)
{
    UniValue snapshot{UniValue::VOBJ};
    for (const auto& key : manifest.getKeys())
        if (key != "snapshot_id" && key != "coinbase_txid" && key != "serialized_bytes" && key != "candidates") snapshot.pushKV(key, manifest[key]);
    const auto digest{pool::Digest("MCA-POOL/1/snapshot", snapshot.write())};
    manifest.pushKV("snapshot_id", digest);
    job.pushKV("snapshot_id", digest);
}
mining::PoolJob Validate(const UniValue& job, const UniValue& manifest)
{
    CScript payout;
    payout << OP_2 << std::vector<unsigned char>(32, 1);
    return mining::ValidatePoolJob(job, manifest, "regtest", std::string(64, '2'), "1", payout, Params().GetConsensus(), 1700000000);
}
} // namespace
class PoolClientTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() { SelectParams(ChainType::REGTEST); }
    void packagedTlsBackend()
    {
        QVERIFY2(QSslSocket::supportsSsl(), "Qt must include an operational TLS backend");
        QVERIFY(!QSslSocket::availableBackends().isEmpty());
    }
    void rejectSelfConsistentInvalidTransactions()
    {
        // M3 reseals all hashes, commitments and payout metadata. These jobs
        // are internally consistent but violate context-free Core rules.
        const std::vector<std::function<void(CMutableTransaction&)>> mutations{
            [](auto& tx) { tx.vout.clear(); },
            [](auto& tx) { tx.vout[0].nValue = -1; },
            [](auto& tx) { tx.vin.push_back(tx.vin[0]); },
            [](auto& tx) { tx.vin[0].prevout.SetNull(); },
            [](auto& tx) { tx.vout[0].nValue = MAX_MONEY; tx.vout.push_back(tx.vout[0]); },
        };
        for (const auto& mutate : mutations) {
            const auto fixture{Fixture(true, mutate)};
            QVERIFY_EXCEPTION_THROWN(Validate(fixture.Message(), fixture.manifest), std::exception);
        }
    }
    void rejectResealedTemplateTarget()
    {
        const auto fixture{Fixture()};
        auto job{fixture.Message()}, manifest{fixture.manifest};
        auto block_template{manifest["template"]};
        block_template.pushKV("target", std::string(64, 'f'));
        manifest.pushKV("template", block_template);
        const auto template_id{pool::Digest("MCA-POOL/1/template", block_template.write())};
        manifest.pushKV("template_id", template_id);
        const auto seed{pool::Digest("MCA-PPLNS/1/seed", "regtest:" + std::string(64, '2') + ":" + std::string(64, '1') + ":" + template_id + ":3:4:9007199254740993")};
        manifest.pushKV("rounding_seed", seed);
        UniValue allocations{UniValue::VARR};
        for (auto allocation : manifest["allocations"].getValues()) {
            allocation.pushKV("tie", pool::Digest("MCA-PPLNS/1/remainder", seed + ":" + allocation["script"].get_str()));
            allocations.push_back(allocation);
        }
        // This reward divides evenly, so amounts/remainders are unchanged.
        manifest.pushKV("allocations", allocations);
        Reseal(job, manifest);
        QVERIFY_EXCEPTION_THROWN(Validate(job, manifest), std::exception);
    }
    void realM3WorkMultipleLocalWorkers()
    {
        struct Shares {
            std::mutex mutex;
            std::set<uint32_t> nonces;
            bool low{false}, high{false}, duplicate{false};
        };
        struct Provider : mining::WorkProvider {
            Provider(mining::MiningJob job, Shares& shares) : work{std::move(job)}, seen{shares} {}
            std::optional<mining::MiningJob> GetJob() override { return work; }
            bool IsCurrent(const mining::MiningJob&) override { return true; }
            mining::SubmissionResult Submit(const mining::MiningJob&, const CBlockHeader& header) override
            {
                std::lock_guard lock{seen.mutex};
                seen.duplicate |= !seen.nonces.insert(header.nNonce).second;
                seen.low |= header.nNonce < 256;
                seen.high |= header.nNonce >= 256;
                return {true, false, "Test accepted real MercaHash share"};
            }
            void Interrupt() override {}
            std::string Destination() const override { return {}; }
            mining::MiningJob work;
            Shares& seen;
        };
        const auto fixture{Fixture()};
        const auto job{Validate(fixture.Message(), fixture.manifest)};
        Shares shares;
        // Exercise two real private scratchpads independently of this CI
        // container's conservative production CPU recommendation.
        mining::MiningController controller{{}, [] { return mining::CalculateWorkerLimits(4, uint64_t{8} << 30); }};
        QVERIFY(controller.Start([&] { return std::make_unique<Provider>(job.work, shares); }, 2));
        const auto deadline{std::chrono::steady_clock::now() + std::chrono::seconds{10}};
        bool both{false};
        while (std::chrono::steady_clock::now() < deadline) {
            {
                std::lock_guard lock{shares.mutex};
                both = shares.low && shares.high;
            }
            if (both) break;
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
        controller.RequestStop();
        controller.Wait();
        QVERIFY(both);
        QVERIFY(!shares.duplicate);
        QVERIFY(controller.GetStats().hashes >= 2);
        QCOMPARE(controller.GetStats().active_workers, 0U);
        QVERIFY(!controller.GetStats().busy);
    }
    void localCoreProposalChecks()
    {
        TestingSetup setup{ChainType::REGTEST};
        setup.m_node.notifications->setChainstateLoaded(true);
        auto local{interfaces::MakeMining(setup.m_node, false)};
        SetMockTime(Params().GenesisBlock().Time());
        {
            LOCK(cs_main);
            setup.m_node.chainman->UpdateIBDStatus();
        }
        QVERIFY(!local->isInitialBlockDownload());
        CScript payout;
        payout << OP_2 << std::vector<unsigned char>(32, 1);
        mining::SoloWorkProvider solo{interfaces::MakeMining(setup.m_node, false), payout, "", true};
        const auto work{solo.GetJob()};
        QVERIFY(work.has_value());
        CBlock block{work->header};
        block.vtx = {work->coinbase};
        QCOMPARE(mining::VerifyLocalPoolCandidate(local.get(), block), mining::LocalPoolValidation::VALID);
        QCOMPARE(mining::VerifyLocalPoolCandidate(nullptr, block), mining::LocalPoolValidation::UNAVAILABLE);
        CMutableTransaction overclaim{*work->coinbase};
        ++overclaim.vout[0].nValue;
        block.vtx[0] = MakeTransactionRef(overclaim);
        block.hashMerkleRoot = BlockMerkleRoot(block);
        QCOMPARE(mining::VerifyLocalPoolCandidate(local.get(), block), mining::LocalPoolValidation::INVALID);
        block.hashPrevBlock = uint256::FromHex(std::string(64, '3')).value();
        QCOMPARE(mining::VerifyLocalPoolCandidate(local.get(), block), mining::LocalPoolValidation::UNAVAILABLE);
    }
    void boundedFrames()
    {
        QVERIFY(mining::ParsePoolFrame("{\"id\":1,\"result\":{},\"error\":null}").isObject());
        const std::vector<std::string> invalid{"", "[]", "{", "{\"x\":1,\"x\":2}", "{\"x\":{\"a\":1,\"a\":2}}", std::string(8 * 1024 * 1024 + 1, ' '), std::string(33, '[') + "0" + std::string(33, ']'), std::string{"{\"x\":\"\xff\"}"}, std::string{"{}\0", 3}};
        for (const auto& frame : invalid)
            QVERIFY_EXCEPTION_THROWN(mining::ParsePoolFrame(frame), std::exception);
        std::string dense{"{\"x\":[0"};
        for (size_t i{0}; i < 524288; ++i)
            dense += ",0";
        dense += "]}";
        QVERIFY_EXCEPTION_THROWN(mining::ParsePoolFrame(dense), std::exception);
        // A frame exactly at the byte limit with a single large string remains
        // admissible; this is a node bound, not a reduced wire capacity.
        const std::string large{"{\"x\":\"" + std::string(8 * 1024 * 1024 - 8, 'a') + "\"}"};
        QCOMPARE(large.size(), size_t{8 * 1024 * 1024});
        QVERIFY(mining::ParsePoolFrame(large).isObject());
        QVERIFY(mining::ValidPoolEndpoint({"localhost", 3333, {}}));
        QVERIFY(mining::ValidPoolEndpoint({"::1", 3333, {}}));
        for (const auto& host : {"", "https://pool", "pool/path", "pool@host", "pool\n"})
            QVERIFY(!mining::ValidPoolEndpoint({host, 3333, {}}));
    }
    void boundedParserMutations()
    {
        std::mt19937 random{4};
        const std::string seed{"{\"id\":1,\"result\":{\"session\":\"1\"},\"error\":null}"};
        for (unsigned int i{0}; i < 5000; ++i) {
            auto frame{seed};
            for (unsigned int j{0}; j <= i % 8; ++j)
                frame[random() % frame.size()] = static_cast<char>(random() % 256);
            try {
                mining::ParsePoolFrame(frame);
            } catch (const std::exception&) {
            }
        }
    }
    void authoritativeM3Job()
    {
        const auto fixture{Fixture()};
        const auto job{Validate(fixture.Message(), fixture.manifest)};
        QVERIFY(job.work.continuous);
        QCOMPARE(job.work.header.GetHash(), fixture.block.GetHash());
        QCOMPARE(job.work.coinbase->GetValueOut(), CAmount{9007199254740993});
        QCOMPARE(job.id, fixture.id);
        QCOMPARE(job.serial, uint64_t{1});
        QCOMPARE(job.snapshot, fixture.snapshot);
        const auto transactions{Fixture(true)};
        const auto with_transaction{Validate(transactions.Message(), transactions.manifest)};
        QCOMPARE(with_transaction.work.header.hashMerkleRoot, transactions.block.hashMerkleRoot);
        const auto maximum{Fixture(false, {}, 1000)};
        const auto maximum_job{Validate(mining::ParsePoolFrame(maximum.Message().write()), mining::ParsePoolFrame(maximum.manifest.write()))};
        QCOMPARE(maximum_job.work.coinbase->vout.size(), size_t{1001});
        const auto warmup{Fixture(false, {}, 1, true)};
        QCOMPARE(Validate(warmup.Message(), warmup.manifest).work.coinbase->vout.size(), size_t{2});
        const auto redirected{Fixture(false, {}, 1, true, 2)};
        QVERIFY_EXCEPTION_THROWN(Validate(redirected.Message(), redirected.manifest), std::exception);
    }
    void rejectInconsistentWork()
    {
        const auto fixture{Fixture()};
        const std::vector<std::pair<std::string, UniValue>> changes{
            {"parent", UniValue{std::string(64, '3')}}, {"network_target", UniValue{std::string(64, 'f')}}, {"share_target", UniValue{std::string(64, '0')}}, {"header", UniValue{"00"}}, {"coinbase", UniValue{"00"}}, {"extranonce_namespace", UniValue{"2"}}, {"snapshot_id", UniValue{std::string(64, '3')}}, {"fee_base_units", UniValue{1}}, {"warmup", UniValue{true}}, {"expires", UniValue{1700000000}}};
        for (const auto& [key, value] : changes) {
            auto job{fixture.Message()};
            job.pushKV(key, value);
            QVERIFY_EXCEPTION_THROWN(Validate(job, fixture.manifest), std::exception);
        }
        for (const auto& key : {"network", "genesis", "coinbase_txid", "template_id", "rounding_seed"}) {
            auto manifest{fixture.manifest};
            manifest.pushKV(key, UniValue{"wrong"});
            QVERIFY_EXCEPTION_THROWN(Validate(fixture.Message(), manifest), std::exception);
        }
        auto manifest{fixture.manifest};
        auto allocation{manifest["allocations"][0]};
        allocation.pushKV("amount", 1);
        UniValue allocations{UniValue::VARR};
        allocations.push_back(allocation);
        for (size_t i{1}; i < manifest["allocations"].size(); ++i)
            allocations.push_back(manifest["allocations"][i]);
        manifest.pushKV("allocations", allocations);
        auto job{fixture.Message()};
        Reseal(job, manifest);
        QVERIFY_EXCEPTION_THROWN(Validate(job, manifest), std::exception);
        const auto transactions{Fixture(true)};
        job = transactions.Message();
        UniValue wrong_path{UniValue::VARR};
        wrong_path.push_back(std::string(64, '4'));
        job.pushKV("merkle_path", wrong_path);
        QVERIFY_EXCEPTION_THROWN(Validate(job, transactions.manifest), std::exception);
    }
};
QTEST_GUILESS_MAIN(PoolClientTests)
#include <poolclienttests.moc>
