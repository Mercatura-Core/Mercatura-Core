// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <test/mercaweave/isolated_validation.h>
#include <consensus/tx_check.h>
#include <consensus/tx_verify.h>
#include <consensus/validation.h>
#include <policy/feerate.h>
#include <mutex>
#include <numeric>
#include <shared_mutex>

namespace mercaweave::integration {
struct View::State {
    const B32 genesis;
    CCoinsView empty;
    CCoinsViewCache coins;
    mutable std::shared_mutex mutex;
    uint64_t epoch{0};
    State(B32 g, CCoinsView* backend) : genesis(g), coins(backend ? backend : &empty, true) {}
    void Advance() { epoch = Add(epoch, 1); }
};
View::View(const B32& g, CCoinsView* backend) : m_state(std::make_unique<State>(g, backend)) {}
View::~View() = default;
ViewPtr View::Create(const B32& g) { return ViewPtr(new View(g)); }
void View::Insert(const COutPoint& outpoint, Coin coin, bool overwrite)
{
    std::unique_lock lock(m_state->mutex);
    if (coin.IsSpent()) throw std::invalid_argument("cannot insert a spent coin; use Spend");
    m_state->Advance(); // Even a rejected mutation conservatively invalidates.
    m_state->coins.AddCoin(outpoint, std::move(coin), overwrite);
}
bool View::Spend(const COutPoint& outpoint)
{
    std::unique_lock lock(m_state->mutex);
    m_state->Advance();
    return m_state->coins.SpendCoin(outpoint);
}
void View::SetTip(const uint256& tip)
{
    std::unique_lock lock(m_state->mutex);
    m_state->Advance();
    m_state->coins.SetBestBlock(tip);
}
std::optional<Coin> View::CoinAt(const COutPoint& outpoint) const
{
    std::shared_lock lock(m_state->mutex);
    return m_state->coins.PeekCoin(outpoint); // No population of the cache.
}
uint64_t View::Epoch() const { std::shared_lock lock(m_state->mutex); return m_state->epoch; }

struct TransactionProof::Data {
    const ViewPtr view;
    const uint64_t epoch;
    const int height;
    const RequestPtr request;
    const ContextPtr groups;
    const std::shared_ptr<const InputChecks> inputs;
    const CTransaction tx;
    PrecomputedTransactionData native;
    Data(ViewPtr v, uint64_t e, int h, RequestPtr r, ContextPtr c, std::shared_ptr<const InputChecks> b)
        : view(std::move(v)), epoch(e), height(h), request(std::move(r)), groups(std::move(c)), inputs(std::move(b)), tx(request->tx)
    {
        native.Init(tx, std::vector<CTxOut>(request->spent));
    }
};
TransactionProof::TransactionProof(std::shared_ptr<const Data> data) : m_data(std::move(data)) {}
const Request& TransactionProof::Snapshot() const { return *m_data->request; }
ScriptJob::ScriptJob(std::shared_ptr<const TransactionProof::Data> d, uint32_t i) : m_data(std::move(d)), m_index(i) {}

ScriptResult ScriptJob::Run() const
{
    ScriptResult out;
    out.index = m_index;
    std::shared_lock lock(m_data->view->m_state->mutex);
    if (m_data->epoch != m_data->view->m_state->epoch) {
        out.failure = {Stage::StaleView, "view-epoch-changed"}; return out;
    }
    if (m_index >= m_data->request->spent.size()) { out.failure = {Stage::InputIndex, "input-index"}; return out; }
    // Stack-local checker: no global callback, mutable shared state or TLS.
    // Ordinary native PQ calls delegate directly to production verification.
    class Checker final : public TransactionSignatureChecker {
        const TransactionProof::Data& m_record;
        const uint32_t m_index;
        ScriptResult& m_result;
    public:
        Checker(const TransactionProof::Data& r, uint32_t i, ScriptResult& result)
            : TransactionSignatureChecker(&r.tx, i, r.request->spent[i].nValue, r.native, MissingDataBehavior::FAIL,
                uint256{std::span<const unsigned char>{r.request->genesis}}), m_record(r), m_index(i), m_result(result) {}
        bool CheckMercaturaPQSignature(std::span<const unsigned char> sig, std::span<const unsigned char> pk, ScriptError* error) const override
        {
            ++m_result.native_calls;
            return TransactionSignatureChecker::CheckMercaturaPQSignature(sig, pk, error);
        }
        bool CheckMercaWeaveResearchAuthorization(const CScriptWitness& witness, const Bytes& program,
                script_verify_flags flags, bool wrapped, ScriptError* error) const override
        {
            ++m_result.hook_calls;
            int version;
            Bytes expected;
            const auto& r = *m_record.request;
            const bool ok = !wrapped && program.size() == 32 && flags == r.flags &&
                r.spent[m_index].scriptPubKey.IsWitnessProgram(version, expected) && version == 3 && expected == program &&
                witness.stack == r.tx.vin[m_index].scriptWitness.stack &&
                m_record.groups->Inputs()[m_index].kind == InputKind::V3 &&
                m_record.inputs->Check(m_index) == ContextError::None;
            if (error) *error = ok ? SCRIPT_ERR_OK : SCRIPT_ERR_WITNESS_PROGRAM_MISMATCH;
            return ok;
        }
    } checker(*m_data, m_index, out);
    ScriptError error{SCRIPT_ERR_UNKNOWN_ERROR};
    ++out.script_calls;
    const auto& in = m_data->tx.vin[m_index];
    out.valid = VerifyScript(in.scriptSig, m_data->request->spent[m_index].scriptPubKey, &in.scriptWitness,
        m_data->request->flags, checker, &error);
    if (!out.valid) out.failure = {Stage::Script, "script-verification", {}, error};
    return out;
}

Prepared Engine::AcquireInputs(const ViewPtr& view, const CMutableTransaction& mutable_tx, const B32& genesis,
        script_verify_flags flags, int height, Policy policy)
{
    Prepared out;
    if (!view || height < 0) { out.failure = {Stage::State, "invalid-view-or-height"}; return out; }
    if ((flags & MANDATORY_SCRIPT_VERIFY_FLAGS) != MANDATORY_SCRIPT_VERIFY_FLAGS) {
        out.failure = {Stage::Flags, "mandatory-flags-missing"}; return out;
    }
    const CTransaction tx(mutable_tx);
    RequestPtr request;
    uint64_t epoch;
    {
        // AccessCoin can populate a cache: capture uses an exclusive lock.
        std::unique_lock lock(view->m_state->mutex);
        if (genesis != view->m_state->genesis) { out.failure = {Stage::Network, "wrong-view-genesis"}; return out; }
        TxValidationState state;
        if (!CheckTransaction(tx, state) || !CheckTransactionWitnessResources(tx, state)) {
            out.failure = {Stage::Transaction, state.GetRejectReason()}; return out;
        }
        if (tx.IsCoinBase()) { out.failure = {Stage::Transaction, "fixture-excludes-coinbase-spends"}; return out; }
        // Protect the native validator's authentic-coin precondition when tests
        // intentionally install malformed synthetic coins, before signed sums.
        CAmount sum{0};
        for (const auto& in : tx.vin) if (auto coin = view->m_state->coins.PeekCoin(in.prevout)) {
            if (!MoneyRange(coin->out.nValue) || coin->out.nValue > MAX_MONEY - sum) {
                out.failure = {Stage::Inputs, "bad-txns-inputvalues-outofrange"}; return out;
            }
            sum += coin->out.nValue;
        }
        if (!Consensus::CheckTxInputs(tx, state, view->m_state->coins, height, out.fee)) {
            out.failure = {Stage::Inputs, state.GetRejectReason()}; return out;
        }
        out.existing_sigops = GetTransactionSigOpCost(tx, view->m_state->coins, flags);
        out.fee_bytes = GetTransactionFeeSize(tx, out.existing_sigops, DEFAULT_BYTES_PER_SIGOP);
        out.legacy_weight = GetTransactionWeight(tx);
        if (policy == Policy::CurrentPolicy) {
            std::string reason;
            if (!IsStandardTx(tx, std::nullopt, true, CFeeRate{DUST_RELAY_TX_FEE}, reason)) {
                out.failure = {Stage::Policy, reason}; return out;
            }
            if (!(flags & SCRIPT_VERIFY_MERCAWEAVE)) {
                const auto grouped = [](const CScript& script) {
                    int version;
                    Bytes program;
                    return script.IsWitnessProgram(version, program) && version == 3;
                };
                for (const auto& output : tx.vout) {
                    if (grouped(output.scriptPubKey)) { out.failure = {Stage::Policy, "mercaweave-inactive"}; return out; }
                }
                for (const auto& input : tx.vin) {
                    if (grouped(view->m_state->coins.AccessCoin(input.prevout).out.scriptPubKey)) {
                        out.failure = {Stage::Policy, "mercaweave-inactive"}; return out;
                    }
                }
            }
            if (!AreInputsStandard(tx, view->m_state->coins)) { out.failure = {Stage::Policy, "inputs-not-standard"}; return out; }
            if (!IsWitnessStandard(tx, view->m_state->coins)) { out.failure = {Stage::Policy, "witness-not-standard"}; return out; }
        }
        std::vector<CTxOut> spent;
        spent.reserve(tx.vin.size());
        for (const auto& in : tx.vin) spent.push_back(view->m_state->coins.AccessCoin(in.prevout).out);
        request = Capture(CMutableTransaction(tx), spent, genesis, flags);
        epoch = view->m_state->epoch;
    }
    out.acquired = request;
    out.acquisition_epoch = epoch;
    return out;
}
Prepared Engine::PrepareImpl(const ViewPtr& view, const CMutableTransaction& mutable_tx, const B32& genesis,
        script_verify_flags flags, int height, Policy policy, Ledger* ledger)
{
    auto out = AcquireInputs(view, mutable_tx, genesis, flags, height, policy);
    if (!out.acquired) return out;
    const auto& request = out.acquired;
    const auto epoch = out.acquisition_epoch;
    out.charged = Estimate(*request);
    Ledger single;
    const auto budget = (ledger ? ledger : &single)->Reserve(out.charged);
    if (budget != BudgetError::None) { out.failure = {Stage::Budget, "resource-reservation", {}, SCRIPT_ERR_OK, budget}; return out; }
    auto prepared = research::Prepare(request);
    out.work = prepared.work;
    out.native_prepare_calls = prepared.native_pq_calls;
    if (!prepared.context) { out.failure = {Stage::Group, "transaction-authorization", prepared.validation_error}; return out; }
    const auto bound = research::Bind(request, prepared.context);
    if (!bound.checks) { out.failure = {Stage::Binding, "internal-binding"}; return out; }
    auto data = std::make_shared<const TransactionProof::Data>(view, epoch, height, request, prepared.context, bound.checks);
    {
        std::shared_lock lock(view->m_state->mutex);
        if (epoch != view->m_state->epoch) { out.failure = {Stage::StaleView, "view-mutated-during-verification"}; return out; }
        out.proof = ProofPtr(new TransactionProof(std::move(data)));
    }
    return out;
}
Prepared Engine::Prepare(const ViewPtr& v, const CMutableTransaction& tx, const B32& g, script_verify_flags f, int h, Policy p)
{ return PrepareImpl(v, tx, g, f, h, p, nullptr); }
Jobs Engine::Bind(const ViewPtr& view, const CMutableTransaction& tx, const B32& genesis,
        script_verify_flags flags, int height, const ProofPtr& proof)
{
    Jobs out;
    if (!proof) { out.failure = {Stage::MissingContext, "missing-group-context"}; return out; }
    if (!view) { out.failure = {Stage::State, "missing-view"}; return out; }
    std::shared_lock lock(view->m_state->mutex);
    const auto& data = *proof->m_data;
    if (data.view != view || data.epoch != view->m_state->epoch) { out.failure = {Stage::StaleView, "foreign-or-stale-view"}; return out; }
    if (height != data.height || flags != data.request->flags || !data.groups->GroupProof().Matches(tx, data.request->spent, genesis)) {
        out.failure = {Stage::Binding, "transaction-flags-genesis-height-mismatch"}; return out;
    }
    for (size_t i = 0; i < tx.vin.size(); ++i) out.jobs.push_back(ScriptJob(proof->m_data, static_cast<uint32_t>(i)));
    return out;
}
TransactionResult Engine::Finish(const ViewPtr& view, const CMutableTransaction& tx, const B32& g,
        script_verify_flags f, int h, Prepared preparation, std::span<const uint32_t> order)
{
    TransactionResult out;
    out.preparation = std::move(preparation);
    if (!out.preparation.proof) { out.failure = out.preparation.failure; return out; }
    auto bound = Bind(view, tx, g, f, h, out.preparation.proof);
    if (bound.failure.stage != Stage::None) { out.failure = bound.failure; return out; }
    std::vector<uint32_t> natural;
    if (order.empty()) { natural.resize(bound.jobs.size()); std::iota(natural.begin(), natural.end(), 0); order = natural; }
    std::vector<bool> seen(bound.jobs.size(), false);
    if (order.size() != bound.jobs.size()) { out.failure = {Stage::Order, "incomplete-script-order"}; return out; }
    for (auto index : order) {
        if (index >= bound.jobs.size() || seen[index]) { out.failure = {Stage::Order, "invalid-script-order"}; return out; }
        seen[index] = true;
        auto script = bound.jobs[index].Run();
        out.scripts.push_back(script);
        if (!script.valid) { out.failure = script.failure; return out; }
    }
    std::shared_lock lock(view->m_state->mutex);
    if (out.preparation.proof->m_data->epoch != view->m_state->epoch) {
        out.failure = {Stage::StaleView, "view-mutated-before-final-result"}; return out;
    }
    out.valid = true;
    return out;
}
TransactionResult Engine::Validate(const ViewPtr& v, const CMutableTransaction& tx, const B32& g,
        script_verify_flags f, int h, Policy p, std::span<const uint32_t> order)
{ return Finish(v, tx, g, f, h, Prepare(v, tx, g, f, h, p), order); }
TransactionResult Engine::ValidateWire(const ViewPtr& v, std::span<const unsigned char> wire, const B32& g, script_verify_flags f)
{
    try { return Validate(v, DecodeTransaction(wire), g, f); }
    catch (const Failure& error) {
        TransactionResult out;
        out.failure = {Stage::Transaction, "bounded-decode", error.code};
        return out;
    }
}

BlockResult Engine::Candidate(const ViewPtr& view, std::span<const CMutableTransaction> txs, const B32& g,
        script_verify_flags flags, bool commit, int height, Budget budget)
{
    BlockResult out;
    if (!view) { out.failure = {Stage::State, "missing-view"}; return out; }
    std::unique_lock parent_lock(view->m_state->mutex);
    auto staged = ViewPtr(new View(view->m_state->genesis, &view->m_state->coins));
    staged->m_state->coins.SetBestBlock(view->m_state->coins.GetBestBlock());
    Ledger ledger{budget};
    // Even an empty candidate has header/count/coinbase bytes. Check those
    // before the per-transaction loop, which an empty candidate never enters.
    const auto envelope = ledger.Reserve({});
    if (envelope != BudgetError::None) {
        out.failure = {Stage::Budget, "block-envelope-budget", {}, SCRIPT_ERR_OK, envelope};
        out.bytes = ledger.BlockBytes(); out.weight = ledger.BlockWeight();
        return out;
    }
    std::vector<RequestPtr> requests;
    for (const auto& tx : txs) {
        auto prepared = PrepareImpl(staged, tx, g, flags, height, Policy::ConsensusFixture, &ledger);
        if (prepared.acquired) requests.push_back(prepared.acquired);
        auto result = Finish(staged, tx, g, flags, height, std::move(prepared), {});
        // A staging view's epoch does not track its parent after this function:
        // never export a proof/job rooted in the staging cache.
        result.preparation.proof.reset();
        out.transactions.push_back(std::move(result));
        if (!out.transactions.back().valid) { out.failure = out.transactions.back().failure; break; }
        std::unique_lock stage_lock(staged->m_state->mutex);
        staged->m_state->Advance();
        for (const auto& in : tx.vin) if (!staged->m_state->coins.SpendCoin(in.prevout)) { out.failure = {Stage::State, "coin-update-missing"}; break; }
        if (out.failure.stage != Stage::None) break;
        const CTransaction immutable(tx);
        for (uint32_t i = 0; i < immutable.vout.size(); ++i) if (staged->m_state->coins.HaveCoin({immutable.GetHash(), i})) {
            out.failure = {Stage::State, "coin-update-overwrite"}; break;
        }
        if (out.failure.stage != Stage::None) break;
        AddCoins(staged->m_state->coins, immutable, height);
    }
    out.charged = ledger.Total();
    out.bytes = ledger.BlockBytes();
    out.weight = ledger.BlockWeight();
    out.valid = out.failure.stage == Stage::None && out.transactions.size() == txs.size();
    if (out.valid) {
        const auto block = SerializeWorkload(requests);
        if (GetSerializeSize(TX_WITH_WITNESS(block)) != out.bytes || GetBlockWeight(block) != out.weight) {
            out.valid = false; out.failure = {Stage::State, "block-serialization-accounting"};
        }
    }
    if (out.valid && commit) {
        view->m_state->Advance(); // Check epoch overflow before publishing writes.
        staged->m_state->coins.Flush(false);
        out.committed = true;
    }
    return out;
}
} // namespace mercaweave::integration
