// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef MERCATURA_MERCAWEAVE_QT_WALLET_BRIDGE_H
#define MERCATURA_MERCAWEAVE_QT_WALLET_BRIDGE_H
#ifndef MERCATURA_MERCAWEAVE_QT_RESEARCH_TEST_ONLY
#error "Mock wallet capability injection is forbidden in production"
#endif
#include "reward_planner.h"
#include <qt/mercaweavemodel.h>
#include <atomic>
#include <mutex>
namespace mercaweave::qt_research {
class Bridge final:public MercaWeaveQt::Backend {
    std::shared_ptr<wallet_research::Wallet> m_wallet;
    integration::ViewPtr m_view;
    std::vector<wallet_research::FundingCoin> m_candidates;
    std::vector<wallet_research::Address> m_inventory;
    reward_research::Planner m_planner;
    reward_research::Policy m_policy;
    std::optional<wallet_research::Address> m_destination;
    std::vector<PartiallySignedTransaction> m_previews;
    std::vector<CTxOut> m_payments;std::optional<wallet_research::Address> m_change;
    std::map<QString,PublicKey> m_keys;
    mutable std::mutex m_mutex;
    std::atomic<uint64_t> m_revision{1};
    std::atomic<MercaWeaveQt::Reason> m_invalid{MercaWeaveQt::Reason::None};
    bool m_opted{false};QString m_synthetic;
    MercaWeaveQt::Snapshot Snapshot();
    void FillBatch(MercaWeaveQt::Snapshot&,std::span<const PartiallySignedTransaction>);
    void FillPreview(MercaWeaveQt::Snapshot&,const PartiallySignedTransaction&);
public:
    Bridge(std::shared_ptr<wallet_research::Wallet>,integration::ViewPtr,std::vector<wallet_research::FundingCoin>,reward_research::Policy={},std::vector<wallet_research::Address> inventory={});
    MercaWeaveQt::Snapshot PublicSnapshot(); // Setup/test observation only.
    MercaWeaveQt::Snapshot Perform(const MercaWeaveQt::Request&)override;
    void Invalidate(MercaWeaveQt::Reason)override;
    void SetPreview(PartiallySignedTransaction);
    void SetPreviewBatch(std::vector<PartiallySignedTransaction>);
    void SetFunding(std::vector<CTxOut>,wallet_research::Address change);
};
}
#endif
