// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <consensus/merkle.h>
#include <hash.h>
#include <openssl/ssl.h>
#include <pool/accounting.h>
#include <pool/config.h>
#include <pool/job.h>
#include <pool/ledger.h>
#include <pool/protocol.h>
#include <pool/rpc.h>
#include <pool/service.h>
#include <pool/transport.h>
#include <pool/verifier.h>
#include <streams.h>
#include <sys/wait.h>
#include <unistd.h>
#include <util/strencodings.h>
#include <util/string.h>

#include <filesystem>
#include <functional>
#include <iostream>
#include <set>
#include <thread>

namespace {
using namespace pool;
size_t checks{0};
void Check(bool result, const std::string& message)
{
    ++checks;
    if (!result) throw std::runtime_error(message);
}
template <typename F>
void Throws(F f, const std::string& message)
{
    bool thrown = false;
    try {
        f();
    } catch (const std::exception&) {
        thrown = true;
    }
    Check(thrown, message);
}
std::string Script(unsigned int identity)
{
    std::vector<unsigned char> commitment(32, 0);
    for (unsigned i = 0; i < 4; ++i)
        commitment[i] = static_cast<unsigned char>(identity >> (8 * i));
    CScript s;
    s << OP_2 << commitment;
    return HexStr(s);
}
UniValue Template(CAmount reward = 100000)
{
    UniValue t{UniValue::VOBJ};
    t.pushKV("version", 0x20000000);
    t.pushKV("height", 1);
    t.pushKV("previousblockhash", std::string(64, '1'));
    t.pushKV("bits", "207fffff");
    arith_uint256 target;
    target.SetCompact(0x207fffff);
    t.pushKV("target", target.GetHex());
    t.pushKV("curtime", 1700000000);
    t.pushKV("mintime", 1699999999);
    t.pushKV("coinbasevalue", reward);
    t.pushKV("coinbaseaux", UniValue{UniValue::VOBJ});
    t.pushKV("transactions", UniValue{UniValue::VARR});
    t.pushKV("sizelimit", 1048576);
    t.pushKV("sigoplimit", 80000);
    uint256 zero;
    auto digest = Hash(zero, zero);
    t.pushKV("default_witness_commitment", "6a24aa21a9ed" + HexStr(digest));
    return t;
}
Window Participants(size_t count)
{
    Window w;
    for (size_t i = 0; i < count; ++i)
        w.weights[Script(i + 1)] = Number{1};
    w.total = Number{count};
    return w;
}
Job MakeJob(size_t count, CAmount reward = 100000) { return BuildJob(Template(reward), Participants(count), 7, Number{4}, "regtest", std::string(64, '2'), 1, 1, std::string(64, 'f'), 1000, 45); }
void Scores()
{
    const auto max = Number::PowerOfTwo(256) - Number{1};
    Check(WorkScore(Number{}) == Number::PowerOfTwo(256), "target zero full 257-bit score");
    Check(WorkScore(Number{1}) == Number::PowerOfTwo(255), "minimum positive target");
    Check(WorkScore(max) == Number{1}, "maximum target boundary");
    Check(WorkScore(max - Number{1}) == Number{1}, "near maximum target");
    Check(WorkScore(Number{7}) == Number::PowerOfTwo(253), "assigned difficulty weighting");
    Throws([] { WorkScore(Number::PowerOfTwo(256)); }, "reject 257-bit target");
    Throws([] { Number::Parse("-1"); }, "reject negative integer");
    Throws([] { Number::Parse("1.5"); }, "reject floating integer");
    Throws([] { Number::PowerOfTwo(64).Uint64(); }, "uint64 overflow checked");
    for (uint32_t bits : {0x207fffffU, 0x1f3fffffU, 0x1d00ffffU, 0x01010000U}) {
        arith_uint256 target;
        target.SetCompact(bits);
        const auto core = ((~target) / (target + 1)) + 1;
        Check(WorkScore(Number::Parse(target.GetHex(), true)) == Number::Parse(core.GetHex(), true), "matches Core work identity for valid nonzero targets");
    }
    auto giant = Number::PowerOfTwo(256) * Number{UINT64_MAX};
    Check(giant / Number{UINT64_MAX} == Number::PowerOfTwo(256), "wide cumulative intermediate");
}
void Windows()
{
    std::vector<Share> shares{{4, Script(1), Number{7}}, {3, Script(2), Number{4}}, {2, Script(1), Number{3}}, {1, Script(3), Number{2}}};
    auto w = SelectWindow(shares, 4, Number{10});
    Check(w.total == Number{10} && w.weights[Script(1)] == Number{7} && w.weights[Script(2)] == Number{3}, "clip oldest boundary");
    Check(w.oldest == 3 && w.oldest_used == Number{3}, "auditable boundary share");
    auto earlier = SelectWindow(shares, 3, Number{10});
    Check(earlier.total == Number{9} && !earlier.weights.contains(Script(1) + "changed"), "cutoff excludes newer work");
    auto higher = SelectWindow(shares, 4, WorkScore(Number::PowerOfTwo(255)) * Number{2});
    Check(higher.total == Number{2} && higher.weights.size() == 1, "two expected block work and network difficulty change");
    auto lower = SelectWindow(shares, 4, Number{100});
    Check(lower.total == Number{16} && lower.weights.contains(Script(3)), "historical disconnected/address identity retained when window expands");
    Throws([&] { SelectWindow(shares, 4, Number{}); }, "zero window rejected");
    std::reverse(shares.begin(), shares.end());
    Throws([&] { SelectWindow(shares, 4, Number{100}); }, "unordered ledger rejected");
}
void Rounding()
{
    Window w;
    w.weights = {{Script(1), Number{1}}, {Script(2), Number{2}}, {Script(3), Number{7}}};
    w.total = Number{10};
    auto a = Allocate(100, w, "seed");
    Check(a[0].amount == 10 && a[1].amount == 20 && a[2].amount == 70, "1:2:7 allocation");
    for (CAmount reward : std::vector<CAmount>{0, 1, 2, 3, 11, 99, 1001, MAX_MONEY}) {
        auto allocations = Allocate(reward, w, "seed");
        CAmount sum = 0;
        for (const auto& p : allocations) {
            Check(p.amount >= 0, "nonnegative allocation");
            sum += p.amount;
        }
        Check(sum == reward, "conserve last base unit");
        Check(AllocationsJson(allocations).write() == AllocationsJson(Allocate(reward, w, "seed")).write(), "repeatable rounding");
    }
    Window reversed;
    reversed.total = w.total;
    for (auto it = w.weights.rbegin(); it != w.weights.rend(); ++it)
        reversed.weights.insert(*it);
    Check(AllocationsJson(Allocate(19, w, "same")).write() == AllocationsJson(Allocate(19, reversed, "same")).write(), "recipient order independence");
    auto equal = Participants(3);
    std::set<std::string> winners;
    for (int i = 0; i < 100; ++i)
        for (const auto& p : Allocate(1, equal, "seed" + util::ToString(i)))
            if (p.amount) winners.insert(p.script);
    Check(winners.size() == 3, "domain-separated ties do not always favor first address");
    auto huge = Participants(3);
    huge.total = Number{};
    for (auto& [s, n] : huge.weights) {
        n = Number::PowerOfTwo(256);
        huge.total += n;
    }
    CAmount sum = 0;
    for (const auto& p : Allocate(MAX_MONEY, huge, "wide"))
        sum += p.amount;
    Check(sum == MAX_MONEY, "wide reward products");
    Throws([] { Allocate(1, Window{}, "empty"); }, "empty window refused");
    Throws([&] { Allocate(-1, w, "negative"); }, "negative reward refused");
    auto invalid = Participants(2);
    invalid.total = Number{1};
    invalid.weights.begin()->second = Number::PowerOfTwo(63);
    Throws([&] { Allocate(MAX_MONEY, invalid, "invalid"); }, "inconsistent oversized weights rejected before signed conversion");
}
void Coinbases()
{
    for (size_t count : {3, 100, 1000}) {
        auto j = MakeJob(count);
        Check(j.block.vtx[0]->vout.size() == count + 1, "every positive recipient and witness output");
        Check(j.block.vtx[0]->GetValueOut() == 100000, "exact template reward");
        Check(j.block.hashMerkleRoot == BlockMerkleRoot(j.block), "correct merkle root");
        Check(BlockHex(ParseBlock(BlockHex(j.block))) == BlockHex(j.block), "witness block serialization round trip");
        const auto actual = BlockHex(j.block).size() / 2;
        Check(actual == RequiredBlockBytes(Template(), count), "measured block size matches reservation");
        Check(actual < 1048576, "all recipient counts fit initial block capacity");
        const auto& cb = *j.block.vtx[0];
        Check(cb.vin[0].scriptWitness.stack == std::vector<std::vector<unsigned char>>{std::vector<unsigned char>(32, 0)}, "reserved witness value");
        Check(HasPoolMarker(cb.vin[0].scriptSig, 1), "canonical marker after height");
        Check(cb.vout.back().nValue == 0, "commitment has zero value");
        for (size_t i = 0; i < count; ++i)
            Check(GetSerializeSize(cb.vout[i]) == 43, "native PQ output serializes to 43 bytes");
        std::cout << "  " << count << " recipients: " << actual << " block bytes, " << GetSerializeSize(TX_WITH_WITNESS(cb)) << " coinbase bytes\n";
    }
    auto j = MakeJob(3);
    Check(j.id == "365a28a922fed051fb42a48cb290966426b0a2fff8b1ced962b073e1ad73b803", "locale-independent conversion preserves issued job identity");
    Check(j.snapshot == "fe033864d18c24f7bb3aa8b69b2c25dbf12cfa5e6d398ea1de242ea81b2799ce", "locale-independent conversion preserves frozen payout snapshot");
    const auto frozen = BlockHex(j.block);
    auto w = Participants(3);
    w.weights[Script(4)] = Number{20};
    w.total += Number{20};
    auto future = BuildJob(Template(), w, 8, Number{100}, "regtest", std::string(64, '2'), 1, 2, std::string(64, 'f'), 1001, 45);
    Check(frozen == BlockHex(j.block) && future.snapshot != j.snapshot, "future contributions cannot change issued block");
    auto another = BuildJob(Template(), Participants(3), 7, Number{4}, "regtest", std::string(64, '2'), 2, 1, std::string(64, 'f'), 1000, 45);
    Check(another.snapshot == j.snapshot && another.block.hashMerkleRoot != j.block.hashMerkleRoot, "same frozen payouts with unique session extranonces");
    auto malformed = j.block.vtx[0]->vin[0].scriptSig;
    malformed.push_back(OP_0);
    Check(!HasPoolMarker(malformed, 1), "malformed marker refused");
    Check(!HasPoolMarker(j.block.vtx[0]->vin[0].scriptSig, 2), "wrong height marker refused");
    auto nonminimal = CoinbaseScript(1, 1, 1);
    nonminimal.insert(nonminimal.begin() + 1, OP_PUSHDATA1);
    Check(!HasPoolMarker(nonminimal, 1), "nonminimal marker push refused");
    Throws([] { MakeJob(1001); }, "1001 recipient launch policy refused by builder");
    Throws([] { PqScript("5120" + std::string(64, '0')); }, "non-PQ witness script refused");
    auto t = Template();
    t.pushKV("sizelimit", 200);
    Throws([&] { BuildJob(t, Participants(3), 1, Number{4}, "regtest", std::string(64, '2'), 1, 1, std::string(64, 'f'), 1000, 45); }, "oversized finalized coinbase rejected");
    t = Template();
    t.pushKV("default_witness_commitment", std::string(76, '0'));
    Throws([&] { BuildJob(t, Participants(3), 1, Number{4}, "regtest", std::string(64, '2'), 1, 1, std::string(64, 'f'), 1000, 45); }, "wrong witness commitment refused");
    t = Template(MAX_MONEY + 1);
    Throws([&] { BuildJob(t, Participants(3), 1, Number{4}, "regtest", std::string(64, '2'), 1, 1, std::string(64, 'f'), 1000, 45); }, "out-of-range reward refused");
    t = Template();
    t.pushKV("bits", "ff7fffff");
    Throws([&] { TemplateTarget(t); }, "compact target overflow");
    auto tiny = MakeJob(3, 1);
    Check(tiny.block.vtx[0]->vout.size() == 2 && tiny.block.vtx[0]->GetValueOut() == 1, "zero allocations omitted without dropping a positive unit");
    // Synthetic witness transaction exercises exact serialization and commitment
    // handling; live fee-bearing consensus coverage also comes from M1.
    CMutableTransaction transaction;
    transaction.vin.resize(1);
    transaction.vin[0].prevout.n = 0;
    transaction.vin[0].scriptWitness.stack = {{1, 2, 3}, {4, 5}};
    transaction.vout.emplace_back(90, PqScript(Script(4)));
    auto ref = MakeTransactionRef(transaction);
    std::vector<unsigned char> raw;
    VectorWriter{raw, 0, TX_WITH_WITNESS(*ref)};
    UniValue entry{UniValue::VOBJ};
    entry.pushKV("data", HexStr(raw));
    entry.pushKV("txid", ref->GetHash().GetHex());
    entry.pushKV("hash", ref->GetWitnessHash().GetHex());
    entry.pushKV("fee", 20);
    UniValue entries{UniValue::VARR};
    entries.push_back(entry);
    t = Template(100020);
    t.pushKV("transactions", entries);
    uint256 zero;
    auto root = Hash(zero, ref->GetWitnessHash().ToUint256());
    auto witness = Hash(root, zero);
    t.pushKV("default_witness_commitment", "6a24aa21a9ed" + HexStr(witness));
    auto fees = BuildJob(t, Participants(3), 7, Number{4}, "regtest", std::string(64, '2'), 1, 1, std::string(64, 'f'), 1000, 45);
    Check(fees.block.vtx[0]->GetValueOut() == 100020, "exact authoritative reward including selected fees");
    Check(fees.block.vtx.size() == 2 && fees.block.vtx[1]->GetWitnessHash() == ref->GetWitnessHash(), "selected transaction and witness preserved");
    Check(BlockWitnessMerkleRoot(fees.block) == root, "coinbase-independent witness root");
}
void InsertJob(Ledger& db, const Job& j)
{
    db.Admit(Script(1), 3);
    auto s = db.Query("INSERT INTO sessions(id,script,target,token_hash,created) VALUES(1,?,?,?,0)");
    s.Bind(1, Script(1)).Bind(2, std::string(64, 'f')).Bind(3, "token").Row();
    auto snapshot = db.Query("INSERT INTO snapshots VALUES(?,?)");
    snapshot.Bind(1, j.snapshot).Bind(2, j.snapshot_manifest.write()).Row();
    auto job = db.Query("INSERT INTO jobs VALUES(?,?,?,?,?,?,?,?,?,?)");
    job.Bind(1, j.id).Bind(2, 1).Bind(3, 1).Bind(4, j.snapshot).Bind(5, j.block.hashPrevBlock.GetHex()).Bind(6, std::string(64, 'f')).Bind(7, BlockHex(j.block)).Bind(8, 1000).Bind(9, 1045).Bind(10, j.manifest.write()).Row();
    auto submission = db.Query("INSERT INTO submissions VALUES('work',?,1,0,'queued',1000)");
    submission.Bind(1, j.id).Row();
}
void Persistence()
{
    auto path = std::filesystem::temp_directory_path() / ("mercatura-pool-test-" + util::ToString(getpid()));
    std::filesystem::create_directory(path);
    const auto file = (path / "ledger.sqlite").string();
    auto j = MakeJob(3);
    {
        Ledger db{file, "regtest", "genesis", "policy"};
        Throws([&] { Ledger second{file, "regtest", "genesis", "policy"}; }, "second coordinator cannot own same database");
        InsertJob(db, j);
        Check(db.Accept("work", j.id, 1, Script(1), Number{7}, 1000) == 1, "durable receipt sequence");
        db.Admit(Script(2), 3);
        db.Admit(Script(3), 3);
        Throws([&] { db.Admit(Script(4), 3); }, "admission before incompatible work");
        Check(db.Counter("namespace") == 1, "durable namespace begins");
        auto too_large = db.Query("INSERT INTO metadata VALUES('overflow','18446744073709551615')");
        too_large.Row();
        Throws([&] { db.Counter("overflow"); }, "namespace overflow checked before signed conversion");
        Throws([&] { db.Pplns(1, Number{}); }, "ledger refuses zero window");
        {
            Transaction rollback{db};
            auto q = db.Query("INSERT INTO snapshots VALUES('rolled-back','partial')");
            q.Row();
        }
    }
    {
        Ledger db{file, "regtest", "genesis", "policy"};
        Check(db.Cutoff() == 1, "accepted shares recovered after restart");
        Check(db.Accept("work", j.id, 1, Script(1), Number{99}, 1001) == 1, "duplicate cannot credit twice after restart");
        Check(db.Pplns(1, Number{100}).weights[Script(1)] == Number{7}, "original score/payout survives disconnect and restart");
        auto jobs = db.Query("SELECT block FROM jobs WHERE id=?");
        jobs.Bind(1, j.id);
        Check(jobs.Row() && jobs.Text(0) == BlockHex(j.block), "issued immutable job recovered");
        Check(db.Counter("namespace") == 2, "namespace does not replay after restart");
        auto q = db.Query("SELECT COUNT(*) FROM snapshots WHERE id='rolled-back'");
        q.Row();
        Check(q.Integer(0) == 0, "transaction rollback excludes partial snapshot");
    }
    pid_t child = fork();
    if (child < 0) throw std::runtime_error("fork failed");
    if (child == 0) {
        try {
            Ledger db{file, "regtest", "genesis", "policy"};
            db.Begin();
            auto q = db.Query("INSERT INTO snapshots VALUES('crash','partial')");
            q.Row();
            _exit(0);
        } catch (...) {
            _exit(1);
        }
    }
    int status = 0;
    waitpid(child, &status, 0);
    Check(WIFEXITED(status) && WEXITSTATUS(status) == 0, "crash fixture executed");
    {
        Ledger db{file, "regtest", "genesis", "policy"};
        Check(db.Cutoff() == 1, "acknowledged share survives abrupt unrelated crash");
        auto q = db.Query("SELECT COUNT(*) FROM snapshots WHERE id='crash'");
        q.Row();
        Check(q.Integer(0) == 0, "crash near snapshot persistence is atomic");
    }
    Throws([&] { Ledger bad{file, "other", "genesis", "policy"}; }, "network mismatch rejected");
    Throws([&] { Ledger bad{file, "regtest", "other", "policy"}; }, "genesis mismatch rejected");
    Throws([&] { Ledger bad{file, "regtest", "genesis", "other"}; }, "unsafe policy change rejected");
    {
        Ledger db{file, "regtest", "genesis", "policy"};
        db.Exec("DROP TABLE registrations; DROP TABLE session_registrations; PRAGMA user_version=1");
    }
    {
        Ledger db{file, "regtest", "genesis", "policy"};
        Check(db.Cutoff() == 1 && db.IdentityCount() == 3, "v1 migration preserves shares and historical identities");
        auto version = db.Query("PRAGMA user_version");
        version.Row();
        Check(version.Integer(0) == 2, "durable v2 admission migration");
    }
    {
        Ledger db{file, "regtest", "genesis", "policy"};
        db.Exec("PRAGMA user_version=99");
    }
    Throws([&] { Ledger bad{file, "regtest", "genesis", "policy"}; }, "schema version mismatch rejected");
    std::filesystem::remove_all(path);
}
void Registration()
{
    auto file = std::filesystem::temp_directory_path() / ("mercatura-admission-" + util::ToString(getpid()));
    auto attempt = [](Ledger& db, unsigned identity, const std::string& ip, int64_t now) {
        Transaction tx{db};
        bool admitted = db.Register(Script(identity), ip, now, 60, 1, 2, 1000);
        if (admitted) tx.Commit();
        return admitted;
    };
    {
        Ledger db{file.string(), "regtest", "genesis", "policy"};
        Check(attempt(db, 1, "one", 100), "first identity admitted");
        Check(!attempt(db, 2, "one", 100), "new addresses limited per IP");
        Check(attempt(db, 1, "one", 100), "known identity does not consume registration quota");
        Check(attempt(db, 2, "two", 100), "second IP admitted");
        Check(!attempt(db, 3, "three", 100) && db.IdentityCount() == 2, "global limit and refusal do not consume lifetime slots");
    }
    {
        Ledger db{file.string(), "regtest", "genesis", "policy"};
        Check(!attempt(db, 3, "three", 101), "registration limit survives restart");
        Check(!attempt(db, 3, "three", 90), "clock rollback cannot replenish registration quota");
        Check(attempt(db, 3, "three", 160), "rolling registration quota releases only rate capacity");
        Check(db.IdentityCount() == 3, "historical payout reservations are not retired");
        Transaction tx{db};
        auto session = db.Query("INSERT INTO sessions(script,target,token_hash,created) VALUES(?,?,?,?)");
        session.Bind(1, Script(1)).Bind(2, std::string(64, 'f')).Bind(3, "namespace-token").Bind(4, 160).Row();
        db.RecordSession(db.LastInsert(), "three", 160);
        tx.Commit();
        Check(!db.SessionAllowed("three", 160, 60, 1, 2), "known-address namespace churn has separate per-IP quota");
        Check(!db.SessionAllowed("four", 160, 60, 1, 1), "namespace quota has global bound");
        Check(db.SessionAllowed("three", 220, 60, 1, 2), "namespace rolling quota expires without dropping history");
    }
    std::filesystem::remove(file);
}
void ProtocolAndTls()
{
    const std::string valid = R"({"id":1,"method":"ping","params":{}})";
    Check(ParseRequest(valid, 4096).method == "ping", "versioned request framing");
    for (const auto& raw : std::vector<std::string>{"{}", "not json", R"({"id":1,"id":2,"method":"ping","params":{}})", R"({"id":-1,"method":"ping","params":{}})", R"({"id":1,"method":"ping","params":{},"score":100})", std::string(4097, ' ')})
        Throws([&] { ParseRequest(raw, 4096); }, "malformed/oversized/duplicate fields refused");
    UniValue forged;
    forged.read(R"({"job_id":"id","nonce":1,"difficulty":100,"recipients":[]})");
    Throws([&] { Fields(forged, {"job_id", "nonce"}); }, "forged difficulty/manifest refused");
    Config cfg;
    cfg.genesis = std::string(64, '1');
    cfg.rpc_user = "test";
    cfg.rpc_password = "secret";
    Throws([&] { cfg.Validate(); }, "TLS required by default");
    cfg.plaintext_regtest = true;
    cfg.Validate();
    Check(true, "explicit localhost regtest plaintext allowed");
    cfg.listen = "0.0.0.0";
    Throws([&] { cfg.Validate(); }, "remote plaintext refused");
    cfg.listen = "127.0.0.1";
    cfg.network = "main";
    Throws([&] { cfg.Validate(); }, "mainnet plaintext refused");
    cfg.network = "regtest";
    cfg.verifier_memory_mib = 159;
    Throws([&] { cfg.Validate(); }, "scratchpad memory bound");
    cfg.verifier_memory_mib = 160;
    cfg.rpc_connect_timeout_seconds = 11;
    Throws([&] { cfg.Validate(); }, "RPC connection timeout cannot exceed total deadline");
    cfg.rpc_connect_timeout_seconds = 10;
    cfg.handshake_seconds = 61;
    Throws([&] { cfg.Validate(); }, "negotiation timeout bounded by heartbeat");
    cfg.handshake_seconds = 30;
    cfg.rpc_url = "http://example.com/";
    Throws([&] { Rpc rpc{cfg}; }, "remote RPC plaintext refused without making network call");
}
void NativeVerifier()
{
    std::vector<unsigned char> bytes(80);
    for (size_t i = 0; i < 80; ++i)
        bytes[i] = static_cast<unsigned char>(i);
    CBlockHeader header;
    SpanReader{bytes} >> header;
    Throws([] { Verifier bad{1, 1, 159}; }, "verifier startup memory check");
    Verifier verifier{1, 1, 160};
    auto request = verifier.Submit(header);
    Check(request.has_value(), "native hash request queued");
    auto result = request->get();
    Check(result.error.empty() && !result.cancelled, "actual MercaHash completed");
    Check(HexStr(result.hash) == "2321712af21502878986c0c4f21d79e17e2281619e3958f11e3c634c9f17f7d8", "frozen Core MercaHash-v1 vector matches");
    std::vector<std::future<Verification>> pending;
    size_t refused = 0;
    for (int i = 0; i < 20; ++i) {
        auto f = verifier.Submit(header);
        if (f)
            pending.push_back(std::move(*f));
        else
            ++refused;
    }
    Check(refused > 0 && pending.size() <= 2 && verifier.Depth() <= 1, "bounded queue and concurrency");
    verifier.Stop();
    for (auto& f : pending)
        Check(f.wait_for(std::chrono::seconds{0}) == std::future_status::ready, "shutdown resolves queued/in-progress promises");
    Check(!verifier.Submit(header), "no new work after cancellation");
    Check(verifier.completed.load() >= 1 && verifier.microseconds.load() > 0, "latency and throughput counters");
}
void ServiceLifecycle(const std::string& config_file)
{
    auto cfg = Config::Load(config_file);
    cfg.refresh_seconds = 1;
    cfg.lifetime_seconds = 3;
    cfg.warmup = true;
    Service service{cfg};
    int64_t session = 0;
    UniValue hello{UniValue::VOBJ};
    hello.pushKV("version", 1);
    hello.pushKV("network", cfg.network);
    hello.pushKV("genesis", cfg.genesis);
    hello.pushKV("algorithm", "MercaHash-V1");
    hello.pushKV("payout", "fixture-pq-1");
    auto connected = service.Handle(session, "127.0.0.1", {1, "hello", hello});
    Check(connected && (*connected)["error"].isNull(), "lifecycle session established");
    auto job = service.Handle(session, "127.0.0.1", {2, "getjob", UniValue{UniValue::VOBJ}});
    Check(job && (*job)["result"]["warmup"].get_bool(), "job explicitly discloses individual warmup");
    const auto job_id = (*job)["result"]["job_id"].get_str();
    auto submit = [&](int64_t request, uint32_t nonce) {
        UniValue params{UniValue::VOBJ};
        params.pushKV("job_id", job_id);
        params.pushKV("nonce", nonce);
        return service.Handle(session, "127.0.0.1", {request, "submit", params});
    };
    Check(!submit(3, 0) && !submit(4, 1), "two actual hashes queued before reactor completion");
    const auto old = session;
    service.Disconnect(session);
    session = 0;
    UniValue resume{UniValue::VOBJ};
    for (const auto& key : hello.getKeys())
        if (key != "payout") resume.pushKV(key, hello[key]);
    resume.pushKV("resume_token", (*connected)["result"]["resume_token"]);
    auto resumed = service.Handle(session, "127.0.0.1", {3, "hello", resume});
    Check(resumed && (*resumed)["error"].isNull() && session == old, "namespace resumed while hashes remain pending");
    auto third = submit(4, 2);
    Check(third && (*third)["error"]["code"].getInt<int>() == OVERLOAD, "reconnect cannot bypass two-pending-share bound");
    std::this_thread::sleep_for(std::chrono::seconds{4});
    auto replies = service.Tick();
    Check(replies.empty(), "old connection request IDs never delivered to resumed connection");
    Check(service.Stats()["accepted_shares"].getInt<int>() == 2, "received-before-expiry shares survive delayed verification completion");
    auto replay = submit(5, 0);
    Check(replay && (*replay)["error"]["code"].getInt<int>() == DUPLICATE && (*replay)["receipt"]["accepted"].get_bool(), "lost old-connection receipt recovered after expiry");
    auto shared = service.Handle(session, "127.0.0.1", {6, "getjob", UniValue{UniValue::VOBJ}});
    Check(shared && !(*shared)["result"]["warmup"].get_bool(), "accepted warmup work creates shared job after expiry");
    for (uint32_t nonce : {201U, 202U}) {
        UniValue params{UniValue::VOBJ};
        params.pushKV("job_id", (*shared)["result"]["job_id"]);
        params.pushKV("nonce", nonce);
        Check(!service.Handle(session, "127.0.0.1", {nonce, "submit", params}), "parent-change fixture queues actual native verification");
    }
    Rpc{cfg}.Call("fixture_advance_parent"); // Only the synthetic test RPC supports this.
    size_t stale = 0;
    for (int i = 0; i < 200 && stale < 2; ++i) {
        for (const auto& response : service.Tick())
            if (response.message["error"].isObject()) {
                Check(response.message["error"]["code"].getInt<int>() == STALE, "queued work rejects changed parent");
                ++stale;
            }
        if (stale < 2) std::this_thread::sleep_for(std::chrono::milliseconds{50});
    }
    Check(stale == 2 && service.Stats()["accepted_shares"].getInt<int>() == 2, "parent change adds no stale credit and preserves prior accepted work");
}
void TlsRetries(const std::string& config_file)
{
    const auto cfg = Config::Load(config_file);
    using Context = std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)>;
    using Connection = std::unique_ptr<SSL, decltype(&SSL_free)>;
    Context server_ctx{SSL_CTX_new(TLS_server_method()), SSL_CTX_free}, client_ctx{SSL_CTX_new(TLS_client_method()), SSL_CTX_free};
    Check(server_ctx && client_ctx, "TLS BIO contexts allocated");
    SSL_CTX_set_num_tickets(server_ctx.get(), 0);
    Check(SSL_CTX_use_certificate_chain_file(server_ctx.get(), cfg.tls_certificate.c_str()) == 1 && SSL_CTX_use_PrivateKey_file(server_ctx.get(), cfg.tls_key.c_str(), SSL_FILETYPE_PEM) == 1, "actual TLS fixture credentials loaded");
    Check(SSL_CTX_load_verify_locations(client_ctx.get(), cfg.tls_certificate.c_str(), nullptr) == 1, "TLS BIO client trusts fixture certificate");
    SSL_CTX_set_verify(client_ctx.get(), SSL_VERIFY_PEER, nullptr);
    Connection server{SSL_new(server_ctx.get()), SSL_free}, client{SSL_new(client_ctx.get()), SSL_free};
    Check(server && client, "TLS BIO connections allocated");
    Check(X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(client.get()), "127.0.0.1") == 1, "TLS BIO hostname verification configured");
    BIO *server_bio = nullptr, *client_bio = nullptr;
    Check(BIO_new_bio_pair(&server_bio, 256, &client_bio, 256) == 1, "bounded TLS BIO pair allocated");
    SSL_set_bio(server.get(), server_bio, server_bio);
    SSL_set_bio(client.get(), client_bio, client_bio);
    SSL_set_accept_state(server.get());
    SSL_set_connect_state(client.get());
    SSL_set_mode(server.get(), SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    auto retry = [](SSL* ssl, int result) {
        if (result > 0) return;
        const auto error = SSL_get_error(ssl, result);
        if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) throw std::runtime_error("unexpected TLS BIO error");
    };
    for (int i = 0; i < 10000 && (!SSL_is_init_finished(server.get()) || !SSL_is_init_finished(client.get())); ++i) {
        retry(server.get(), SSL_do_handshake(server.get()));
        retry(client.get(), SSL_do_handshake(client.get()));
    }
    Check(SSL_is_init_finished(server.get()) && SSL_is_init_finished(client.get()) && SSL_get_verify_result(client.get()) == X509_V_OK, "actual authenticated TLS BIO handshake completes");
    std::string output(1024, 'a'), received;
    size_t pending = 0, offset = 0;
    const auto first = SSL_write(server.get(), output.data(), TlsWriteLength(pending, output.size()));
    Check(first < 0 && SSL_get_error(server.get(), first) == SSL_ERROR_WANT_WRITE, "bounded BIO deterministically forces actual SSL_write retry");
    output.append(333, 'b'); // Another response queues before the retry.
    for (int i = 0; i < 10000 && received.size() < output.size(); ++i) {
        if (offset < output.size()) {
            const auto n = SSL_write(server.get(), output.data() + offset, TlsWriteLength(pending, output.size() - offset));
            retry(server.get(), n);
            if (n > 0) {
                offset += n;
                pending = 0;
            }
        }
        char bytes[512];
        const auto n = SSL_read(client.get(), bytes, sizeof(bytes));
        retry(client.get(), n);
        if (n > 0) received.append(bytes, n);
    }
    Check(received == output, "production TLS retry length preserves original and appended responses");
}
} // namespace
int main(int argc, char** argv)
{
    if (argc != 1) {
        try {
            if (argc != 3) throw std::invalid_argument("unknown test arguments");
            if (std::string{argv[1]} == "--service-config")
                ServiceLifecycle(argv[2]);
            else if (std::string{argv[1]} == "--tls-config")
                TlsRetries(argv[2]);
            else
                throw std::invalid_argument("unknown test arguments");
            std::cout << "PASS deterministic " << argv[1] << " lifecycle: " << checks << " checks\n";
            return 0;
        } catch (const std::exception& e) {
            std::cerr << "FAIL " << argv[1] << " lifecycle: " << e.what() << '\n';
            return 1;
        }
    }
    size_t failures = 0;
    for (const auto& [name, run] : std::vector<std::pair<std::string, std::function<void()>>>{{"work scores", Scores}, {"PPLNS windows", Windows}, {"reward conservation", Rounding}, {"PQ coinbases", Coinbases}, {"SQLite recovery", Persistence}, {"durable registration limits", Registration}, {"protocol and TLS", ProtocolAndTls}, {"native MercaHash verifier", NativeVerifier}}) {
        try {
            run();
            std::cout << "PASS " << name << "\n";
        } catch (const std::exception& e) {
            ++failures;
            std::cerr << "FAIL " << name << ": " << e.what() << "\n";
        }
    }
    std::cout << checks << " checks, " << failures << " failing groups\n";
    return failures ? 1 : 0;
}
