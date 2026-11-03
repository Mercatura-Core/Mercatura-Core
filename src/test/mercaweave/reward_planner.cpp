// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include "reward_planner.h"
#include <crypto/sha256.h>
namespace mercaweave::reward_research {
namespace {
B32 Hash(const Writer& w){B32 h;CSHA256{}.Write(w.bytes.data(),w.bytes.size()).Finalize(h.data());return h;}
Block ErrorBlock(const WalletError& e){switch(e.code){case Code::Fee:return Block::Fee;case Code::Funds:return Block::Funds;case Code::Policy:return std::string_view(e.what())=="final dust output"?Block::Dust:Block::Limits;case Code::Locked:return Block::Locked;case Code::Inputs:return Block::Stale;default:return Block::Metadata;}}
}
void Planner::Retire(Phase phase){if(m_pending){m_history[m_pending->id]=phase;m_pending.reset();if(m_history.size()>256)m_history.erase(m_history.begin());}}
void Planner::Cancel(){if(m_pending)m_suppressed=m_pending->fingerprint;m_suppression=Block::Cancelled;Retire(Phase::Cancelled);}
void Planner::Conflict(bool replaced){if(m_pending)m_suppressed=m_pending->fingerprint;m_suppression=Block::Conflict;Retire(replaced?Phase::Replaced:Phase::Conflicted);}
void Planner::Fail(){if(m_pending)m_suppressed=m_pending->fingerprint;m_suppression=Block::Failed;Retire(Phase::Failed);}
void Planner::Unload(){m_unloaded=true;Retire(Phase::Stale);}
Decision Planner::Evaluate(const Wallet& w,const integration::ViewPtr& view,std::span<const FundingCoin> candidates,const Policy& policy)
{
    Decision result;auto blocked=[&](Block b){Retire(Phase::Stale);result.block=b;return result;};
    if(!policy.enabled||!policy.opted_in)return blocked(Block::Disabled);
    if(!policy.isolated||!policy.network)return blocked(Block::Network);
    if(m_unloaded||!policy.loaded||!view)return blocked(Block::Unloaded);
    if(w.IsWatchOnly())return blocked(Block::Signer);
    if(w.IsLocked())return blocked(Block::Locked);
    if(!policy.signer)return blocked(Block::Signer);
    if(policy.reorganizing)return blocked(Block::Reorg);
    if(!policy.synchronized)return blocked(Block::Sync);
    if(policy.spend_height<=0||policy.threshold==0||policy.threshold>MAX_INPUTS||candidates.size()>MAX_INPUTS)return blocked(Block::Limits);
    int version;Bytes program;if(!policy.destination.IsWitnessProgram(version,program)||version!=2||program.size()!=32)return blocked(Block::Destination);
    if(GetDustThreshold(CTxOut{2,policy.destination},CFeeRate{DUST_RELAY_TX_FEE})!=2)return blocked(Block::Dust);
    const auto epoch=view->Epoch();std::map<PublicKey,std::vector<FundingCoin>> buckets;std::set<COutPoint> seen;
    Writer fingerprint;fingerprint<<epoch<<policy.revision<<policy.threshold<<policy.spend_height<<policy.destination<<policy.fee.per_k<<policy.fee.minimum<<policy.fee.maximum;
    std::vector<FundingCoin> sorted(candidates.begin(),candidates.end());std::sort(sorted.begin(),sorted.end(),[](const auto& a,const auto& b){return a.point<b.point;});
    bool reserved=false;
    for(auto c:sorted){if(!seen.insert(c.point).second)return blocked(Block::Metadata);auto coin=view->CoinAt(c.point);if(!coin||!coin->IsCoinBase())continue;
        auto owner=w.Find(coin->out.scriptPubKey);if(!owner||owner->location.witness!=3||owner->location.purpose!=2)continue;
        if(policy.selected&&owner->pk!=*policy.selected)continue;
        fingerprint<<c.point<<coin->out<<coin->nHeight<<c.depth<<c.safe<<uint64_t(c.ancestors)<<uint64_t(c.cluster_count);
        if(static_cast<uint64_t>(coin->nHeight)+COINBASE_MATURITY>static_cast<uint64_t>(policy.spend_height)){++result.immature;continue;}
        ++result.mature;
        if(!c.safe||c.depth<1||c.ancestors||c.cluster_count>25)continue;
        if(policy.external_reservations.contains(c.point)){reserved=true;continue;}
        buckets[owner->pk].push_back(c);++result.eligible;
    }
    auto chosen=std::find_if(buckets.begin(),buckets.end(),[&](const auto& item){return item.second.size()>=policy.threshold;});
    if(chosen==buckets.end())return blocked(reserved?Block::Reserved:Block::Threshold);
    fingerprint<<chosen->first;for(const auto& c:chosen->second)fingerprint<<c.point;
    const auto identity=Hash(fingerprint);
    if(m_suppressed&&*m_suppressed==identity)return blocked(m_suppression);
    m_suppressed.reset();
    if(m_pending&&m_pending->fingerprint==identity){result.block=m_pending->phase==Phase::Partial?Block::Partial:Block::None;result.proposal=m_pending;result.reused=true;return result;}
    Retire(Phase::Stale);
    try{
        auto txs=Funding::Sweep(w,view,chosen->second,policy.destination,FundingMode::MercaWeave,policy.fee,policy.spend_height);
        Require(m_next<UINT64_MAX,Code::Policy,"proposal sequence exhausted");Proposal proposal;proposal.id=m_next++;proposal.epoch=epoch;proposal.revision=policy.revision;proposal.key=chosen->first;proposal.destination=policy.destination;proposal.fingerprint=identity;proposal.transactions=std::move(txs);
        for(const auto& tx:proposal.transactions)for(const auto& in:tx.estimate.vin)Require(proposal.inputs.insert(in.prevout).second,Code::Inputs,"duplicate planner selection");
        if(view->Epoch()!=epoch||w.IsLocked())return blocked(Block::Stale);
        m_pending=proposal;result.proposal=std::move(proposal);result.block=Block::None;return result;
    }catch(const WalletError& e){return blocked(ErrorBlock(e));}catch(const Failure&){return blocked(Block::Metadata);}
}
Block Planner::Attach(const integration::ViewPtr& view,const B32& g,std::span<const PartiallySignedTransaction> psbts,int height)
{
    if(!m_pending||!view||m_pending->epoch!=view->Epoch())return Block::Stale;
    if(psbts.size()!=m_pending->transactions.size())return Block::Partial;
    bool complete=true;
    try{for(size_t i=0;i<psbts.size();++i){if(!psbts[i].tx||Wire(*psbts[i].tx,false)!=Wire(*m_pending->transactions[i].psbt.tx,false))return Block::Stale;
        auto info=GroupPSBT::Inspect(psbts[i],view,g,height);bool all=std::all_of(info.complete.begin(),info.complete.end(),[](bool b){return b;});complete&=all;
        if(all)GroupPSBT::Extract(psbts[i],view,g,height);}
        if(m_pending->epoch!=view->Epoch())return Block::Stale;
    }catch(const std::exception&){Retire(Phase::Failed);return Block::Failed;}
    m_pending->phase=complete?Phase::Verified:Phase::Partial;return complete?Block::None:Block::Partial;
}
}
