// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_TEST_MERCAWEAVE_NODE_VALIDATION_H
#define BITCOIN_TEST_MERCAWEAVE_NODE_VALIDATION_H
#ifndef MERCATURA_MERCAWEAVE_NODE_RESEARCH_TEST_ONLY
#error "Node integration is forbidden in normal builds"
#endif
#include <test/mercaweave/isolated_validation.h>
class CCoinsViewCache;
class CScriptCheck;
class TxValidationState;
struct ValidationCache;

namespace mercaweave::node_research {
// This object can only be minted by complete uncached verification against an
// owned authentic view. Queued CScriptCheck objects retain its lifetime. Native
// precomputation is private; the reference is exposed only to the native API.
class Context final {
    const integration::ViewPtr m_view;
    const uint64_t m_epoch;
    const research::RequestPtr m_request;
    const CTransaction m_tx;
    const std::vector<integration::ScriptJob> m_jobs;
    PrecomputedTransactionData m_native;
    Context(integration::ViewPtr, uint64_t, research::RequestPtr, std::vector<integration::ScriptJob>);
public:
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    static std::shared_ptr<Context> Prepare(const integration::ViewPtr&, const CMutableTransaction&, const B32&, script_verify_flags, int height = 200);
    const CTransaction& Transaction() const { return m_tx; }
    PrecomputedTransactionData& Native() { return m_native; }
    bool Matches(const CTransaction&, const CCoinsViewCache&, const PrecomputedTransactionData&, script_verify_flags, std::optional<uint256>) const;
    bool MatchesJob(const CTransaction*, const PrecomputedTransactionData*, unsigned int, const CTxOut&, script_verify_flags, std::optional<uint256>) const;
    bool IsGrouped(unsigned int) const;
    integration::ScriptResult Run(unsigned int) const;
};
}
bool CheckInputScripts(const CTransaction&, TxValidationState&, const CCoinsViewCache&, script_verify_flags,
                       bool, bool, PrecomputedTransactionData&, ValidationCache&, std::vector<CScriptCheck>*,
                       std::optional<uint256>, std::shared_ptr<const mercaweave::node_research::Context> = {});
#endif // BITCOIN_TEST_MERCAWEAVE_NODE_VALIDATION_H
