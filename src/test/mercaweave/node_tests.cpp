// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include "node_validation.h"
#include "integration_fixtures.h"
#include <checkqueue.h>
#include <undo.h>
#include <random>
#include <iostream>
#include <future>

using namespace mercaweave;
using namespace mercaweave::test;
using namespace mercaweave::integration;
using node_research::Context;
void UpdateCoins(const CTransaction&, CCoinsViewCache&, CTxUndo&, int);
int ApplyTxInUndo(Coin&&, CCoinsViewCache&, const COutPoint&);
namespace fixture = mercaweave::integration::test;
namespace {
constexpr auto FLAGS = STANDARD_SCRIPT_VERIFY_FLAGS;
size_t checks{}, corpus{}, rejects{};
void Check(bool value, const char* reason) { ++checks; if (!value) throw std::runtime_error(reason); }
uint256 Genesis(const B32& g) { return uint256{std::span<const unsigned char>{g}}; }
void Fill(CCoinsViewCache& coins, const Fixture& f)
{
    for (size_t i = 0; i < f.spent.size(); ++i) coins.AddCoin(f.tx.vin[i].prevout, Coin{CTxOut{f.spent[i]}, 1, false}, false);
}
uint256 CacheKey(ValidationCache& cache, const CTransaction& tx, script_verify_flags flags, const B32& g)
{
    uint256 out; auto h = cache.ScriptExecutionCacheHasher();
    h.Write(UCharCast(tx.GetWitnessHash().begin()),32);
    h.Write(reinterpret_cast<unsigned char*>(&flags), sizeof(flags));
    const unsigned char present{1}; h.Write(&present,1); h.Write(g.data(),g.size()); h.Finalize(out.begin()); return out;
}
bool Validate(const ViewPtr& view, const Fixture& f, ValidationCache& cache, bool threaded = false)
{
    auto context = Context::Prepare(view, f.tx, f.genesis, FLAGS);
    if (!context) return false;
    CCoinsView empty; CCoinsViewCache coins{&empty}; Fill(coins,f);
    std::vector<CScriptCheck> jobs; TxValidationState state;
    bool accepted;
    {
        LOCK(cs_main);
        accepted = CheckInputScripts(context->Transaction(),state,coins,FLAGS,true,true,context->Native(),cache,
                                     threaded ? &jobs : nullptr,Genesis(f.genesis),context);
    }
    if (!accepted) return false;
    if (threaded) {
        CCheckQueue<CScriptCheck> queue{3,2}; CCheckQueueControl<CScriptCheck> control{queue};
        control.Add(std::move(jobs)); if (control.Complete()) return false;
    }
    return context->Matches(context->Transaction(),coins,context->Native(),FLAGS,Genesis(f.genesis));
}
void BindingsAndQueue()
{
    auto f=MixedFixture(53,1,2); auto view=fixture::Funded(f);
    ValidationCache cache{1<<20,1<<20};
    {CCoinsView empty;CCoinsViewCache coins{&empty};PrecomputedTransactionData native;TxValidationState state;CTransaction coinbase{research::ResearchCoinbase()};
        LOCK(cs_main);Check(CheckInputScripts(coinbase,state,coins,FLAGS,true,true,native,cache,nullptr,Genesis(f.genesis)),"native coinbase bypass preserved in isolated path");}
    Check(Validate(view,f,cache) && Validate(view,f,cache,true),"native CheckInputScripts inline and actual queue mixed");
    auto context=Context::Prepare(view,f.tx,f.genesis,FLAGS);
    CCoinsView empty; CCoinsViewCache coins{&empty}; Fill(coins,f);
    auto run=[&](const CTransaction& tx,PrecomputedTransactionData& data,script_verify_flags flags,std::optional<uint256> genesis,std::shared_ptr<const Context> ctx,std::vector<CScriptCheck>* jobs=nullptr) {
        TxValidationState state; LOCK(cs_main);
        const bool accepted=CheckInputScripts(tx,state,coins,flags,true,true,data,cache,jobs,genesis,std::move(ctx));
        if(!accepted)Check(state.GetRejectReason()=="research-context-binding","deterministic binding rejection reason");
        return accepted;
    };
    PrecomputedTransactionData foreign; CTransaction copied{f.tx};
    Check(!run(copied,context->Native(),FLAGS,Genesis(f.genesis),context),"equal bytes with foreign transaction pointer rejected");
    Check(!run(context->Transaction(),foreign,FLAGS,Genesis(f.genesis),context),"foreign precomputation rejected");
    Check(!run(context->Transaction(),context->Native(),MANDATORY_SCRIPT_VERIFY_FLAGS,Genesis(f.genesis),context),"changed flags rejected");
    auto wrong=f.genesis; wrong[0]^=1;
    Check(!run(context->Transaction(),context->Native(),FLAGS,Genesis(wrong),context),"changed genesis rejected");
    Check(!run(context->Transaction(),context->Native(),FLAGS,{},context),"missing genesis rejected");
    Check(!run(context->Transaction(),context->Native(),FLAGS,Genesis(f.genesis),{}),"missing context rejected");
    auto saved=context->Native().m_spent_outputs[0]; context->Native().m_spent_outputs[0].nValue++;
    Check(!run(context->Transaction(),context->Native(),FLAGS,Genesis(f.genesis),context),"mutated precomputed amount rejected");
    context->Native().m_spent_outputs[0]=saved;
    auto old=coins.AccessCoin(f.tx.vin[0].prevout); coins.SpendCoin(f.tx.vin[0].prevout);
    auto changed=old; changed.out.nValue++;coins.AddCoin(f.tx.vin[0].prevout,std::move(changed),false);
    Check(!run(context->Transaction(),context->Native(),FLAGS,Genesis(f.genesis),context),"authentic UTXO binding checked before cache");
    coins.SpendCoin(f.tx.vin[0].prevout);coins.AddCoin(f.tx.vin[0].prevout,std::move(old),false);
    std::vector<CScriptCheck> jobs;
    Check(run(context->Transaction(),context->Native(),FLAGS,Genesis(f.genesis),context,&jobs) && jobs.size()==f.tx.vin.size(),"every input queued");
    std::weak_ptr<const Context> lifetime=context; context.reset();
    Check(!lifetime.expired(),"CScriptCheck owns complete context");
    for(auto& job:jobs) Check(!job(),"jobs survive factory owner destruction");
    jobs.clear(); Check(lifetime.expired(),"last queued job releases context");
    context=Context::Prepare(view,f.tx,f.genesis,FLAGS);
    Check(run(context->Transaction(),context->Native(),FLAGS,Genesis(f.genesis),context,&jobs),"stale jobs prepared");
    view->SetTip(uint256::ONE);
    for(auto& job:jobs) Check(bool(job()),"epoch invalidates grouped and native queued jobs");
    Check(!run(context->Transaction(),context->Native(),FLAGS,Genesis(f.genesis),context),"identical restored state cannot revive proof");
}
void CachesAndCorpus()
{
    auto f=MixedFixture(8,2,2); auto view=fixture::Funded(f);
    ValidationCache warm{1<<20,1<<20},cold{1<<20,1<<20};
    const CTransaction tx{f.tx}; const auto key=CacheKey(warm,tx,FLAGS,f.genesis);
    {LOCK(cs_main);warm.m_script_execution_cache.insert(key);}
    Check(Validate(view,f,warm,true) && Validate(view,f,cold,true),"poisoned existing cache cannot bypass full grouped verification");
    {LOCK(cs_main);Check(warm.m_script_execution_cache.contains(key,false),"research route never even consumes cache entry");}
    CCoinsView empty;CCoinsViewCache coins{&empty};Fill(coins,f);PrecomputedTransactionData data;TxValidationState state;
    {LOCK(cs_main);Check(!CheckInputScripts(tx,state,coins,FLAGS,true,true,data,warm,nullptr,Genesis(f.genesis)),"preprimed cache never authorizes contextless v3");}
    std::mt19937_64 rng{0x574f303743414348ULL};
    for (size_t i=0;i<1000;++i) {
        auto mutation=f.tx;
        switch(i%10) {
        case 0: mutation.vout[0].nValue++;break;
        case 1: mutation.nLockTime++;break;
        case 2: mutation.vin[0].nSequence--;break;
        case 3: mutation.vin[0].scriptWitness.stack[0][rng()%3309]^=1;break;
        case 4: mutation.vin.back().scriptWitness.stack[0][rng()%3309]^=1;break;
        case 5: mutation.vin[1].scriptWitness.stack.back()[0]^=1;break;
        case 6: mutation.vin.push_back(mutation.vin.back());break;
        case 7: mutation.vin[0].scriptSig<<OP_0;break;
        case 8: mutation.vin[0].scriptWitness.stack.push_back(Bytes{0});break;
        case 9: break;
        }
        auto original=f.tx; f.tx=mutation;
        const bool baseline=Engine::Validate(view,mutation,f.genesis,FLAGS).valid;
        Check(baseline==(i%10==9),"precise per-mutation acceptance expectation");
        {LOCK(cs_main);warm.m_script_execution_cache.insert(CacheKey(warm,CTransaction{mutation},FLAGS,f.genesis));}
        const bool cached=Validate(view,f,warm,i%2),uncached=Validate(view,f,cold,i%2);
        Check(cached==baseline && uncached==baseline,"cold/warm/adversarial agreement");
        if (!baseline) ++rejects; ++corpus;f.tx=std::move(original);
    }
    Check(rejects==900,"exact deterministic rejection cardinality");
    auto native=Make(2,1);ConvertToV2(native);CCoinsViewCache native_coins{&empty};Fill(native_coins,native);
    CTransaction native_tx{native.tx};PrecomputedTransactionData native_data;
    {LOCK(cs_main);TxValidationState s;Check(CheckInputScripts(native_tx,s,native_coins,FLAGS,true,true,native_data,warm,nullptr,Genesis(native.genesis)),"genuine native v2 cache population");}
    std::vector<CScriptCheck> jobs;
    {LOCK(cs_main);TxValidationState s;Check(CheckInputScripts(native_tx,s,native_coins,FLAGS,true,true,native_data,warm,&jobs,Genesis(native.genesis)) && jobs.empty(),"native v2 warm cache remains unchanged");}
    auto wrong=native.genesis;wrong[0]^=1;
    {LOCK(cs_main);TxValidationState s;Check(!CheckInputScripts(native_tx,s,native_coins,FLAGS,true,true,native_data,warm,nullptr,Genesis(wrong)),"native cached result cannot cross genesis");}
    for(int change=0;change<3;++change){auto mutated=native.tx;if(change==0)mutated.vout[0].nValue++;else if(change==1)mutated.vin[0].scriptWitness.stack[0][100]^=1;else mutated.nLockTime++;
        CTransaction immutable{mutated};PrecomputedTransactionData fresh;TxValidationState s;LOCK(cs_main);
        Check(!CheckInputScripts(immutable,s,native_coins,FLAGS,true,true,fresh,warm,nullptr,Genesis(native.genesis)),"native cached result binds witness and body");
        Check(s.GetRejectReason().find("mempool-script-verify-flag-failed")==0,"native failure retains actual script-check provenance");
    }
}
void UndoAndBranches()
{
    auto a=MixedFixture(3,1,1);auto view=fixture::Funded(a);
    CCoinsView empty;CCoinsViewCache base{&empty};Fill(base,a);
    auto proof=Context::Prepare(view,a.tx,a.genesis,FLAGS);
    CTxUndo undo;
    {CCoinsViewCache staged{&base};UpdateCoins(CTransaction{a.tx},staged,undo,200);staged.Flush();Check(true,"actual UpdateCoins commits staged valid inputs");}
    Check(undo.vprevout.size()==a.tx.vin.size(),"actual per-input undo captured");
    view->SetTip(uint256::ONE);
    Check(!proof->Matches(proof->Transaction(),base,proof->Native(),FLAGS,Genesis(a.genesis)),"connect invalidates old proof");
    for(size_t i=0;i<a.tx.vout.size();++i) Check(base.SpendCoin(COutPoint{CTransaction{a.tx}.GetHash(),uint32_t(i)}),"disconnect removes outputs");
    for(size_t i=a.tx.vin.size();i-->0;) Check(ApplyTxInUndo(std::move(undo.vprevout[i]),base,a.tx.vin[i].prevout)==DISCONNECT_OK,"actual ApplyTxInUndo restores coin");
    for(size_t i=0;i<a.tx.vin.size();++i) {const auto& coin=base.AccessCoin(a.tx.vin[i].prevout);Check(coin.out==a.spent[i]&&coin.nHeight==1&&!coin.fCoinBase,"undo metadata exactly restored");}
    auto b=MixedFixture(3,1,1);b.tx.vout[0].nValue--;
    const NativeSession native{b.tx,b.spent,b.genesis};auto sig=SignDigest(native.Digest(3),*b.keys[0],B32{},"Mercatura/PQAuth/v1");b.tx.vin[3].scriptWitness.stack[0].assign(sig.begin(),sig.end());SignFixture(b);
    Check(Validate(view,b,*std::make_unique<ValidationCache>(1<<20,1<<20)),"conflicting branch B revalidates with new proof");
    CTxUndo undo_b;UpdateCoins(CTransaction{b.tx},base,undo_b,200);
    for(size_t i=0;i<b.tx.vout.size();++i) base.SpendCoin(COutPoint{CTransaction{b.tx}.GetHash(),uint32_t(i)});
    for(size_t i=b.tx.vin.size();i-->0;) Check(ApplyTxInUndo(std::move(undo_b.vprevout[i]),base,b.tx.vin[i].prevout)==DISCONNECT_OK,"disconnect competing branch");
    view->SetTip(uint256{});Check(bool(Context::Prepare(view,a.tx,a.genesis,FLAGS)),"return to A requires complete fresh verification");
    {CCoinsViewCache staged{&base};CTxUndo discarded;UpdateCoins(CTransaction{a.tx},staged,discarded,200);auto invalid=a.tx;invalid.vin[0].scriptWitness.stack[0][0]^=1;Check(!Context::Prepare(view,invalid,a.genesis,FLAGS),"late invalid group rejects candidate");}
    for(const auto& in:a.tx.vin) Check(base.HaveCoin(in.prevout),"abandoned staging never mutates authoritative parent");
}
void DescendantBlock()
{
    auto parent=MixedFixture(3,1,1), child=MixedFixture(1,1,0);
    parent.tx.vout[0].scriptPubKey=child.spent[0].scriptPubKey;
    const NativeSession native{parent.tx,parent.spent,parent.genesis};
    const auto sig=SignDigest(native.Digest(3),*parent.keys[0],B32{},"Mercatura/PQAuth/v1");
    parent.tx.vin[3].scriptWitness.stack[0].assign(sig.begin(),sig.end());SignFixture(parent);
    child.tx.vin[0].prevout={CTransaction{parent.tx}.GetHash(),0};child.spent[0]=parent.tx.vout[0];
    child.tx.vout[0].nValue=child.spent[0].nValue-100;SignFixture(child);
    CCoinsView empty;CCoinsViewCache base{&empty};Fill(base,parent);
    std::vector<Fixture*> chain{&parent,&child};std::vector<CTxUndo> undo;
    {
        CCoinsViewCache staged{&base};ValidationCache cache{1<<20,1<<20};
        for(const auto* tx:chain){
            auto authentic=View::Create(tx->genesis);
            for(const auto& in:tx->tx.vin){const auto& coin=staged.AccessCoin(in.prevout);Check(!coin.IsSpent(),"candidate obtains child prevout from actual parent staging");authentic->Insert(in.prevout,Coin{CTxOut{coin.out},int(coin.nHeight),bool(coin.fCoinBase)});}
            auto context=Context::Prepare(authentic,tx->tx,tx->genesis,FLAGS);
            Check(bool(context),"parent/descendant immutable authorization");TxValidationState state;
            {LOCK(cs_main);Check(CheckInputScripts(context->Transaction(),state,staged,FLAGS,true,true,context->Native(),cache,nullptr,Genesis(tx->genesis),context),"real script dispatch against layered block coins");}
            undo.emplace_back();UpdateCoins(context->Transaction(),staged,undo.back(),200);
        }
        staged.Flush();
    }
    const auto child_point=COutPoint{CTransaction{child.tx}.GetHash(),0};
    Check(!base.HaveCoin(child.tx.vin[0].prevout)&&base.HaveCoin(child_point),"multi-transaction block commits only descendant output");
    for(size_t t=chain.size();t-->0;){const auto& tx=chain[t]->tx;for(size_t o=0;o<tx.vout.size();++o)Check(base.SpendCoin(COutPoint{CTransaction{tx}.GetHash(),uint32_t(o)}),"reverse block removes descendant then parent outputs");for(size_t i=tx.vin.size();i-->0;)Check(ApplyTxInUndo(std::move(undo[t].vprevout[i]),base,tx.vin[i].prevout)==DISCONNECT_OK,"reverse block restores actual native undo");}
    Check(!base.HaveCoin(child_point)&&!base.HaveCoin(child.tx.vin[0].prevout),"disconnect removes entire branch");
    for(size_t i=0;i<parent.tx.vin.size();++i)Check(base.AccessCoin(parent.tx.vin[i].prevout).out==parent.spent[i],"branch endpoint UTXOs exactly restored");
}

}
int main()
{
    try {BindingsAndQueue();CachesAndCorpus();UndoAndBranches();DescendantBlock();
        std::cout<<"{\"passed\":true,\"assertions\":"<<checks<<",\"cache_corpus\":"<<corpus<<",\"rejected\":"<<rejects<<",\"actual_checkinput_and_cscriptcheck\":true,\"actual_undo_components\":true,\"full_chainstate_v3\":false}\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
