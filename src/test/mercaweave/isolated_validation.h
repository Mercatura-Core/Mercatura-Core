// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef MERCATURA_MERCAWEAVE_ISOLATED_VALIDATION_H
#define MERCATURA_MERCAWEAVE_ISOLATED_VALIDATION_H
#ifndef MERCATURA_MERCAWEAVE_RESEARCH_TEST_ONLY
#error "Isolated validation must never be included by a normal node target"
#endif
#include "block_work.h"
#include <coins.h>

namespace mercaweave::integration {
using namespace research;
class Engine;
// Synthetic authoritative chainstate owner. Validation accepts this view, never
// caller-provided spent amounts/scripts. Mutations are serialized and invalidate
// every old proof, including restored identical coins. No disk/network backend.
class View final {
    struct State;
    std::unique_ptr<State> m_state;
    explicit View(const B32&, CCoinsView* = nullptr);
    friend class Engine;
    friend class ScriptJob;
public:
    ~View();
    static std::shared_ptr<View> Create(const B32&);
    void Insert(const COutPoint&, Coin, bool overwrite = false);
    bool Spend(const COutPoint&);
    void SetTip(const uint256&);
    std::optional<Coin> CoinAt(const COutPoint&) const;
    uint64_t Epoch() const;
};
using ViewPtr = std::shared_ptr<View>;
enum class Stage { None, Transaction, Inputs, Network, Flags, Group, Binding, MissingContext, StaleView, InputIndex, Script, Policy, Budget, State, Order };
enum class Policy { ConsensusFixture, CurrentPolicy };
struct FailureInfo {
    Stage stage{Stage::None};
    std::string reason;
    std::optional<Error> group_error;
    ScriptError script_error{SCRIPT_ERR_OK};
    BudgetError budget_error{BudgetError::None};
};
class TransactionProof final {
    struct Data;
    const std::shared_ptr<const Data> m_data;
    explicit TransactionProof(std::shared_ptr<const Data>);
    friend class Engine;
    friend class ScriptJob;
public:
    TransactionProof(const TransactionProof&) = delete;
    const Request& Snapshot() const;
};
using ProofPtr = std::shared_ptr<const TransactionProof>;
struct Prepared {
    ProofPtr proof;
    RequestPtr acquired; // Diagnostic immutable data, not an authorization.
    FailureInfo failure;
    Charge charged;
    Statistics work;
    size_t native_prepare_calls{0};
    CAmount fee{0};
    int64_t fee_bytes{0}, legacy_weight{0}, existing_sigops{0};
};
struct ScriptResult {
    bool valid{false}; // Diagnostic only; never accepted as authority.
    FailureInfo failure;
    uint32_t index{0};
    size_t script_calls{0}, hook_calls{0}, native_calls{0};
};
// Jobs own immutable transaction/proof/precomputation and a fixed absolute input
// index. There is no constructor accepting success booleans or an alternate index.
class ScriptJob final {
    const std::shared_ptr<const TransactionProof::Data> m_data;
    const uint32_t m_index;
    ScriptJob(std::shared_ptr<const TransactionProof::Data>, uint32_t);
    friend class Engine;
public:
    ScriptJob(const ScriptJob&) = delete;
    ScriptJob(ScriptJob&&) = default;
    ScriptResult Run() const;
    uint32_t Index() const { return m_index; }
};
struct Jobs { std::vector<ScriptJob> jobs; FailureInfo failure; };
struct TransactionResult {
    bool valid{false};
    Prepared preparation;
    FailureInfo failure;
    std::vector<ScriptResult> scripts;
};
struct BlockResult {
    bool valid{false}, committed{false};
    FailureInfo failure;
    std::vector<TransactionResult> transactions;
    Charge charged;
    uint64_t bytes{0}, weight{0};
};
class Engine final {
    static Prepared PrepareImpl(const ViewPtr&, const CMutableTransaction&, const B32&, script_verify_flags, int, Policy, Ledger*);
    static TransactionResult Finish(const ViewPtr&, const CMutableTransaction&, const B32&, script_verify_flags, int, Prepared, std::span<const uint32_t>);
public:
    // Genuine CheckTransaction, CheckTxInputs, coin acquisition, optional CURRENT
    // policy, then complete WO03 verification. Never accepts external spent data.
    static Prepared Prepare(const ViewPtr&, const CMutableTransaction&, const B32&, script_verify_flags, int spend_height = 200, Policy = Policy::ConsensusFixture);
    // Rebind exact request and current view epoch. Null, foreign, stale, changed
    // flags/body/witness/genesis/height cannot produce usable script jobs.
    static Jobs Bind(const ViewPtr&, const CMutableTransaction&, const B32&, script_verify_flags, int spend_height, const ProofPtr&);
    static TransactionResult Validate(const ViewPtr&, const CMutableTransaction&, const B32&, script_verify_flags, int spend_height = 200, Policy = Policy::ConsensusFixture, std::span<const uint32_t> order = {});
    static TransactionResult ValidateWire(const ViewPtr&, std::span<const unsigned char>, const B32&, script_verify_flags);
    // Real layered CCoinsViewCache staging. Synthetic candidate only: no PoW,
    // merkle, header, sequence-lock or full ConnectBlock claim. Invalid candidates
    // discard all staged writes. Returned diagnostic receipts contain no proofs.
    static BlockResult Candidate(const ViewPtr&, std::span<const CMutableTransaction>, const B32&, script_verify_flags, bool commit = false, int height = 200, Budget = {});
};
} // namespace mercaweave::integration
#endif
