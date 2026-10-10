// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.

#ifndef BITCOIN_MINING_SOLO_H
#define BITCOIN_MINING_SOLO_H

#include <interfaces/mining.h>
#include <mining/cpu_miner.h>
#include <script/script.h>

namespace interfaces { class Handler; }
namespace mining {

class SoloWorkProvider final : public WorkProvider {
public:
    SoloWorkProvider(std::unique_ptr<interfaces::Mining> mining, CScript payout,
                     std::string destination, bool regtest,
                     std::unique_ptr<interfaces::Handler> unload_handler = {},
                     std::shared_ptr<std::atomic<bool>> cancelled = {});
    ~SoloWorkProvider() override;
    std::optional<MiningJob> GetJob() override;
    bool IsCurrent(const MiningJob& job) override;
    SubmissionResult Submit(const MiningJob& job, const CBlockHeader& solution) override;
    void Interrupt() override;
    bool IsCancelled() const override { return m_cancelled && m_cancelled->load(); }
    std::string Destination() const override { return m_destination; }

private:
    const std::unique_ptr<interfaces::Mining> m_mining;
    const CScript m_payout;
    const std::string m_destination;
    const bool m_regtest;
    const uint64_t m_session;
    uint64_t m_extranonce{0};
    std::unique_ptr<interfaces::BlockTemplate> m_template;
    std::unique_ptr<interfaces::Handler> m_unload_handler;
    const std::shared_ptr<std::atomic<bool>> m_cancelled;
};
} // namespace mining
#endif // BITCOIN_MINING_SOLO_H
