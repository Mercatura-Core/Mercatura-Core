// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef MERCATURA_MERCAWEAVE_REWARD_PLANNER_H
#define MERCATURA_MERCAWEAVE_REWARD_PLANNER_H
#ifndef MERCATURA_MERCAWEAVE_QT_RESEARCH_TEST_ONLY
#error "Reward planner accepts synthetic research state only"
#endif
#include "wallet_funding.h"
#include <set>
namespace mercaweave::reward_research {
using namespace wallet_research;
enum class Block {None,Disabled,Network,Unloaded,Locked,Signer,Sync,Reorg,Threshold,Metadata,Destination,Reserved,Fee,Dust,Funds,Limits,Stale,Failed,Cancelled,Conflict,Partial};
enum class Phase {Ready,Partial,Verified,Failed,Cancelled,Conflicted,Replaced,Stale};
struct Policy {
    bool enabled{false},opted_in{false},isolated{false},network{false},loaded{true},synchronized{false},signer{false},reorganizing{false};
    uint32_t threshold{50};int spend_height{200};uint64_t revision{0};
    FeePolicy fee{1,1,1000};CScript destination;
    std::optional<PublicKey> selected;
    std::set<COutPoint> external_reservations;
};
struct Proposal {
    uint64_t id{0},epoch{0},revision{0};int spend_height{200};Phase phase{Phase::Ready};
    B32 fingerprint{};PublicKey key{};CScript destination;
    std::vector<Funded> transactions;
    std::set<COutPoint> inputs;
};
struct Decision {Block block{Block::Disabled};size_t mature{0},immature{0},eligible{0};std::optional<Proposal> proposal;bool reused{false};};
// No timer, seed, signing callback, unlock or broadcast method. Caller owns
// lifecycle and supplies an actual research wallet plus owned synthetic view.
class Planner final {
    uint64_t m_next{1};std::optional<Proposal> m_pending;
    bool m_unloaded{false};std::optional<B32> m_suppressed;Block m_suppression{Block::Cancelled};
    std::map<uint64_t,Phase> m_history;
    void Retire(Phase);
public:
    Decision Evaluate(const Wallet&,const integration::ViewPtr&,std::span<const FundingCoin>,const Policy&);
    void Cancel();void Conflict(bool replaced=false);void Fail();void Unload();
    Block Attach(const integration::ViewPtr&,const B32&,std::span<const PartiallySignedTransaction>,int spend_height=200);
    const std::optional<Proposal>& Pending()const{return m_pending;}
    size_t HistorySize()const{return m_history.size();}
};
}
#endif
