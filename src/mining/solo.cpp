// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.

#include <mining/solo.h>

#include <chainparams.h>
#include <consensus/merkle.h>
#include <crypto/common.h>
#include <interfaces/handler.h>
#include <pow.h>
#include <random.h>
#include <script/solver.h>

#include <limits>
#include <stdexcept>
#include <utility>

namespace mining {
SoloWorkProvider::SoloWorkProvider(std::unique_ptr<interfaces::Mining> mining, CScript payout,
                                 std::string destination, bool regtest,
                                 std::unique_ptr<interfaces::Handler> unload_handler,
                                 std::shared_ptr<std::atomic<bool>> cancelled)
    : m_mining{std::move(mining)}, m_payout{std::move(payout)},
      m_destination{std::move(destination)}, m_regtest{regtest}, m_session{ReadLE64(GetRandHash().data())},
      m_unload_handler{std::move(unload_handler)}, m_cancelled{std::move(cancelled)}
{
    std::vector<std::vector<unsigned char>> solutions;
    if (!m_mining) throw std::runtime_error{"Native mining interface is unavailable"};
    if (Solver(m_payout, solutions) != TxoutType::WITNESS_V2_MERCATURA_PQ) {
        throw std::runtime_error{"Mining requires a native Mercatura PQ payout script"};
    }
}
SoloWorkProvider::~SoloWorkProvider() = default;

std::optional<MiningJob> SoloWorkProvider::GetJob()
{
    m_template.reset();
    if (IsCancelled()) return {};
    if ((!m_regtest && m_mining->isInitialBlockDownload()) || !m_mining->getTip()) return {};
    node::BlockCreateOptions options;
    options.coinbase_output_script = m_payout;
    // The assembler validates its preliminary coinbase before we append our
    // extranonce. Height 1 needs its existing dummy byte to satisfy the
    // consensus minimum scriptSig length.
    options.include_dummy_extranonce = true;
    // Readiness above handles IBD. Avoid blocking on cooldown; in particular,
    // a peerless regtest must be able to mine its first block.
    m_template = m_mining->createNewBlock(options, /*cooldown=*/false);
    if (!m_template) return {};
    const auto fields{m_template->getCoinbaseTx()};
    CMutableTransaction coinbase;
    coinbase.version = fields.version;
    coinbase.nLockTime = fields.lock_time;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vin[0].nSequence = fields.sequence;
    coinbase.vin[0].scriptSig = fields.script_sig_prefix;
    if (m_extranonce == std::numeric_limits<uint64_t>::max()) throw std::runtime_error{"Mining extranonce exhausted; restart mining"};
    std::vector<unsigned char> extra(16);
    WriteLE64(extra.data(), m_session);
    WriteLE64(extra.data() + 8, ++m_extranonce);
    coinbase.vin[0].scriptSig << extra;
    if (coinbase.vin[0].scriptSig.size() > 100) throw std::runtime_error{"Mining coinbase script exceeds its consensus size limit"};
    if (fields.witness) {
        coinbase.vin[0].scriptWitness.stack.emplace_back(fields.witness->begin(), fields.witness->end());
    }
    // Reward comes from the authoritative assembler's actual SP-LT subsidy
    // and transaction fees. No software fee, donation or developer output.
    coinbase.vout.emplace_back(fields.block_reward_remaining, m_payout);
    coinbase.vout.insert(coinbase.vout.end(), fields.required_outputs.begin(), fields.required_outputs.end());
    CBlock block{m_template->getBlock()};
    block.vtx[0] = MakeTransactionRef(std::move(coinbase));
    block.hashMerkleRoot = BlockMerkleRoot(block);
    block.nNonce = 0;
    auto target{DeriveTarget(block.nBits, Params().GetConsensus().powLimit)};
    if (!target) throw std::runtime_error{"Core returned an invalid mining target"};
    return MiningJob{static_cast<const CBlockHeader&>(block), *target, block.vtx[0]};
}

bool SoloWorkProvider::IsCurrent(const MiningJob& job)
{
    if (IsCancelled() || (!m_regtest && m_mining->isInitialBlockDownload())) return false;
    const auto tip{m_mining->getTip()};
    return tip && tip->hash == job.header.hashPrevBlock;
}
SubmissionResult SoloWorkProvider::Submit(const MiningJob& job, const CBlockHeader& solution)
{
    if (!m_template || !IsCurrent(job)) return {false, false, "Discarded stale solo solution"};
    if (solution.hashPrevBlock != job.header.hashPrevBlock || solution.hashMerkleRoot != job.header.hashMerkleRoot ||
        solution.nBits != job.header.nBits || solution.nTime != job.header.nTime || solution.nVersion != job.header.nVersion) {
        throw std::runtime_error{"Mining solution does not match its job"};
    }
    const bool processed{m_template->submitSolution(solution.nVersion, solution.nTime, solution.nNonce, job.coinbase)};
    const auto tip{m_mining->getTip()};
    // submitSolution's bool is processing success, NOT active-chain acceptance.
    const bool accepted{tip && tip->hash == solution.GetHash()};
    return {processed, accepted, accepted ? "Solo block accepted in the active chain" :
            processed ? "Solo solution processed; active-chain acceptance not observed" : "Solo solution rejected"};
}
void SoloWorkProvider::Interrupt() { m_mining->interrupt(); }
} // namespace mining
