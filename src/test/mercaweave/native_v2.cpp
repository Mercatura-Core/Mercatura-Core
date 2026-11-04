// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <test/mercaweave/native_v2.h>

namespace mercaweave::test {
namespace {
class CountingChecker final : public MutableTransactionSignatureChecker {
    size_t& m_calls;
public:
    CountingChecker(const CMutableTransaction& tx, uint32_t i, CAmount amount,
                    const PrecomputedTransactionData& data, std::optional<uint256> genesis, size_t& calls)
        : MutableTransactionSignatureChecker(&tx, i, amount, data, MissingDataBehavior::FAIL, genesis), m_calls(calls) {}
    bool CheckMercaturaPQSignature(std::span<const unsigned char> sig,
                                  std::span<const unsigned char> pk, ScriptError* error) const override
    {
        ++m_calls;
        return MutableTransactionSignatureChecker::CheckMercaturaPQSignature(sig, pk, error);
    }
};
}
NativeSession::NativeSession(const CMutableTransaction& tx, const std::vector<CTxOut>& spent,
                             const B32& genesis, script_verify_flags flags)
    : m_tx(tx), m_spent(spent), m_genesis(genesis), m_flags(flags)
{
    if (m_spent.size() != m_tx.vin.size()) throw Failure(Error::Context);
    m_data.Init(m_tx, std::vector<CTxOut>(m_spent), true);
}
H48 NativeSession::Digest(uint32_t index) const
{
    H48 h;
    if (!ComputeMercaturaPQAuthDigestV1(h, m_tx, index, uint256{std::span<const unsigned char>{m_genesis}}, m_data)) throw Failure(Error::Context);
    return h;
}
bool NativeSession::Check(uint32_t index, ScriptError& error, size_t& pq_calls,
                          bool supply_genesis, bool supply_precomputed) const
{
    if (index >= m_tx.vin.size()) { error = SCRIPT_ERR_UNKNOWN_ERROR; return false; }
    const std::optional<uint256> genesis = supply_genesis ? std::optional{uint256{std::span<const unsigned char>{m_genesis}}} : std::nullopt;
    const auto& in = m_tx.vin[index];
    if (!supply_precomputed) {
        MutableTransactionSignatureChecker checker{&m_tx, index, m_spent[index].nValue, MissingDataBehavior::FAIL, genesis};
        return VerifyScript(in.scriptSig, m_spent[index].scriptPubKey, &in.scriptWitness, m_flags, checker, &error);
    }
    CountingChecker checker{m_tx, index, m_spent[index].nValue, m_data, genesis, pq_calls};
    return VerifyScript(in.scriptSig, m_spent[index].scriptPubKey, &in.scriptWitness, m_flags, checker, &error);
}
NativeResult NativeSession::VerifyMixed() const
{
    NativeResult r{{}, 1, 0, 0, {}, m_flags};
    r.grouped = Verify(m_tx, m_spent, m_genesis, [&](const auto& tx, const auto& spent, const auto& genesis, uint32_t index) {
        // Only this frozen session's objects can consume its precomputation.
        if (&tx != &m_tx || &spent != &m_spent || &genesis != &m_genesis) return false;
        ScriptError error{SCRIPT_ERR_UNKNOWN_ERROR};
        ++r.script_checks;
        const auto ok = Check(index, error, r.pq_checker_calls);
        r.errors.push_back(error);
        return ok;
    });
    return r;
}
} // namespace mercaweave::test
