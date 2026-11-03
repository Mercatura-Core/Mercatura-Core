// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef MERCATURA_MERCAWEAVE_AUTH_CONTEXT_H
#define MERCATURA_MERCAWEAVE_AUTH_CONTEXT_H
#include "native_v2.h"

namespace mercaweave::research {
// An immutable request is data, never an authorization. Its spent outputs must
// already be authenticated by the caller's UTXO view in absolute vin order.
struct Request final {
    const CMutableTransaction tx;
    const std::vector<CTxOut> spent;
    const B32 genesis;
    const script_verify_flags flags;
    Request(const CMutableTransaction& t, const std::vector<CTxOut>& s, B32 g, script_verify_flags f)
        : tx(t), spent(s), genesis(g), flags(f) {}
};
using RequestPtr = std::shared_ptr<const Request>;
RequestPtr Capture(const CMutableTransaction&, const std::vector<CTxOut>&, const B32&, script_verify_flags);
enum class ContextError { None, MissingContext, InvalidRequest, UnsupportedFlags, BindingMismatch, InputIndex, Invariant, NativeFailure, WrappedV3 };
class ValidatedContext;
using ContextPtr = std::shared_ptr<const ValidatedContext>;
struct Preparation {
    ContextPtr context;
    ContextError error{ContextError::None};
    std::optional<Error> validation_error;
    Statistics work;
    size_t native_checks{0}, native_pq_calls{0};
};
enum class InputKind { Native, V3 };
struct InputEvidence { InputKind kind; uint32_t group; };
// No valid/incomplete public constructor and no mutable proof map. Presence of
// evidence means the factory completed the real verification, not a caller bool.
class ValidatedContext final {
    const RequestPtr m_request;
    const std::shared_ptr<const Authorization> m_groups;
    const std::vector<InputEvidence> m_inputs;
    const CTransaction m_identity;
    ValidatedContext(RequestPtr r, std::shared_ptr<const Authorization> a, std::vector<InputEvidence> inputs)
        : m_request(std::move(r)), m_groups(std::move(a)), m_inputs(std::move(inputs)), m_identity(m_request->tx) {}
    friend Preparation Prepare(const RequestPtr&);
public:
    ValidatedContext(const ValidatedContext&) = delete;
    ValidatedContext& operator=(const ValidatedContext&) = delete;
    static constexpr uint32_t FORMAT{1}; // Local validation semantics; not signed bytes.
    const RequestPtr& Snapshot() const { return m_request; }
    const Authorization& GroupProof() const { return *m_groups; }
    std::span<const InputEvidence> Inputs() const { return m_inputs; }
    const Txid& TxIdentity() const { return m_identity.GetHash(); }
    const Wtxid& WitnessIdentity() const { return m_identity.GetWitnessHash(); }
    bool Matches(const Request& request) const;
};
// Always performs all verification. There is no signature or transaction cache.
Preparation Prepare(const RequestPtr&);
class InputChecks;
struct Binding { std::shared_ptr<const InputChecks> checks; ContextError error{ContextError::None}; };
// Bind once per transaction; subsequent indexed reads are O(1) and thread-safe.
class InputChecks final {
    const ContextPtr m_context;
    explicit InputChecks(ContextPtr c) : m_context(std::move(c)) {}
    friend Binding Bind(const RequestPtr&, const ContextPtr&);
public:
    InputChecks(const InputChecks&) = delete;
    ContextError Check(uint32_t index) const;
};
Binding Bind(const RequestPtr&, const ContextPtr&);
// Convenience entrypoint: full binding check per invocation. For queues use
// Bind once and share InputChecks. Without context ONLY non-v3 native Script
// checks can run; v3 never reaches production unknown-witness permissiveness.
ContextError CheckInput(const RequestPtr&, uint32_t index, const ContextPtr& = {});
} // namespace mercaweave::research
#endif
