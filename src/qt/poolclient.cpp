// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <qt/poolclient.h>

#include <consensus/consensus.h>
#include <consensus/merkle.h>
#include <crypto/common.h>
#include <hash.h>
#include <interfaces/handler.h>
#include <pow.h>
#include <streams.h>
#include <support/cleanse.h>
#include <util/strencodings.h>
#include <util/string.h>
#include <validation.h>

#include <QDateTime>
#include <QFile>
#include <QSslConfiguration>
#include <QSslSocket>
#include <boost/multiprecision/cpp_int.hpp>

#include <algorithm>
#include <set>
#include <stdexcept>

namespace mining {
namespace {
constexpr size_t MAX_FRAME{8 * 1024 * 1024};
using Integer = boost::multiprecision::cpp_int;
void Require(bool condition)
{
    if (!condition) throw std::runtime_error{"Invalid or inconsistent pool work"};
}
void Fields(const UniValue& value, const std::vector<std::string>& required, const std::vector<std::string>& optional = {})
{
    Require(value.isObject());
    std::set<std::string> seen;
    for (const auto& key : value.getKeys()) {
        Require(seen.insert(key).second);
        Require(std::find(required.begin(), required.end(), key) != required.end() || std::find(optional.begin(), optional.end(), key) != optional.end());
    }
    for (const auto& key : required)
        Require(seen.contains(key));
}
void UniqueKeys(const UniValue& value, unsigned int depth = 0)
{
    Require(depth <= 32);
    if (value.isObject()) {
        std::set<std::string> seen;
        for (const auto& key : value.getKeys())
            Require(seen.insert(key).second);
    }
    if (value.isObject() || value.isArray())
        for (const auto& child : value.getValues())
            UniqueKeys(child, depth + 1);
}
uint256 HashHex(const UniValue& value)
{
    const auto& text{value.get_str()};
    Require(text.size() == 64 && IsHex(text));
    auto hash{uint256::FromHex(text)};
    Require(hash.has_value());
    return *hash;
}
std::vector<unsigned char> Bytes(const UniValue& value, size_t maximum)
{
    const auto& text{value.get_str()};
    Require(!text.empty() && text.size() <= maximum * 2 && text.size() % 2 == 0 && IsHex(text));
    return ParseHex(text);
}
Integer Decimal(const UniValue& value)
{
    const auto& text{value.get_str()};
    Require(!text.empty() && text.size() <= 1024 && (text.size() == 1 || text[0] != '0'));
    Integer result{0};
    for (char c : text) {
        Require(c >= '0' && c <= '9');
        result = result * 10 + (c - '0');
    }
    return result;
}
std::string Digest(const std::string& domain, const std::string& value) { return (HashWriter{} << domain << value).GetHash().GetHex(); }
bool Pq(const CScript& script)
{
    int version{0};
    std::vector<unsigned char> program;
    return script.size() == 34 && script.IsWitnessProgram(version, program) && version == 2 && program.size() == 32;
}
struct RemoteError {
    int code;
    UniValue frame;
};
struct CleanseHello {
    UniValue& value;
    ~CleanseHello()
    {
        if (value["resume_token"].isStr()) {
            const auto& token{value["resume_token"].get_str()};
            memory_cleanse(const_cast<char*>(token.data()), token.size());
        }
    }
};
bool Transient(int code) { return code == 105 || code == 106 || code == 107 || code == 110 || code == 111 || code == 112 || code == 113; }
} // namespace

bool ValidPoolEndpoint(const PoolEndpoint& endpoint)
{
    if (endpoint.host.empty() || endpoint.host.size() > 253 || endpoint.port == 0 || endpoint.certificate_file.size() > 4096) return false;
    for (unsigned char c : endpoint.host)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == ':')) return false;
    return true;
}
UniValue ParsePoolFrame(const std::string& frame)
{
    Require(!frame.empty() && frame.size() <= MAX_FRAME && frame.find('\0') == std::string::npos);
    // Bound depth and structural items before allocating UniValue nodes. A
    // dense array can otherwise amplify a small frame into hundreds of MiB.
    // M3 transaction hex and 1,000-recipient manifests fit within this budget.
    unsigned int depth{0};
    size_t items{0};
    bool quoted{false}, escape{false};
    for (char c : frame) {
        if (quoted) {
            if (escape)
                escape = false;
            else if (c == '\\')
                escape = true;
            else if (c == '"')
                quoted = false;
        } else if (c == '"')
            quoted = true;
        else if (c == '[' || c == '{') {
            Require(++depth <= 32);
            Require(++items <= MAX_FRAME / 16);
        } else if (c == ',' || c == ':') {
            Require(++items <= MAX_FRAME / 16);
        } else if (c == ']' || c == '}') {
            Require(depth > 0);
            --depth;
        }
    }
    Require(depth == 0 && !quoted);
    Require(QString::fromUtf8(frame.data(), static_cast<qsizetype>(frame.size())).toUtf8().toStdString() == frame);
    UniValue result;
    Require(result.read(frame) && result.isObject());
    UniqueKeys(result);
    return result;
}
LocalPoolValidation VerifyLocalPoolCandidate(interfaces::Mining* local, const CBlock& block)
{
    if (!local || local->isInitialBlockDownload()) return LocalPoolValidation::UNAVAILABLE;
    const auto tip{local->getTip()};
    if (!tip || tip->hash != block.hashPrevBlock) return LocalPoolValidation::UNAVAILABLE;
    std::string reason, debug;
    if (local->checkBlock(block, {.check_merkle_root = true, .check_pow = false}, reason, debug)) return LocalPoolValidation::VALID;
    return reason == "inconclusive-not-best-prevblk" ? LocalPoolValidation::STALE : LocalPoolValidation::INVALID;
}

PoolJob ValidatePoolJob(const UniValue& message, const UniValue& audit,
                        const std::string& network, const std::string& genesis,
                        const std::string& session, const CScript& payout,
                        const Consensus::Params& consensus, int64_t server_time)
{
    Fields(message, {"job_id", "snapshot_id", "warmup", "fee_base_units", "header", "coinbase", "share_target", "network_target", "expires", "parent", "extranonce_namespace", "merkle_path"});
    const std::vector<std::string> snapshot_keys{"accounting_version", "network", "genesis", "parent", "height", "template_id", "template", "reward", "cutoff", "reference_work", "window_limit", "total_work", "oldest_sequence", "oldest_used", "rounding", "rounding_seed", "allocations", "pool_name", "marker", "fee_base_units", "warmup"};
    Fields(audit, snapshot_keys, {"snapshot_id", "coinbase_txid", "serialized_bytes", "candidates"});
    Require(audit["network"].get_str() == network && audit["genesis"].get_str() == genesis);
    Require(audit["accounting_version"].getInt<int>() == 1 && audit["rounding"].get_str() == "largest-remainder/domain-sha256d-v1");
    Require(message["fee_base_units"].getInt<int64_t>() == 0 && audit["fee_base_units"].getInt<int64_t>() == 0);
    Require(message["extranonce_namespace"].get_str() == session && Decimal(UniValue{session}) > 0);
    PoolJob job;
    job.id = HashHex(message["job_id"]).GetHex();
    job.snapshot = HashHex(message["snapshot_id"]).GetHex();
    UniValue snapshot{UniValue::VOBJ};
    for (const auto& key : snapshot_keys)
        snapshot.pushKV(key, audit[key]);
    Require(Digest("MCA-POOL/1/snapshot", snapshot.write()) == job.snapshot && audit["snapshot_id"].get_str() == job.snapshot);
    auto header{Bytes(message["header"], 80)};
    Require(header.size() == 80);
    SpanReader reader{header};
    reader >> job.work.header;
    Require(job.work.header.nNonce == 0 && job.work.header.hashPrevBlock == HashHex(message["parent"]));
    const auto target{DeriveTarget(job.work.header.nBits, consensus.powLimit)};
    Require(target.has_value() && *target == UintToArith256(HashHex(message["network_target"])));
    job.work.target = UintToArith256(HashHex(message["share_target"]));
    Require(job.work.target >= *target);
    job.expires = message["expires"].getInt<int64_t>();
    Require(server_time > 0 && server_time <= UINT32_MAX && job.expires > server_time && job.expires - server_time <= 7200);
    Require(job.work.header.nTime <= server_time + 7200);
    auto coinbase_data{Bytes(message["coinbase"], 128 * 1024)};
    SpanReader coinbase_reader{coinbase_data};
    CMutableTransaction cb;
    coinbase_reader >> TX_WITH_WITNESS(cb);
    Require(coinbase_reader.empty() && cb.version == 2 && cb.nLockTime == 0 && cb.vin.size() == 1 && cb.vin[0].prevout.IsNull());
    Require(cb.vin[0].nSequence == CTxIn::SEQUENCE_FINAL && cb.vin[0].scriptWitness.stack == std::vector<std::vector<unsigned char>>{std::vector<unsigned char>(32, 0)});
    const auto height{audit["height"].getInt<int>()};
    Require(height > 0);
    CScript prefix;
    prefix << height;
    const auto& script{cb.vin[0].scriptSig};
    Require(script.size() >= prefix.size() && script.size() <= 100 && std::equal(prefix.begin(), prefix.end(), script.begin()));
    auto pc{script.begin() + prefix.size()};
    opcodetype op;
    std::vector<unsigned char> marker, extra;
    Require(script.GetOp(pc, op, marker) && op == static_cast<opcodetype>(10) && std::string(marker.begin(), marker.end()) == "MCA-POOL/1");
    Require(script.GetOp(pc, op, extra) && op == static_cast<opcodetype>(16) && extra.size() == 16 && pc == script.end());
    job.serial = ReadLE64(extra.data() + 8);
    Require(util::ToString(ReadLE64(extra.data())) == session && job.serial > 0 && job.serial <= INT64_MAX);
    Require(audit["marker"].get_str() == "MCA-POOL/1");
    job.work.coinbase = MakeTransactionRef(cb);
    Require(job.work.coinbase->GetHash().ToUint256() == HashHex(audit["coinbase_txid"]));
    Require(message["merkle_path"].isArray() && message["merkle_path"].size() <= 32);
    uint256 root{job.work.coinbase->GetHash().ToUint256()};
    for (const auto& entry : message["merkle_path"].getValues())
        root = Hash(root, HashHex(entry));
    Require(root == job.work.header.hashMerkleRoot);
    const auto& t{audit["template"]};
    Require(t.isObject() && Digest("MCA-POOL/1/template", t.write()) == audit["template_id"].get_str());
    Require(t["height"].getInt<int>() == height && HashHex(t["previousblockhash"]) == job.work.header.hashPrevBlock && HashHex(audit["parent"]) == job.work.header.hashPrevBlock);
    Require(t["bits"].get_str().size() == 8 && IsHex(t["bits"].get_str()));
    Require(std::stoul(t["bits"].get_str(), nullptr, 16) == job.work.header.nBits && t["version"].getInt<int32_t>() == job.work.header.nVersion);
    Require(UintToArith256(HashHex(t["target"])) == *target);
    Require(std::max(t["curtime"].getInt<uint32_t>(), t["mintime"].getInt<uint32_t>()) == job.work.header.nTime);
    Require(t["coinbaseaux"].isObject() && t["coinbaseaux"].empty() && t["transactions"].isArray());
    CBlock block{job.work.header};
    block.vtx.push_back(job.work.coinbase);
    for (const auto& tx : t["transactions"].getValues()) {
        auto bytes{Bytes(tx["data"], MAX_FRAME / 2)};
        SpanReader input{bytes};
        CMutableTransaction decoded;
        input >> TX_WITH_WITNESS(decoded);
        Require(input.empty());
        auto ref{MakeTransactionRef(std::move(decoded))};
        Require(ref->GetHash().ToUint256() == HashHex(tx["txid"]) && ref->GetWitnessHash().ToUint256() == HashHex(tx["hash"]));
        block.vtx.push_back(std::move(ref));
    }
    // Run the existing context-free consensus checks even while the local
    // node is syncing or lacks this parent. Full proposal checks follow later.
    BlockValidationState validation;
    Require(CheckBlock(block, validation, consensus, /*fCheckPOW=*/false, /*fCheckMerkleRoot=*/true));
    bool mutated{false};
    Require(BlockMerkleRoot(block, &mutated) == root && !mutated);
    const auto path{TransactionMerklePath(block, 0)};
    Require(path.size() == message["merkle_path"].size());
    for (size_t i{0}; i < path.size(); ++i)
        Require(path[i] == HashHex(message["merkle_path"][i]));
    auto witness{ParseHex("6a24aa21a9ed")};
    const auto commitment{Hash(BlockWitnessMerkleRoot(block), uint256{})};
    witness.insert(witness.end(), commitment.begin(), commitment.end());
    const CScript witness_script{witness.begin(), witness.end()};
    Require(HexStr(witness_script) == t["default_witness_commitment"].get_str());
    Require(!cb.vout.empty() && cb.vout.back().nValue == 0 && cb.vout.back().scriptPubKey == witness_script);
    const auto reward{audit["reward"].getInt<CAmount>()};
    Require(MoneyRange(reward) && reward > 0 && reward == t["coinbasevalue"].getInt<CAmount>() && job.work.coinbase->GetValueOut() == reward);
    const auto serialized_size{GetSerializeSize(TX_WITH_WITNESS(block))};
    Require(serialized_size == audit["serialized_bytes"].getInt<uint64_t>() && serialized_size <= t["sizelimit"].getInt<uint64_t>() && serialized_size <= GetMaxBlockCapacityBytes(height));
    Require(audit["allocations"].isArray() && !audit["allocations"].empty() && audit["allocations"].size() <= 1000);
    const bool warmup{message["warmup"].get_bool()};
    Require(warmup == audit["warmup"].get_bool());
    const Integer total{Decimal(audit["total_work"])};
    Require(total > 0 && Decimal(audit["reference_work"]) == (Integer{1} << 256) / (Integer{"0x" + target->GetHex()} + 1));
    const Integer reference{Decimal(audit["reference_work"])}, window{Decimal(audit["window_limit"])};
    Require(window >= reference && window <= reference * 100 && window % reference == 0 && total <= window);
    const auto cutoff{audit["cutoff"].getInt<int64_t>()}, oldest{audit["oldest_sequence"].getInt<int64_t>()};
    Require(cutoff >= 0 && oldest >= 0 && oldest <= cutoff && Decimal(audit["oldest_used"]) <= total);
    if (!warmup) Require(oldest > 0 && Decimal(audit["oldest_used"]) > 0);
    const auto seed{Digest("MCA-PPLNS/1/seed", network + ":" + genesis + ":" + job.work.header.hashPrevBlock.GetHex() + ":" + audit["template_id"].get_str() + ":" + util::ToString(audit["cutoff"].getInt<int64_t>()) + ":" + audit["window_limit"].get_str() + ":" + util::ToString(reward))};
    Require(seed == audit["rounding_seed"].get_str());
    struct Allocation {
        Integer remainder;
        std::string tie, script;
        CAmount amount, base;
    };
    std::vector<Allocation> allocations;
    std::string previous;
    Integer sum{0};
    CAmount paid{0}, base_paid{0};
    size_t output{0};
    for (const auto& a : audit["allocations"].getValues()) {
        Fields(a, {"script", "work", "amount", "remainder", "tie"});
        auto bytes{Bytes(a["script"], 34)};
        const CScript recipient{bytes.begin(), bytes.end()};
        const auto hex{HexStr(recipient)};
        Require(Pq(recipient) && hex == a["script"].get_str() && (previous.empty() || previous < hex));
        previous = hex;
        const Integer work{Decimal(a["work"])};
        Require(work > 0);
        sum += work;
        const Integer product{Integer{reward} * work};
        const Integer quotient{product / total};
        Require(quotient <= reward);
        const CAmount base{quotient.convert_to<CAmount>()}, amount{a["amount"].getInt<CAmount>()};
        Require(MoneyRange(amount) && amount <= reward - paid && base <= reward - base_paid);
        const auto tie{Digest("MCA-PPLNS/1/remainder", seed + ":" + hex)};
        Require(Decimal(a["remainder"]) == product % total && a["tie"].get_str() == tie);
        allocations.push_back({product % total, tie, hex, amount, base});
        paid += amount;
        base_paid += base;
        if (amount > 0) {
            Require(output + 1 < cb.vout.size() && cb.vout[output].nValue == amount && cb.vout[output].scriptPubKey == recipient);
            ++output;
        }
        if (warmup) Require(recipient == payout && amount == reward && work == 1);
    }
    Require(sum == total && paid == reward && output + 1 == cb.vout.size());
    std::sort(allocations.begin(), allocations.end(), [](const auto& a, const auto& b) { if (a.remainder != b.remainder) return a.remainder > b.remainder; if (a.tie != b.tie) return a.tie < b.tie; return a.script < b.script; });
    Require(reward - base_paid <= static_cast<CAmount>(allocations.size()));
    for (size_t i{0}; i < allocations.size(); ++i)
        Require(allocations[i].amount == allocations[i].base + (i < static_cast<size_t>(reward - base_paid) ? 1 : 0));
    job.work.continuous = true;
    job.work.valid = std::make_shared<std::atomic<bool>>(true);
    job.work.deadline = std::chrono::steady_clock::now() + std::chrono::seconds{job.expires - server_time};
    job.manifest = std::make_shared<const std::string>(audit.write());
    job.candidate = std::make_shared<const CBlock>(std::move(block));
    return job;
}

PoolStats PoolState::Snapshot() const
{
    std::lock_guard lock{mutex};
    return stats;
}
void PoolState::ForgetSession()
{
    std::lock_guard lock{mutex};
    memory_cleanse(token.data(), token.size());
    token.clear();
    session.clear();
    last_job.clear();
    last_serial = 0;
    pending.reset();
}
PoolWorkProvider::PoolWorkProvider(PoolEndpoint endpoint, std::string network, std::string genesis,
                                   std::string destination, CScript payout, Consensus::Params consensus,
                                   std::shared_ptr<PoolState> state, std::shared_ptr<std::atomic<bool>> cancelled,
                                   std::unique_ptr<interfaces::Handler> unload, std::unique_ptr<interfaces::Mining> local)
    : m_endpoint{std::move(endpoint)}, m_network{std::move(network)}, m_genesis{std::move(genesis)}, m_destination{std::move(destination)}, m_payout{std::move(payout)}, m_consensus{std::move(consensus)}, m_state{std::move(state)}, m_cancelled{std::move(cancelled)}, m_unload{std::move(unload)}, m_local{std::move(local)}
{
    if (!ValidPoolEndpoint(m_endpoint) || !Pq(m_payout) || !m_state) throw std::runtime_error{"Configure a valid TLS pool endpoint and owned native PQ payout"};
    const auto binding{m_network + ":" + m_genesis + ":" + m_endpoint.host + ":" + util::ToString(m_endpoint.port) + ":" + m_endpoint.certificate_file + ":" + HexStr(m_payout)};
    std::lock_guard lock{m_state->mutex};
    if (m_state->binding != binding) {
        m_state->binding = binding;
        memory_cleanse(m_state->token.data(), m_state->token.size());
        m_state->token.clear();
        m_state->session.clear();
        m_state->last_job.clear();
        m_state->last_serial = 0;
        m_state->pending.reset();
    }
    m_last_given = m_state->last_job;
    m_replay = std::move(m_state->pending);
    m_state->pending.reset();
    m_state->stats = {};
}
PoolWorkProvider::~PoolWorkProvider()
{
    {
        std::lock_guard lock{m_state->mutex};
        m_state->pending = std::move(m_replay);
    }
    Disconnect();
    Status("Pool mining stopped");
}
bool PoolWorkProvider::IsCancelled() const { return m_stopped || (m_cancelled && *m_cancelled); }
void PoolWorkProvider::Status(const std::string& text)
{
    std::lock_guard lock{m_state->mutex};
    m_state->stats.status = text;
}
void PoolWorkProvider::Disconnect()
{
    if (m_job && m_job->work.valid) *m_job->work.valid = false;
    m_socket.reset();
    memory_cleanse(m_input.data(), m_input.size());
    m_input.clear();
    m_job.reset();
    m_next.reset();
    m_clean = true;
    std::lock_guard lock{m_state->mutex};
    m_state->stats.connected = false;
    m_state->stats.locally_verified = false;
}
int64_t PoolWorkProvider::ServerTime() const { return m_server_time + std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - m_time_base).count(); }
void PoolWorkProvider::Connect()
{
    Status("Connecting to pool with certificate and hostname verification");
    if (!QSslSocket::supportsSsl()) throw std::runtime_error{"TLS is unavailable in this Qt build"};
    m_socket = std::make_unique<QSslSocket>();
    m_notified_parent.clear();
    m_socket->setReadBufferSize(MAX_FRAME + 1);
    auto configuration{QSslConfiguration::defaultConfiguration()};
    configuration.setProtocol(QSsl::TlsV1_2OrLater);
    configuration.setPeerVerifyMode(QSslSocket::VerifyPeer);
    if (!m_endpoint.certificate_file.empty()) {
        QFile file{QString::fromStdString(m_endpoint.certificate_file)};
        if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) throw std::runtime_error{"Cannot read the selected pool CA certificate"};
        const auto pem{file.read(65537)};
        if (pem.size() > 65536 || !file.atEnd()) throw std::runtime_error{"The selected pool CA certificate exceeds the size limit"};
        auto certificates{QSslCertificate::fromData(pem, QSsl::Pem)};
        if (certificates.empty()) throw std::runtime_error{"The selected file contains no CA certificate"};
        auto trusted{configuration.caCertificates()};
        trusted.append(certificates);
        configuration.setCaCertificates(trusted);
    }
    m_socket->setSslConfiguration(configuration);
    const auto certificate_error{std::make_shared<std::atomic<bool>>(false)};
    const auto errors{QObject::connect(m_socket.get(), &QSslSocket::sslErrors, m_socket.get(), [certificate_error](const QList<QSslError>&) { *certificate_error = true; })};
    m_socket->connectToHostEncrypted(QString::fromStdString(m_endpoint.host), m_endpoint.port);
    const auto deadline{Clock::now() + std::chrono::seconds{10}};
    while (!IsCancelled() && !m_socket->isEncrypted()) {
        m_socket->waitForEncrypted(100);
        if (*certificate_error) {
            QObject::disconnect(errors);
            throw std::runtime_error{"Pool TLS certificate or hostname verification failed"};
        }
        if (Clock::now() >= deadline || m_socket->state() == QAbstractSocket::UnconnectedState) {
            QObject::disconnect(errors);
            throw Offline{};
        }
    }
    QObject::disconnect(errors);
    if (IsCancelled()) throw Offline{};
    UniValue hello{UniValue::VOBJ};
    hello.pushKV("version", 1);
    hello.pushKV("network", m_network);
    hello.pushKV("genesis", m_genesis);
    hello.pushKV("algorithm", "MercaHash-V1");
    SecureString token;
    {
        std::lock_guard lock{m_state->mutex};
        token = m_state->token;
    }
    hello.pushKV(token.empty() ? "payout" : "resume_token", token.empty() ? m_destination : std::string{token.begin(), token.end()});
    UniValue reply;
    CleanseHello cleanse{reply};
    try {
        reply = Request("hello", std::move(hello));
    } catch (const RemoteError& error) {
        if (error.code == 104 && !token.empty()) {
            // An active owner may still be draining. Do not silently allocate a
            // second namespace on a generic session error; retry at most later.
            throw Offline{"Pool session unavailable; retrying. Stop and forget the disconnected session to register again."};
        }
        if (Transient(error.code)) throw Offline{};
        throw std::runtime_error{"Pool rejected protocol, network or native PQ registration"};
    }
    Fields(reply, {"version", "algorithm", "network", "genesis", "session", "resume_token", "extranonce_namespace", "pool_name", "fee_base_units", "heartbeat_seconds"});
    if (reply["version"].getInt<int>() != 1 || reply["algorithm"].get_str() != "MercaHash-V1") throw std::runtime_error{"Pool protocol or mining algorithm is unsupported"};
    if (reply["network"].get_str() != m_network || reply["genesis"].get_str() != m_genesis) throw std::runtime_error{"Pool network or genesis differs from this wallet's network"};
    if (reply["fee_base_units"].getInt<int64_t>() != 0) throw std::runtime_error{"Pool did not confirm the required zero-fee policy"};
    Require(Decimal(reply["session"]) > 0 && Decimal(reply["session"]) <= INT64_MAX && reply["session"].get_str() == reply["extranonce_namespace"].get_str());
    HashHex(reply["resume_token"]);
    m_session = reply["session"].get_str();
    {
        std::lock_guard lock{m_state->mutex};
        Require(m_state->session.empty() || m_state->session == m_session);
        m_state->session = m_session;
        const auto& bearer{reply["resume_token"].get_str()};
        m_state->token.assign(bearer.begin(), bearer.end());
    }
    m_heartbeat = reply["heartbeat_seconds"].getInt<int>();
    Require(m_heartbeat >= 1 && m_heartbeat <= 3600);
    const auto time{Request("ping")};
    Fields(time, {"time"});
    m_server_time = time["time"].getInt<int64_t>();
    m_time_base = Clock::now();
    Require(m_server_time > 0 && m_server_time <= UINT32_MAX);
    m_ping = Clock::now();
    m_poll = Clock::now();
    m_clean = false;
    m_backoff = 1;
    {
        std::lock_guard lock{m_state->mutex};
        m_state->stats.connected = true;
        ++m_state->stats.reconnects;
    }
    Status("TLS certificate and hostname verified; pool session registered");
    if (m_replay) {
        const auto saved{*m_replay};
        m_replay.reset();
        UniValue p{UniValue::VOBJ};
        p.pushKV("job_id", saved.first.id);
        p.pushKV("nonce", saved.second.nNonce);
        try {
            Receipt(Request("submit", p), saved.first, saved.second);
        } catch (const RemoteError& error) {
            if (error.code == 108 && !error.frame["receipt"].isNull())
                Receipt(error.frame["receipt"], saved.first, saved.second);
            else if (error.code == 107) {
                std::lock_guard lock{m_state->mutex};
                ++m_state->stats.stale;
            } else if (Transient(error.code)) {
                m_replay = saved;
                throw Offline{};
            } else {
                std::lock_guard lock{m_state->mutex};
                ++m_state->stats.rejected;
            }
        } catch (const Offline&) {
            m_replay = saved;
            throw;
        }
    }
}
void PoolWorkProvider::ReadAvailable()
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState || IsCancelled()) throw Offline{};
    auto bytes{m_socket->read(static_cast<qint64>(MAX_FRAME + 1 - m_input.size()))};
    m_input.append(bytes.constData(), static_cast<size_t>(bytes.size()));
    memory_cleanse(bytes.data(), static_cast<size_t>(bytes.size()));
    Require(m_input.size() <= MAX_FRAME);
}
std::optional<UniValue> PoolWorkProvider::NextFrame()
{
    const auto end{m_input.find('\n')};
    if (end == std::string::npos) return {};
    if (Clock::now() - m_rate_start >= std::chrono::seconds{1}) {
        m_rate_start = Clock::now();
        m_frames = 0;
    }
    Require(++m_frames <= 64);
    std::string raw{m_input.substr(0, end)};
    UniValue frame;
    try {
        frame = ParsePoolFrame(raw);
    } catch (...) {
        memory_cleanse(raw.data(), raw.size());
        throw;
    }
    memory_cleanse(raw.data(), raw.size());
    memory_cleanse(m_input.data(), end + 1);
    m_input.erase(0, end + 1);
    return frame;
}
void PoolWorkProvider::Notification(const UniValue& frame)
{
    Fields(frame, {"method", "params"});
    Require(frame["method"].get_str() == "parent");
    Fields(frame["params"], {"parent", "clean_jobs"});
    HashHex(frame["params"]["parent"]);
    Require(frame["params"]["clean_jobs"].get_bool());
    m_clean = true;
    m_notified_parent = frame["params"]["parent"].get_str();
    if (m_job && m_job->work.valid) *m_job->work.valid = false;
}
UniValue PoolWorkProvider::Request(const std::string& method, UniValue params)
{
    if (!m_socket || IsCancelled()) throw Offline{};
    if (m_id == INT32_MAX) m_id = 0;
    const auto id{++m_id};
    UniValue request{UniValue::VOBJ};
    request.pushKV("id", id);
    request.pushKV("method", method);
    request.pushKV("params", std::move(params));
    auto encoded{request.write() + "\n"};
    SecureString wire{encoded.begin(), encoded.end()};
    memory_cleanse(encoded.data(), encoded.size());
    if (method == "hello" && !request["params"]["resume_token"].isNull()) {
        const auto& bearer{request["params"]["resume_token"].get_str()};
        memory_cleanse(const_cast<char*>(bearer.data()), bearer.size());
    }
    Require(wire.size() <= 4096 && m_socket->bytesToWrite() <= 4096);
    if (m_socket->write(wire.data(), static_cast<qint64>(wire.size())) != static_cast<qint64>(wire.size())) throw Offline{};
    const auto deadline{Clock::now() + std::chrono::seconds{10}};
    while (!IsCancelled() && Clock::now() < deadline) {
        if (m_socket->bytesToWrite()) m_socket->waitForBytesWritten(100);
        ReadAvailable();
        while (auto frame = NextFrame()) {
            if ((*frame)["id"].isNull()) {
                Notification(*frame);
                continue;
            }
            Fields(*frame, {"id", "result", "error"}, {"receipt"});
            Require((*frame)["id"].getInt<int32_t>() == id);
            if (!(*frame)["error"].isNull()) {
                Require((*frame)["result"].isNull());
                Fields((*frame)["error"], {"code", "message"});
                Require((*frame)["error"]["message"].isStr() && (*frame)["error"]["message"].get_str().size() <= 1024);
                throw RemoteError{(*frame)["error"]["code"].getInt<int>(), *frame};
            }
            Require((*frame)["result"].isObject() && (*frame)["receipt"].isNull());
            UniValue result{(*frame)["result"]};
            if (method == "hello" && (*frame)["result"]["resume_token"].isStr()) {
                const auto& bearer{(*frame)["result"]["resume_token"].get_str()};
                memory_cleanse(const_cast<char*>(bearer.data()), bearer.size());
            }
            return result;
        }
        m_socket->waitForReadyRead(100);
    }
    throw Offline{};
}
std::optional<PoolJob> PoolWorkProvider::FetchJob()
{
    auto message{Request("getjob")};
    if (m_job && message["job_id"].get_str() == m_job->id) {
        Require(message.write() == m_next_message);
        return {};
    }
    UniValue params{UniValue::VOBJ};
    params.pushKV("job_id", message["job_id"]);
    auto manifest{Request("manifest", params)};
    auto job{ValidatePoolJob(message, manifest, m_network, m_genesis, m_session, m_payout, m_consensus, ServerTime())};
    {
        std::lock_guard lock{m_state->mutex};
        Require(job.serial > m_state->last_serial || (job.id == m_state->last_job && job.serial == m_state->last_serial));
    }
    if (!m_notified_parent.empty() && job.work.header.hashPrevBlock.GetHex() != m_notified_parent) return {};
    const auto local{VerifyLocalPoolCandidate(m_local.get(), *job.candidate)};
    if (local == LocalPoolValidation::STALE) return {};
    if (local == LocalPoolValidation::INVALID) throw std::runtime_error{"Pool candidate was rejected by local Core proposal validation"};
    m_next_message = message.write();
    UniValue details{UniValue::VOBJ};
    for (const auto& key : {"accounting_version", "network", "genesis", "parent", "height", "reward", "cutoff", "reference_work", "window_limit", "total_work", "warmup", "fee_base_units", "snapshot_id", "coinbase_txid", "serialized_bytes", "allocations"})
        details.pushKV(key, manifest[key]);
    auto display{details.write(2)};
    if (display.size() > 256 * 1024) display = display.substr(0, 256 * 1024) + "\n[Preview limit reached; the full validated manifest remains available through M3's public audit API.]";
    auto preview{std::make_shared<const std::string>(std::move(display))};
    {
        std::lock_guard lock{m_state->mutex};
        m_state->stats.commitment = job.snapshot;
        m_state->stats.manifest = job.manifest;
        m_state->stats.locally_verified = local == LocalPoolValidation::VALID;
        m_state->stats.details = std::move(preview);
    }
    return job;
}
std::optional<MiningJob> PoolWorkProvider::GetJob()
{
    if (IsCancelled() || Clock::now() < m_retry) return {};
    try {
        if (!m_socket) Connect();
        if (!m_next) m_next = FetchJob();
        if (!m_next || m_next->id == m_last_given) {
            m_next.reset();
            m_retry = Clock::now() + std::chrono::seconds{1};
            Status("Waiting for a fresh pool nonce namespace");
            return {};
        }
        m_job = std::move(m_next);
        m_next.reset();
        m_last_given = m_job->id;
        m_clean = false;
        {
            std::lock_guard lock{m_state->mutex};
            m_state->last_job = m_last_given;
            m_state->last_serial = m_job->serial;
        }
        m_poll = Clock::now();
        Status("Mining pool shares; payout snapshot checked against the candidate");
        return m_job->work;
    } catch (const Offline& error) {
        Disconnect();
        m_retry = Clock::now() + std::chrono::seconds{m_backoff};
        m_backoff = std::min(30, m_backoff * 2);
        Status(error.reason.empty() ? "Pool disconnected; hashing paused while reconnecting" : error.reason);
        return {};
    } catch (const RemoteError& error) {
        if (!Transient(error.code)) throw std::runtime_error{"Pool rejected a job request"};
        m_retry = Clock::now() + std::chrono::seconds{2};
        Status(error.code == 106 ? "Pool has no PPLNS work; operator warm-up is required" : "Pool temporarily cannot supply work");
        return {};
    }
}
bool PoolWorkProvider::IsCurrent(const MiningJob& job)
{
    if (IsCancelled() || !m_socket || !m_job || m_job->expires <= ServerTime()) return false;
    try {
        m_socket->waitForReadyRead(1);
        ReadAvailable();
        while (auto frame = NextFrame())
            Notification(*frame);
        if (Clock::now() - m_ping >= std::chrono::seconds{std::max(1, m_heartbeat / 3)}) {
            Request("ping");
            m_ping = Clock::now();
        }
        if (Clock::now() - m_status_time >= std::chrono::seconds{10}) {
            const auto reported{Request("status")};
            Require(reported["fee_base_units"].getInt<int64_t>() == 0);
            const auto shares{reported["accepted_shares"].getInt<uint64_t>()};
            const auto connections{reported["connected_sessions"].getInt<uint64_t>()};
            {
                std::lock_guard lock{m_state->mutex};
                m_state->stats.reported = util::ToString(shares) + " accepted shares; " + util::ToString(connections) + " connected sessions (whole pool)";
            }
            m_status_time = Clock::now();
            if (!reported["healthy"].get_bool()) {
                if (m_job->work.valid) *m_job->work.valid = false;
                return false;
            }
        }
        if (m_clean) return false;
        if (Clock::now() - m_poll >= std::chrono::seconds{2}) {
            m_next = FetchJob();
            m_poll = Clock::now();
            if (m_next) return false;
        }
        return job.header.GetHash() == m_job->work.header.GetHash();
    } catch (const Offline&) {
        Disconnect();
        Status("Pool disconnected; hashing paused while reconnecting");
        return false;
    } catch (const RemoteError& error) {
        if (!Transient(error.code)) throw std::runtime_error{"Pool rejected a work refresh"};
        Status("Pool work paused");
        return false;
    }
}
bool PoolWorkProvider::Receipt(const UniValue& receipt, const PoolJob& job, const CBlockHeader& solution)
{
    Fields(receipt, {"sequence", "receipt_id", "work_id", "job_id", "snapshot_id", "score", "accepted", "network_candidate"});
    Require(receipt["accepted"].get_bool() && receipt["job_id"].get_str() == job.id && receipt["snapshot_id"].get_str() == job.snapshot && HashHex(receipt["work_id"]) == solution.GetHash());
    Require(Decimal(receipt["sequence"]) > 0 && Decimal(receipt["sequence"]) <= INT64_MAX);
    const Integer target{"0x" + job.work.target.GetHex()};
    Require(Decimal(receipt["score"]) == (Integer{1} << 256) / (target + 1));
    Require(HashHex(receipt["receipt_id"]).GetHex() == Digest("MCA-PPLNS/1/receipt", m_genesis + ":" + solution.GetHash().GetHex() + ":" + receipt["sequence"].get_str()));
    receipt["network_candidate"].get_bool();
    std::lock_guard lock{m_state->mutex};
    ++m_state->stats.accepted;
    m_state->stats.last_accepted = QDateTime::currentSecsSinceEpoch();
    return true;
}
SubmissionResult PoolWorkProvider::Submit(const MiningJob& work, const CBlockHeader& solution)
{
    if (!m_job || m_clean || IsCancelled()) return {false, false, "Pool work became stale"};
    auto compare{solution};
    compare.nNonce = 0;
    Require(compare.GetHash() == work.header.GetHash() && compare.GetHash() == m_job->work.header.GetHash());
    UniValue params{UniValue::VOBJ};
    params.pushKV("job_id", m_job->id);
    params.pushKV("nonce", solution.nNonce);
    const auto saved{std::make_pair(*m_job, solution)};
    try {
        Receipt(Request("submit", params), saved.first, solution);
        return {true, false, "Pool accepted a share; rewards depend on frozen PPLNS snapshots"};
    } catch (const Offline&) {
        m_replay = saved;
        Disconnect();
        Status("Pool reply lost; reconnecting to recover the share receipt");
        return {false, false, "Pool disconnected"};
    } catch (const RemoteError& error) {
        if (error.code == 108 && !error.frame["receipt"].isNull()) {
            Receipt(error.frame["receipt"], saved.first, solution);
            std::lock_guard lock{m_state->mutex};
            ++m_state->stats.duplicates;
            return {true, false, "Pool returned the original accepted receipt"};
        }
        if (error.code == 110 || error.code == 111 || error.code == 113) {
            m_replay = saved;
            Disconnect();
            m_retry = Clock::now() + std::chrono::seconds{2};
            Status("Pool verifier busy; work paused before retrying the share");
            return {false, false, "Pool verifier busy"};
        }
        std::lock_guard lock{m_state->mutex};
        if (error.code == 107) {
            ++m_state->stats.stale;
            m_clean = true;
        } else if (error.code == 108)
            ++m_state->stats.duplicates;
        else
            ++m_state->stats.rejected;
        return {false, false, error.code == 107 ? "Pool rejected stale work" : "Pool did not accept the share"};
    }
}
} // namespace mining
