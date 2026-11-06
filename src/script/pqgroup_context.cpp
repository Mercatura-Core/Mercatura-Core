// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <script/pqgroup_context.h>
#include <consensus/mercaweave.h>

namespace mercaweave {
std::shared_ptr<const IsolatedExecution> IsolatedExecution::ForConsensus(
    const Consensus::Params& params, int height)
{
    if (!Consensus::MercaWeaveActive(params, height)) return {};
    B32 genesis;
    std::copy(params.hashGenesisBlock.begin(), params.hashGenesisBlock.end(), genesis.begin());
    return std::shared_ptr<const IsolatedExecution>(new IsolatedExecution(genesis));
}

std::shared_ptr<const GroupContext> GroupContext::PrepareActivated(
    const CTransaction& tx, std::vector<CTxOut> spent, script_verify_flags flags,
    const uint256& genesis, std::optional<Error>& error, Statistics* work)
{
    if (!(flags & SCRIPT_VERIFY_MERCAWEAVE)) {
        error = Error::Context;
        if (work) *work = {};
        return {};
    }
    B32 network;
    std::copy(genesis.begin(), genesis.end(), network.begin());
    const IsolatedExecution execution(network);
    return Prepare(execution, tx, std::move(spent), flags, genesis, error, work);
}

namespace {
std::vector<bool> Authorized(const Result& result, size_t count)
{
    std::vector<bool> out(count, false);
    for (const auto index : result.authorized_inputs) out.at(index) = true;
    return out;
}
}

GroupContext::GroupContext(const CTransaction& tx, std::vector<CTxOut> spent,
                           B32 genesis, script_verify_flags flags, const Result& result)
    : m_tx(MakeTransactionRef(tx)), m_spent(std::move(spent)),
      m_genesis(genesis), m_flags(flags), m_groups(result.authorization),
      m_statistics(result.stats), m_authorized(Authorized(result, tx.vin.size()))
{
    auto outputs = m_spent;
    m_native.Init(*m_tx, std::move(outputs), /*force=*/true);
}

std::shared_ptr<const GroupContext> GroupContext::Prepare(
    const IsolatedExecution& execution, const CTransaction& tx,
    std::vector<CTxOut> spent, script_verify_flags flags,
    const uint256& genesis, std::optional<Error>& error, Statistics* work)
{
    error.reset();
    if (work) *work = {};
    if (!(flags & SCRIPT_VERIFY_WITNESS) ||
        !std::equal(genesis.begin(), genesis.end(), execution.Genesis().begin())) {
        error = Error::Context;
        return {};
    }
    // This callback deliberately does not authorize any non-v3 input. It only
    // defers those inputs to the genuine CScriptCheck pipeline. A GroupContext
    // cannot be used as evidence that a complete transaction is valid.
    const CMutableTransaction mutable_tx(tx);
    const auto result = Verify(mutable_tx, spent, execution.Genesis(),
        [](const auto&, const auto&, const auto&, uint32_t) { return true; });
    if (work) *work = result.stats;
    if (!result.valid) {
        error = result.error;
        return {};
    }
    return std::shared_ptr<const GroupContext>(new GroupContext(
        tx, std::move(spent), execution.Genesis(), flags, result));
}

bool GroupContext::Matches(const CTransaction& tx, const std::vector<CTxOut>& spent,
                           script_verify_flags flags, const uint256& genesis) const
{
    return flags == m_flags && std::equal(genesis.begin(), genesis.end(), m_genesis.begin()) &&
        m_groups && m_groups->Matches(CMutableTransaction(tx), spent, m_genesis);
}

bool GroupContext::Authorize(unsigned int index, const CScriptWitness& witness,
                             const Bytes& program, script_verify_flags flags, bool is_p2sh) const
{
    if (is_p2sh || flags != m_flags || program.size() != 32 ||
        index >= m_authorized.size() || !m_authorized[index] ||
        witness.stack != m_tx->vin[index].scriptWitness.stack) return false;
    int version;
    Bytes expected;
    return m_spent[index].scriptPubKey.IsWitnessProgram(version, expected) &&
        version == 3 && expected == program;
}

bool GroupContext::MatchesJob(const CTransaction* tx, const PrecomputedTransactionData* native,
                              unsigned int index, const CTxOut& spent, script_verify_flags flags,
                              std::optional<uint256> genesis) const
{
    return tx == m_tx.get() && native == &m_native && index < m_spent.size() &&
        spent == m_spent[index] && flags == m_flags && genesis &&
        std::equal(genesis->begin(), genesis->end(), m_genesis.begin());
}
} // namespace mercaweave
