// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <test/mercaweave/node_validation.h>
#include <coins.h>
namespace mercaweave::node_research {
Context::Context(integration::ViewPtr view, uint64_t epoch, research::RequestPtr request, std::vector<integration::ScriptJob> jobs)
    : m_view(std::move(view)), m_epoch(epoch), m_request(std::move(request)), m_tx(m_request->tx), m_jobs(std::move(jobs))
{
    auto spent = m_request->spent;
    m_native.Init(m_tx, std::move(spent));
}
std::shared_ptr<Context> Context::Prepare(const integration::ViewPtr& view, const CMutableTransaction& tx, const B32& genesis, script_verify_flags flags, int height)
{
    auto result = integration::Engine::Prepare(view, tx, genesis, flags, height);
    if (!result.proof) return {};
    auto jobs = integration::Engine::Bind(view, tx, genesis, flags, height, result.proof);
    if (jobs.failure.stage != integration::Stage::None) return {};
    return std::shared_ptr<Context>(new Context(view, result.acquisition_epoch, result.acquired, std::move(jobs.jobs)));
}
bool Context::MatchesJob(const CTransaction* tx, const PrecomputedTransactionData* native, unsigned int index, const CTxOut& spent, script_verify_flags flags, std::optional<uint256> genesis) const
{
    return tx == &m_tx && native == &m_native && index < m_jobs.size() && m_epoch == m_view->Epoch()
        && flags == m_request->flags && genesis && std::equal(genesis->begin(), genesis->end(), m_request->genesis.begin())
        && spent == m_request->spent[index] && m_native.m_spent_outputs_ready
        && m_native.m_spent_outputs.size() == m_request->spent.size()
        && m_native.m_spent_outputs[index] == m_request->spent[index];
}
bool Context::Matches(const CTransaction& tx, const CCoinsViewCache& coins, const PrecomputedTransactionData& native, script_verify_flags flags, std::optional<uint256> genesis) const
{
    if (&tx != &m_tx || &native != &m_native || m_jobs.size() != tx.vin.size()) return false;
    for (unsigned int i = 0; i < tx.vin.size(); ++i) {
        const auto& coin = coins.AccessCoin(tx.vin[i].prevout);
        const auto owned = m_view->CoinAt(tx.vin[i].prevout);
        if (coin.IsSpent() || !owned || owned->out != coin.out || owned->nHeight != coin.nHeight || owned->fCoinBase != coin.fCoinBase
            || !MatchesJob(&tx, &native, i, coin.out, flags, genesis)) return false;
    }
    return m_epoch == m_view->Epoch();
}
bool Context::IsGrouped(unsigned int index) const
{
    int version; std::vector<unsigned char> program;
    return index < m_request->spent.size() && m_request->spent[index].scriptPubKey.IsWitnessProgram(version, program) && version == 3;
}
integration::ScriptResult Context::Run(unsigned int index) const
{
    if (index >= m_jobs.size()) return {};
    return m_jobs[index].Run();
}
}
