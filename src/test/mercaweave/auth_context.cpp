// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include "auth_context.h"
#include <limits>

namespace mercaweave::research {
namespace {
bool V3(const CScript& s)
{
    int v; Bytes program;
    return s.IsWitnessProgram(v, program) && v == 3;
}
bool WrappedV3(const CTxIn& in, const CTxOut& spent)
{
    if (!spent.scriptPubKey.IsPayToScriptHash()) return false;
    auto pc = in.scriptSig.begin();
    opcodetype opcode{OP_INVALIDOPCODE};
    Bytes data;
    while (pc != in.scriptSig.end()) if (!in.scriptSig.GetOp(pc, opcode, data)) return false;
    return opcode <= OP_PUSHDATA4 && V3(CScript(data.begin(), data.end()));
}
bool Flags(script_verify_flags flags) { return (flags & MANDATORY_SCRIPT_VERIFY_FLAGS) == MANDATORY_SCRIPT_VERIFY_FLAGS; }
}
RequestPtr Capture(const CMutableTransaction& tx, const std::vector<CTxOut>& spent, const B32& genesis, script_verify_flags flags)
{
    return std::make_shared<const Request>(tx, spent, genesis, flags);
}
bool ValidatedContext::Matches(const Request& r) const
{
    return (&r == m_request.get()) || (r.flags == m_request->flags && m_groups->Matches(r.tx, r.spent, r.genesis));
}
Preparation Prepare(const RequestPtr& request)
{
    Preparation out;
    if (!request || request->spent.size() != request->tx.vin.size()) { out.error = ContextError::InvalidRequest; return out; }
    if (!Flags(request->flags)) { out.error = ContextError::UnsupportedFlags; return out; }
    // Defer production precomputation until the group pass actually requests a
    // non-v3 check. Pure-v3 and malformed-v3 transactions do not hash native data.
    std::unique_ptr<test::NativeSession> native;
    auto result = Verify(request->tx, request->spent, request->genesis,
        [&](const auto& tx, const auto& spent, const auto& genesis, uint32_t index) {
            if (&tx != &request->tx || &spent != &request->spent || &genesis != &request->genesis) return false;
            if (!native) native = std::make_unique<test::NativeSession>(tx, spent, genesis, request->flags);
            ScriptError error;
            ++out.native_checks;
            return native->Check(index, error, out.native_pq_calls);
        });
    out.work = result.stats;
    if (!result.valid || !result.authorization) {
        out.error = ContextError::InvalidRequest;
        out.validation_error = result.error;
        return out;
    }
    const auto& mapping = result.authorization->Groups();
    if (out.work.verification_calls != mapping.groups.size() || out.work.ownership_checks != mapping.inputs ||
        result.authorization->GroupDigests().size() != mapping.groups.size()) { out.error = ContextError::Invariant; return out; }
    std::vector<InputEvidence> inputs(request->tx.vin.size(), {InputKind::Native, UINT32_MAX});
    size_t members{0};
    for (size_t k = 0; k < mapping.groups.size(); ++k) for (const auto& member : mapping.groups[k].members) {
        if (member.index >= inputs.size() || inputs[member.index].kind != InputKind::Native) { out.error = ContextError::Invariant; return out; }
        inputs[member.index] = {InputKind::V3, static_cast<uint32_t>(k)};
        ++members;
    }
    if (members != mapping.inputs || out.native_checks != inputs.size() - members) { out.error = ContextError::Invariant; return out; }
    out.context = ContextPtr(new ValidatedContext(request, std::move(result.authorization), std::move(inputs)));
    return out;
}
Binding Bind(const RequestPtr& request, const ContextPtr& context)
{
    if (!context) return {{}, ContextError::MissingContext};
    if (!request) return {{}, ContextError::InvalidRequest};
    if (!context->Matches(*request)) return {{}, ContextError::BindingMismatch};
    return {std::shared_ptr<const InputChecks>(new InputChecks(context)), ContextError::None};
}
ContextError InputChecks::Check(uint32_t index) const
{
    const auto inputs = m_context->Inputs();
    if (index >= inputs.size()) return ContextError::InputIndex;
    const auto evidence = inputs[index];
    if (evidence.kind == InputKind::V3 && evidence.group >= m_context->GroupProof().GroupDigests().size()) return ContextError::Invariant;
    return ContextError::None;
}
ContextError CheckInput(const RequestPtr& request, uint32_t index, const ContextPtr& context)
{
    if (!request || request->spent.size() != request->tx.vin.size()) return ContextError::InvalidRequest;
    if (index >= request->tx.vin.size()) return ContextError::InputIndex;
    if (context) {
        const auto bound = Bind(request, context);
        return bound.checks ? bound.checks->Check(index) : bound.error;
    }
    if (V3(request->spent[index].scriptPubKey)) return ContextError::MissingContext;
    if (WrappedV3(request->tx.vin[index], request->spent[index])) return ContextError::WrappedV3;
    if (!Flags(request->flags)) return ContextError::UnsupportedFlags;
    test::NativeSession native(request->tx, request->spent, request->genesis, request->flags);
    ScriptError error;
    size_t calls{0};
    return native.Check(index, error, calls) ? ContextError::None : ContextError::NativeFailure;
}
} // namespace mercaweave::research
