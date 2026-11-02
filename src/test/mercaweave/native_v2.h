// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef MERCATURA_MERCAWEAVE_NATIVE_V2_H
#define MERCATURA_MERCAWEAVE_NATIVE_V2_H
#include "mercaweave.h"
#include <policy/policy.h>
#include <script/interpreter.h>

namespace mercaweave::test {
struct NativeResult {
    Result grouped;
    size_t precomputations{0}, script_checks{0}, pq_checker_calls{0};
    std::vector<ScriptError> errors;
    script_verify_flags flags;
};
// Immutable transaction snapshot and one production precomputation per session.
// The caller must authenticate spent outputs before entering this test adapter.
class NativeSession {
    const CMutableTransaction m_tx;
    const std::vector<CTxOut> m_spent;
    const B32 m_genesis;
    const script_verify_flags m_flags;
    PrecomputedTransactionData m_data;
public:
    NativeSession(const CMutableTransaction& tx, const std::vector<CTxOut>& spent,
                  const B32& genesis, script_verify_flags flags = MANDATORY_SCRIPT_VERIFY_FLAGS);
    H48 Digest(uint32_t index) const;
    bool Check(uint32_t index, ScriptError& error, size_t& pq_calls,
               bool supply_genesis = true, bool supply_precomputed = true) const;
    NativeResult VerifyMixed() const;
};
} // namespace mercaweave::test
#endif
