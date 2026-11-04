// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_SCRIPT_PQGROUP_CONTEXT_H
#define BITCOIN_SCRIPT_PQGROUP_CONTEXT_H

#include <script/interpreter.h>
#include <script/pqgroup.h>

namespace mercaweave {
class ProductionPathTestAccess;

/** Nonserializable fixture capability. No production constructor, setting, RPC,
 * deployment or wire field creates one. Only the separately guarded test helper
 * can install it on disposable Chainstate/CWallet instances. */
class IsolatedExecution final {
    const B32 m_genesis;
    explicit IsolatedExecution(B32 genesis) : m_genesis(genesis) {}
    friend class ProductionPathTestAccess;
public:
    const B32& Genesis() const { return m_genesis; }
};

/** Group-only authorization. Non-v3 inputs MUST still execute ordinary Script.
 * Owns all data needed by asynchronous jobs; no view pointers, wallet objects,
 * callbacks, mutable proof flags or cross-transaction cache entries. */
class GroupContext final {
    const CTransactionRef m_tx;
    const std::vector<CTxOut> m_spent;
    const B32 m_genesis;
    const script_verify_flags m_flags;
    const std::shared_ptr<const Authorization> m_groups;
    const Statistics m_statistics;
    const std::vector<bool> m_authorized;
    PrecomputedTransactionData m_native;

    GroupContext(const CTransaction&, std::vector<CTxOut>, B32,
                 script_verify_flags, const Result&);
public:
    static std::shared_ptr<const GroupContext> Prepare(
        const IsolatedExecution&, const CTransaction&, std::vector<CTxOut>,
        script_verify_flags, const uint256& genesis, std::optional<Error>& error,
        Statistics* work = nullptr);

    const CTransaction& Transaction() const { return *m_tx; }
    const std::vector<CTxOut>& Spent() const { return m_spent; }
    const PrecomputedTransactionData& Native() const { return m_native; }
    const Statistics& Work() const { return m_statistics; }
    bool Authorize(unsigned int, const CScriptWitness&, const Bytes& program,
                   script_verify_flags, bool is_p2sh) const;
    bool Matches(const CTransaction&, const std::vector<CTxOut>&,
                 script_verify_flags, const uint256& genesis) const;
    bool MatchesJob(const CTransaction*, const PrecomputedTransactionData*,
                    unsigned int, const CTxOut&, script_verify_flags,
                    std::optional<uint256>) const;
};
} // namespace mercaweave
#endif // BITCOIN_SCRIPT_PQGROUP_CONTEXT_H
