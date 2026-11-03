// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include "qt_wallet_bridge.h"
#include <bech32.h>
#include <util/strencodings.h>
namespace mercaweave::qt_research {
using namespace MercaWeaveQt;using namespace wallet_research;
namespace {
Reason Map(reward_research::Block b){using B=reward_research::Block;switch(b){
case B::None:return Reason::None;case B::Disabled:return Reason::OptIn;case B::Network:return Reason::Network;case B::Unloaded:return Reason::Unloaded;
case B::Locked:return Reason::Locked;case B::Signer:return Reason::Signer;case B::Sync:return Reason::Sync;case B::Reorg:case B::Stale:return Reason::Stale;
case B::Threshold:return Reason::Threshold;case B::Metadata:return Reason::Metadata;case B::Destination:return Reason::Destination;case B::Reserved:return Reason::Reserved;
case B::Fee:return Reason::Fee;case B::Dust:return Reason::Dust;case B::Funds:return Reason::Funds;case B::Limits:return Reason::Limits;case B::Failed:return Reason::Failed;
case B::Cancelled:return Reason::Cancelled;case B::Conflict:return Reason::Conflict;case B::Partial:return Reason::Partial;}return Reason::Unavailable;}
CScript NativeDestination(const QString& text)
{
    auto decoded=bech32::Decode(text.toStdString());Bytes program;
    Require(decoded.encoding==bech32::Encoding::BECH32M&&decoded.hrp=="tmca"&&decoded.data.size()>1&&decoded.data[0]==2,Code::PSBT,"destination encoding/network/type");
    Require(ConvertBits<5,8,false>([&](unsigned char b){program.push_back(b);},decoded.data.begin()+1,decoded.data.end())&&program.size()==32,Code::PSBT,"destination program");
    return CScript{}<<OP_2<<program;
}
}
Bridge::Bridge(std::shared_ptr<Wallet> wallet,integration::ViewPtr view,std::vector<FundingCoin> coins,reward_research::Policy policy,std::vector<Address> inventory)
:m_wallet(std::move(wallet)),m_view(std::move(view)),m_candidates(std::move(coins)),m_inventory(std::move(inventory)),m_policy(std::move(policy)){}
void Bridge::Invalidate(Reason reason){m_invalid=reason;++m_revision;}
void Bridge::SetFunding(std::vector<CTxOut> payments,Address change){std::lock_guard lock(m_mutex);m_payments=std::move(payments);m_change=std::move(change);++m_revision;}
void Bridge::SetPreviewBatch(std::vector<PartiallySignedTransaction> psbts){std::lock_guard lock(m_mutex);m_previews=std::move(psbts);++m_revision;}
void Bridge::SetPreview(PartiallySignedTransaction psbt){std::lock_guard lock(m_mutex);m_previews={std::move(psbt)};++m_revision;}
Snapshot Bridge::Snapshot()
{
    MercaWeaveQt::Snapshot s;s.revision=m_revision.load();auto& c=s.capability;
    c.wallet=bool(m_wallet)&&bool(m_view);c.network=m_policy.network;c.isolated=m_policy.isolated;c.loaded=m_policy.loaded&&m_invalid!=Reason::Unloaded;
    c.optedIn=m_opted;c.synchronized=m_policy.synchronized&&!m_policy.reorganizing;c.signer=m_policy.signer;
    c.locked=!m_wallet||m_wallet->IsLocked();c.watchOnly=m_wallet&&m_wallet->IsWatchOnly();c.inventory=!m_inventory.empty();s.inventory=static_cast<int>(m_inventory.size());
    s.syntheticAddress=m_synthetic;s.reason=Availability(c,Action::Inspect);
    if(!m_wallet)return s;
    try{auto records=m_wallet->Records();c.metadata=true;s.metadataBackup=!c.watchOnly;
        std::map<std::tuple<uint32_t,uint8_t,uint32_t>,GroupRow> rows;m_keys.clear();
        for(const auto& a:records)if(a.location.witness==3){auto key=std::tuple{a.location.account,a.location.purpose,a.location.group};auto& row=rows[key];if(row.issued==0)row.first=a.location.address;
            row.account=a.location.account;row.purpose=a.location.purpose;row.group=a.location.group;row.last=std::max(row.last,a.location.address);++row.issued;
            row.identity=QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(a.pk.data()),a.pk.size()).toHex());m_keys[row.identity]=a.pk;
            row.lifecycle=row.issued>=64?QStringLiteral("Rotation due"):QStringLiteral("Active");}
        for(const auto& [key,row]:rows)s.groups.push_back(row);
        for(const auto& input:m_candidates){auto coin=m_view->CoinAt(input.point);if(!coin||!coin->IsCoinBase())continue;auto owner=m_wallet->Find(coin->out.scriptPubKey);if(!owner||owner->location.witness!=3||owner->location.purpose!=2)continue;
            if(static_cast<uint64_t>(coin->nHeight)+COINBASE_MATURITY<=static_cast<uint64_t>(m_policy.spend_height)){++s.mature;if(input.safe&&input.depth>0&&!input.ancestors&&input.cluster_count<=25)++s.eligible;}else ++s.immature;}
        s.reason=Availability(c,Action::Inspect);s.recoveryStatus=QStringLiteral("Issued ranges only. Live and resumable discovery are unavailable.");
        if(!m_previews.empty())FillBatch(s,m_previews);
        if(m_planner.Pending()){s.proposal=QString::number(m_planner.Pending()->id);std::vector<PartiallySignedTransaction> psbts;for(const auto& tx:m_planner.Pending()->transactions)psbts.push_back(tx.psbt);if(!psbts.empty())FillBatch(s,psbts);}
    }catch(const WalletError& e){if(e.code==Code::Fee){s.reason=Reason::Fee;s.preview.complete=false;}else{c.metadata=false;s.reason=Reason::Metadata;s.preview={};}}catch(const std::exception&){c.metadata=false;s.reason=Reason::Metadata;s.preview={};}
    return s;
}
void Bridge::FillBatch(MercaWeaveQt::Snapshot& s,std::span<const PartiallySignedTransaction> psbts)
{
    Preview sum;sum.available=!psbts.empty();sum.complete=!psbts.empty();sum.transactions=psbts.size();sum.perK=m_policy.fee.per_k;sum.changeType=QStringLiteral("No change");
    for(const auto& p:psbts){MercaWeaveQt::Snapshot part;FillPreview(part,p);const auto& v=part.preview;
        sum.inputs+=v.inputs;sum.outputs+=v.outputs;sum.nativeInputs+=v.nativeInputs;sum.groups+=v.groups;sum.anchors+=v.anchors;sum.followers+=v.followers;sum.signatures+=v.signatures;
        sum.bytes+=v.bytes;sum.weight+=v.weight;sum.maxWeight=std::max(sum.maxWeight,v.weight);sum.fee+=v.fee;sum.complete&=v.complete;
        if(v.changeType!=QStringLiteral("No change"))sum.changeType=v.changeType;if(part.reason==Reason::Fee)s.reason=Reason::Fee;
    }
    if(sum.fee>m_policy.fee.maximum)s.reason=Reason::Fee;s.preview=std::move(sum);
}
void Bridge::FillPreview(MercaWeaveQt::Snapshot& s,const PartiallySignedTransaction& p)
{
    auto info=GroupPSBT::Inspect(p,m_view,m_wallet->Genesis(),m_policy.spend_height);auto& preview=s.preview;preview={};
    preview.available=true;preview.inputs=info.tx.vin.size();preview.outputs=info.tx.vout.size();preview.groups=info.mapping.groups.size();preview.anchors=preview.groups;size_t members=0;
    for(const auto& group:info.mapping.groups)members+=group.members.size();preview.followers=members-preview.groups;preview.nativeInputs=preview.inputs-members;preview.signatures=preview.groups+preview.nativeInputs;
    auto sizes=Measure(info.tx);preview.bytes=sizes.total;preview.weight=sizes.weight;preview.perK=m_policy.fee.per_k;
    for(const auto& spent:info.spent)preview.fee+=spent.nValue;for(const auto& out:info.tx.vout)preview.fee-=out.nValue;
    if(preview.fee<Funding::Fee(sizes.total,m_policy.fee)||preview.fee>m_policy.fee.maximum)s.reason=Reason::Fee;
    preview.complete=std::all_of(info.complete.begin(),info.complete.end(),[](bool b){return b;});preview.changeType=QStringLiteral("No change");
    for(const auto& out:info.tx.vout)if(auto a=m_wallet->Find(out.scriptPubKey);a&&a->location.purpose==1)preview.changeType=a->location.witness==2?QStringLiteral("Independent-key witness-v2 PQ"):QStringLiteral("Grouped witness-v3 PQ");
}
MercaWeaveQt::Snapshot Bridge::PublicSnapshot(){std::lock_guard lock(m_mutex);return Snapshot();}
MercaWeaveQt::Snapshot Bridge::Perform(const Request& request)
{
    std::lock_guard lock(m_mutex);const auto cancellation=m_invalid.load();
    if(cancellation!=Reason::None){if(cancellation==Reason::Unloaded){m_planner.Unload();m_policy.loaded=false;}else m_planner.Cancel();m_destination.reset();m_synthetic.clear();m_invalid=Reason::None;}
    m_opted=request.optedIn;auto state=Snapshot();
    if(request.revision!=m_revision){state.reason=Reason::Stale;return state;}
    state.reason=Availability(state.capability,request.action);if(state.reason!=Reason::None)return state;
    const auto generation=m_revision.load();
    try{
        switch(request.action){
        case Action::Inspect:break;
        case Action::IssueReceive:{Require(request.purpose==0,Code::Metadata,"receive purpose only");Address a;
            if(m_wallet->IsLocked()||m_wallet->IsWatchOnly()){auto it=std::find_if(m_inventory.begin(),m_inventory.end(),[&](const auto& item){return item.location.account==request.account&&item.location.purpose==0&&m_wallet->Find(item.script)==item;});
                if(it==m_inventory.end()){state.reason=Reason::Inventory;return state;}a=*it;m_inventory.erase(it);}
            else a=m_wallet->Issue(request.account,0,3);
            m_synthetic=QStringLiteral("SYNTHETIC ONLY — account %1 / receive group %2 / index %3. Not a payment destination.").arg(a.location.account).arg(a.location.group).arg(a.location.address);break;}
        case Action::RotateReceive:m_wallet->Rotate(request.account,0);m_synthetic.clear();break;
        case Action::SelectMining:{auto key=m_keys.find(request.group);if(key==m_keys.end()){state.reason=Reason::Metadata;return state;}bool mining=false;for(const auto& a:m_wallet->Records())mining|=a.pk==key->second&&a.location.purpose==2;
            if(!mining){state.reason=Reason::Metadata;return state;}m_policy.selected=key->second;m_planner.Cancel();m_synthetic=QStringLiteral("SYNTHETIC mining reward group selected. Native miner payout is unchanged.");break;}
        case Action::Cancel:m_planner.Cancel();m_destination.reset();m_synthetic.clear();state=Snapshot();state.reason=Reason::Cancelled;return state;
        case Action::Preview:
            if(request.mode<0||request.mode>2){state.reason=Reason::Unsupported;return state;}
            if(!m_payments.empty()&&m_change){auto funded=Funding::Fund(*m_wallet,m_view,m_candidates,m_payments,*m_change,static_cast<FundingMode>(request.mode),m_policy.fee,m_policy.spend_height);m_previews={std::move(funded.psbt)};}
            if(m_previews.empty()){state.reason=Reason::Unsupported;return state;}FillBatch(state,m_previews);if(state.reason==Reason::Fee)return state;if((request.mode==1&&state.preview.groups)||(request.mode==2&&state.preview.nativeInputs)){state.reason=Reason::Funds;state.preview.complete=false;return state;}state.reason=state.preview.complete?Reason::None:Reason::Partial;return state;
        case Action::Plan:{m_policy.enabled=request.sweepEnabled;m_policy.opted_in=request.optedIn;m_policy.threshold=request.threshold;m_policy.fee.per_k=request.perK;m_policy.fee.maximum=request.maximumFee;
            if(!request.group.isEmpty()){auto key=m_keys.find(request.group);if(key==m_keys.end()){state.reason=Reason::Metadata;return state;}m_policy.selected=key->second;}
            if(!m_policy.enabled){m_planner.Cancel();state.reason=Reason::OptIn;return state;}
            if(request.destination.isEmpty()){
                std::map<PublicKey,size_t> eligible;
                for(const auto& input:m_candidates){auto coin=m_view->CoinAt(input.point);if(!coin||!coin->IsCoinBase()||!input.safe||input.depth<1||input.ancestors||input.cluster_count>25||m_policy.external_reservations.contains(input.point))continue;auto a=m_wallet->Find(coin->out.scriptPubKey);
                    if(a&&a->location.purpose==2&&a->location.witness==3&&(!m_policy.selected||a->pk==*m_policy.selected)&&uint64_t(coin->nHeight)+COINBASE_MATURITY<=uint64_t(m_policy.spend_height))++eligible[a->pk];}
                bool threshold=false;for(const auto& [key,n]:eligible)threshold|=n>=m_policy.threshold;if(!threshold){state.reason=Reason::Threshold;return state;}
                if(!m_destination)m_destination=m_wallet->Issue(request.account,0,2);m_policy.destination=m_destination->script;
            }else{try{m_policy.destination=NativeDestination(request.destination);}catch(const std::exception&){state.reason=Reason::Destination;return state;}}
            auto decision=m_planner.Evaluate(*m_wallet,m_view,m_candidates,m_policy);state=Snapshot();state.reason=Map(decision.block);state.mature=decision.mature;state.immature=decision.immature;state.eligible=decision.eligible;break;}
        }
    }catch(const WalletError& e){state=Snapshot();state.preview={};switch(e.code){case Code::Locked:state.reason=Reason::Locked;break;case Code::WatchOnly:state.reason=Reason::WatchOnly;break;case Code::Fee:state.reason=Reason::Fee;break;case Code::Funds:case Code::Mode:state.reason=Reason::Funds;break;case Code::Inputs:state.reason=Reason::Stale;break;case Code::Policy:state.reason=Reason::Limits;break;case Code::PSBT:case Code::Signature:state.reason=Reason::Failed;break;default:state.reason=Reason::Metadata;break;}return state;}
    if(generation!=m_revision){m_planner.Cancel();state=Snapshot();state.reason=Reason::Stale;state.preview={};state.syntheticAddress.clear();return state;}
    ++m_revision;auto result=Snapshot();if(request.action==Action::Plan)result.reason=state.reason;return result;
}
}
