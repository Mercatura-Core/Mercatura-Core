// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include "wallet_funding.h"
#include <wallet/coinselection.h>
#include <random.h>
#include <set>
namespace mercaweave::wallet_research {
namespace {
struct Resolved {FundingCoin candidate;CTxOut out;Address owner;};
CAmount AddAmount(CAmount a,CAmount b){Require(MoneyRange(a)&&MoneyRange(b)&&b<=MAX_MONEY-a,Code::Funds,"money range or overflow");return a+b;}
std::vector<Resolved> Resolve(const Wallet& w,const integration::ViewPtr& v,std::span<const FundingCoin> coins,FundingMode mode,bool skip,int height)
{
    Require(height>0,Code::Inputs,"invalid spend height");Require(v&&coins.size()<=MAX_INPUTS,Code::Inputs,"planning input bound");std::vector<Resolved> result;std::set<COutPoint> seen;CAmount total=0;
    for(const auto& c:coins){Require(seen.insert(c.point).second,Code::Inputs,"duplicate funding outpoint");
        auto coin=v->CoinAt(c.point);auto a=coin?w.Find(coin->out.scriptPubKey):std::nullopt;
        bool eligible=coin&&a&&coin->nHeight<static_cast<uint32_t>(height)&&c.depth>=1&&c.safe&&c.ancestors==0&&c.cluster_count<=25&&(!coin->IsCoinBase()||coin->nHeight+COINBASE_MATURITY<=static_cast<uint32_t>(height));
        if(!eligible&&skip)continue;Require(eligible,Code::Inputs,"missing, immature, unsafe or foreign funding input");
        bool allowed=mode==FundingMode::Automatic||(mode==FundingMode::StandardPQ&&a->location.witness==2)||(mode==FundingMode::MercaWeave&&a->location.witness==3);
        if(!allowed&&skip)continue;Require(allowed,Code::Mode,"input excluded by strict funding mode");
        total=AddAmount(total,coin->out.nValue);auto bounded=c;bounded.depth=std::min(c.depth,height-static_cast<int>(coin->nHeight));result.push_back({bounded,coin->out,*a});
    }return result;
}
Funded Assemble(const Wallet& w,const integration::ViewPtr& view,std::vector<Resolved> selected,std::vector<CTxOut> recipients,const Address* change,FeePolicy policy,bool sweep,int height)
{
    Require(!selected.empty()&&selected.size()<=MAX_INPUTS&&!recipients.empty(),Code::Funds,"empty funding selection or recipients");
    std::sort(selected.begin(),selected.end(),[](const auto& a,const auto& b){return a.candidate.point<b.candidate.point;});
    CMutableTransaction tx;std::vector<CTxOut> spent;std::vector<Address> addresses;CAmount input=0,output=0;size_t native_count=0;
    for(const auto& r:selected){tx.vin.emplace_back(r.candidate.point);spent.push_back(r.out);addresses.push_back(r.owner);input=AddAmount(input,r.out.nValue);native_count+=r.owner.location.witness==2;}
    for(const auto& o:recipients){Require(!IsDust(o,CFeeRate{DUST_RELAY_TX_FEE}),Code::Policy,"recipient below native dust floor");output=AddAmount(output,o.nValue);}Require(input>=output,Code::Funds,"insufficient input value");tx.vout=recipients;
    if(change){Require(w.Find(change->script)==*change&&change->location.purpose==1,Code::Ownership,"change is not an issued change address");Require(!native_count||change->location.witness==2,Code::Mode,"native inputs require native change");tx.vout.emplace_back(2,change->script);}
    // Materialize fixed-length public witnesses without requiring signatures.
    std::map<PublicKey,uint32_t> anchors;std::map<PublicKey,size_t> member_counts;
    for(uint32_t i=0;i<addresses.size();++i){const auto& a=addresses[i];if(a.location.witness==2)tx.vin[i].scriptWitness.stack={Bytes(3309),Bytes(a.pk.begin(),a.pk.end())};else{Require(++member_counts[a.pk]<=MAX_MEMBERS,Code::Policy,"group member bound");auto [it,first]=anchors.emplace(a.pk,i);Require(anchors.size()<=MAX_GROUPS,Code::Policy,"group count bound");
        if(first)tx.vin[i].scriptWitness.stack={Bytes(3309),Bytes(a.pk.begin(),a.pk.end()),Bytes(a.nonce.begin(),a.nonce.end())};
        else{Writer ref;ref<<it->second;tx.vin[i].scriptWitness.stack={Bytes(a.nonce.begin(),a.nonce.end()),ref.bytes};}}}
    CAmount fee=Funding::Fee(Measure(tx).total,policy);
    if(sweep){Require(input>fee,Code::Funds,"sweep fee exceeds value");tx.vout[0].nValue=input-fee;}
    else if(change){Require(input>=output&&input-output>=fee,Code::Funds,"funding short after exact fee");auto remaining=input-output-fee;
        if(remaining>=GetDustThreshold(tx.vout.back(),CFeeRate{DUST_RELAY_TX_FEE}))tx.vout.back().nValue=remaining;
        else{tx.vout.pop_back();auto minimum=Funding::Fee(Measure(tx).total,policy);Require(input-output>=minimum,Code::Funds,"funding short without change");fee=input-output;}}
    else{Require(input-output>=fee,Code::Funds,"funding short after exact fee");fee=input-output;}
    Require(fee<=policy.maximum,Code::Fee,"fee exceeds explicit maximum");
    for(const auto& o:tx.vout)Require(!IsDust(o,CFeeRate{DUST_RELAY_TX_FEE}),Code::Policy,"final dust output");
    auto mapping=Reconstruct(tx,spent);Statistics ownership;CheckOwnership(mapping,spent,w.Genesis(),ownership);
    auto acquired=integration::Engine::AcquireInputs(view,tx,w.Genesis(),MANDATORY_SCRIPT_VERIFY_FLAGS,height);Require(acquired.acquired!=nullptr,Code::Inputs,"funded authoritative preflight");
    Require(acquired.fee==fee,Code::Fee,"actual input/output fee mismatch");research::Ledger budget;Require(budget.Reserve(research::Estimate(*acquired.acquired))==research::BudgetError::None,Code::Policy,"block resource limit");
    std::string reason;Require(IsStandardTx(CTransaction(tx),std::nullopt,true,CFeeRate{DUST_RELAY_TX_FEE},reason),Code::Policy,"standard transaction shape or 400000 WU limit");
    Require(acquired.existing_sigops<=MAX_STANDARD_TX_SIGOPS_COST,Code::Policy,"existing signature cost limit");
    auto policy_check=integration::Engine::AcquireInputs(view,tx,w.Genesis(),MANDATORY_SCRIPT_VERIFY_FLAGS,height,integration::Policy::CurrentPolicy);
    Require(view->Epoch()==acquired.acquisition_epoch,Code::Inputs,"view changed while funding");
    auto psbt=GroupPSBT::Create(tx,spent,addresses);
    return {std::move(psbt),tx,Measure(tx),fee,mapping.groups.size(),native_count,selected.size(),0,policy_check.acquired!=nullptr,policy_check.failure.reason};
}
}
CAmount Funding::Fee(size_t bytes,const FeePolicy& p)
{
    Require(bytes<=4*1024*1024&&p.per_k>=1&&p.per_k<=MAX_MONEY/4096&&MoneyRange(p.minimum)&&MoneyRange(p.maximum)&&p.maximum>=p.minimum,Code::Fee,"invalid fee policy or size");
    auto fee=std::max<CAmount>({1,p.minimum,CFeeRate{p.per_k}.GetFee(static_cast<int32_t>(bytes))});Require(fee<=p.maximum,Code::Fee,"fee exceeds maximum");return fee;
}
Funded Funding::Exact(const Wallet& w,const integration::ViewPtr& v,std::span<const FundingCoin> coins,std::span<const CTxOut> outputs,const Address* change,FundingMode mode,FeePolicy p,int height)
{return Assemble(w,v,Resolve(w,v,coins,mode,false,height),std::vector<CTxOut>(outputs.begin(),outputs.end()),change,p,false,height);}
Funded Funding::Fund(const Wallet& w,const integration::ViewPtr& v,std::span<const FundingCoin> coins,std::span<const CTxOut> outputs,const Address& change,FundingMode mode,FeePolicy p,int height)
{
    Fee(1,p);auto available=Resolve(w,v,coins,mode,true,height);Require(!available.empty(),Code::Funds,"strict mode has no eligible coins");
    CAmount target=0;size_t overhead=12+GetSerializeSize(CTxOut{2,change.script});for(const auto& o:outputs){Require(!IsDust(o,CFeeRate{DUST_RELAY_TX_FEE}),Code::Policy,"recipient dust");target=AddAmount(target,o.nValue);overhead+=GetSerializeSize(o);}
    Require(overhead<MAX_STANDARD_TX_WEIGHT/4,Code::Policy,"recipient batch exceeds standard weight");target=AddAmount(target,Fee(overhead,p));std::optional<Funded> best;size_t attempts=0;
    std::set<std::vector<COutPoint>> tried;
    auto consider=[&](const wallet::SelectionResult& result){std::set<COutPoint> points;for(const auto& c:result.GetInputSet())points.insert(c->outpoint);std::vector<COutPoint> ordered(points.begin(),points.end());if(!tried.insert(ordered).second)return;
        std::vector<FundingCoin> picked;for(const auto& c:available)if(points.contains(c.candidate.point))picked.push_back(c.candidate);
        try{auto candidate=Exact(w,v,picked,outputs,&change,mode,p,height);++attempts;
            // Complete immediate fee then byte/weight cost, not raw input count.
            if(!best||std::tie(candidate.fee,candidate.sizes.total,candidate.sizes.weight)<std::tie(best->fee,best->sizes.total,best->sizes.weight))best=std::move(candidate);
        }catch(const WalletError& e){if(e.code!=Code::Funds&&e.code!=Code::Policy&&e.code!=Code::Mode&&e.code!=Code::Fee)throw;}};
    // Disjoint partitions give native solvers individual coins, progressively
    // shared-key chunks and whole groups; no partition contains overlaps.
    for(size_t chunk:{size_t(1),size_t(2),size_t(4),size_t(8),size_t(16),size_t(32),size_t(64),size_t(1024)}){
        std::map<std::pair<uint8_t,PublicKey>,std::vector<const Resolved*>> buckets;for(const auto& c:available)buckets[{c.owner.location.witness,c.owner.pk}].push_back(&c);
        std::vector<wallet::OutputGroup> pool;
        for(auto& [key,bucket]:buckets){std::sort(bucket.begin(),bucket.end(),[](auto a,auto b){return a->out.nValue!=b->out.nValue?a->out.nValue>b->out.nValue:a->candidate.point<b->candidate.point;});size_t step=key.first==2?1:chunk;
            for(size_t start=0;start<bucket.size();start+=step){wallet::OutputGroup group;size_t end=std::min(bucket.size(),start+step),count=end-start;
                size_t bytes=key.first==2?5309:5342+(count-1)*80;CAmount group_fee=CFeeRate{p.per_k}.GetFee(static_cast<int32_t>(bytes));
                for(size_t j=start;j<end;++j){auto r=bucket[j];bool anchor=j==start;int input_bytes=key.first==2?5309:anchor?5342:80;int weight=key.first==2?5432:anchor?5465:203;
                    auto output=std::make_shared<wallet::COutput>(r->candidate.point,r->out,r->candidate.depth,input_bytes,true,r->candidate.safe,0,false,anchor?group_fee:CAmount{0},weight);
                    group.Insert(output,r->candidate.ancestors,r->candidate.cluster_count);}
                if(group.EligibleForSpending(wallet::CoinEligibilityFilter{1,1,25,25})&&group.GetSelectionAmount()>0)pool.push_back(std::move(group));}}
        if(pool.empty())continue;const int max_weight=MAX_STANDARD_TX_WEIGHT-static_cast<int>(overhead*4);
        auto bnb=wallet::SelectCoinsBnB(pool,target,Fee(5342+43,p),max_weight);if(bnb)consider(*bnb);
        auto grinder=wallet::CoinGrinder(pool,target,2,max_weight);if(grinder)consider(*grinder);
        FastRandomContext rng(true);auto fallback=wallet::KnapsackSolver(pool,target,2,rng,max_weight);if(fallback)consider(*fallback);
    }
    Require(best.has_value(),Code::Funds,"no standard candidate funds strict mode");best->selection_attempts=attempts;return std::move(*best);
}
std::vector<Funded> Funding::Sweep(const Wallet& w,const integration::ViewPtr& v,std::span<const FundingCoin> coins,const CScript& destination,FundingMode mode,FeePolicy p,int height)
{
    Fee(1,p);auto available=Resolve(w,v,coins,mode,false,height);Require(!available.empty(),Code::Funds,"empty sweep");
    std::sort(available.begin(),available.end(),[](const auto& a,const auto& b){return std::tie(a.owner.pk,a.candidate.point)<std::tie(b.owner.pk,b.candidate.point);});
    std::vector<Funded> batches;std::vector<Resolved> pending;std::optional<Funded> previous;
    auto build=[&]{return Assemble(w,v,pending,std::vector<CTxOut>{CTxOut{2,destination}},nullptr,p,true,height);};
    for(const auto& c:available){pending.push_back(c);try{previous=build();}catch(const WalletError& e){if(e.code!=Code::Policy)throw;const std::string_view cause=e.what();if(cause!="group member bound"&&cause!="group count bound"&&cause!="standard transaction shape or 400000 WU limit"&&cause!="block resource limit"&&cause!="existing signature cost limit")throw;Require(previous.has_value(),Code::Policy,"single sweep input exceeds policy");pending.pop_back();batches.push_back(std::move(*previous));pending={c};previous=build();}}
    if(previous)batches.push_back(std::move(*previous));return batches;
}
}
