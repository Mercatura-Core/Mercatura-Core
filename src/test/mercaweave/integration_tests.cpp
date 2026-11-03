// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include "integration_fixtures.h"
#include <crypto/sha256.h>
#include <barrier>
#include <future>
#include <iostream>
#include <map>
#include <random>
#include <tuple>
#include <type_traits>

using namespace mercaweave;
using namespace mercaweave::test;
using namespace mercaweave::integration;
namespace {
namespace fixture = mercaweave::integration::test;
constexpr auto FLAGS = STANDARD_SCRIPT_VERIFY_FLAGS;
size_t assertions{0}, rejected{0}, mutations{0}, concurrent{0};
std::map<int, size_t> mutation_stages;
void Check(bool b, const char* name) { ++assertions; if (!b) throw std::runtime_error(name); }
void Bad(const ViewPtr& v, const CMutableTransaction& tx, const B32& g, Stage stage, std::optional<Error> error = {})
{
    auto a = Engine::Validate(v, tx, g, FLAGS);
    auto b = Engine::Validate(v, tx, g, FLAGS);
    Check(!a.valid && !a.preparation.proof && a.failure.stage == stage && a.failure.group_error == error, "exact rejection provenance");
    Check(a.failure.reason == b.failure.reason && a.failure.stage == b.failure.stage && a.preparation.work.verification_calls == b.preparation.work.verification_calls,
          "deterministic failure and work");
    ++rejected;
}
void PositiveAndBindings()
{
    static_assert(!std::is_default_constructible_v<TransactionProof> && !std::is_aggregate_v<TransactionProof>);
    static_assert(!std::is_constructible_v<TransactionProof, Result> && !std::is_default_constructible_v<ScriptJob>);
    for (const auto [n,g,v2] : {std::array<size_t,3>{1,1,0}, {10,1,0}, {7,3,0}, {6,2,2}, {1024,1,0}, {256,256,0}, {4096,4,0}}) {
        auto f = MixedFixture(n,g,v2);
        const auto result = Engine::Validate(fixture::Funded(f), f.tx, f.genesis, FLAGS);
        Check(result.valid && result.preparation.work.verification_calls == g && result.preparation.work.ownership_checks == n, "real maximum/mixed transaction pipeline");
        size_t hooks{0}, native{0};
        for (const auto& script : result.scripts) { hooks += script.hook_calls; native += script.native_calls; }
        Check(hooks == n && native == v2 && result.preparation.native_prepare_calls == v2, "production hook and both genuine native passes counted");
        Check(result.preparation.fee_bytes == static_cast<int64_t>(Measure(f.tx).total) && result.preparation.legacy_weight == static_cast<int64_t>(Measure(f.tx).weight), "actual undiscounted fee/legacy weight helpers");
    }
    auto f = MixedFixture(6,2,1);
    auto view = fixture::Funded(f);
    auto prepared = Engine::Prepare(view, f.tx, f.genesis, FLAGS);
    Check(bool(prepared.proof), "full authoritative proof");
    const auto original = f.tx;
    Check(Engine::Bind(view, f.tx, f.genesis, FLAGS, 200, {}).failure.stage == Stage::MissingContext, "missing context cannot mint jobs");
    auto reject = [&](const ViewPtr& v, const CMutableTransaction& tx, B32 g, script_verify_flags flags, int height, Stage stage) {
        auto bound = Engine::Bind(v, tx, g, flags, height, prepared.proof);
        Check(bound.jobs.empty() && bound.failure.stage == stage, "full binding mismatch has no usable job"); ++rejected;
    };
    f.tx.version++; reject(view,f.tx,f.genesis,FLAGS,200,Stage::Binding); f.tx=original;
    f.tx.vin[0].scriptWitness.stack[0][100]^=1; reject(view,f.tx,f.genesis,FLAGS,200,Stage::Binding); f.tx=original;
    auto wrong_genesis=f.genesis; wrong_genesis[0]^=1;
    reject(view,f.tx,wrong_genesis,FLAGS,200,Stage::Binding);
    reject(view,f.tx,f.genesis,MANDATORY_SCRIPT_VERIFY_FLAGS,200,Stage::Binding);
    reject(view,f.tx,f.genesis,FLAGS,201,Stage::Binding);
    reject(fixture::Funded(f),f.tx,f.genesis,FLAGS,200,Stage::StaleView);
    auto bound = Engine::Bind(view, f.tx, f.genesis, FLAGS, 200, prepared.proof);
    f.tx.vout[0].nValue++;
    Check(bound.jobs[0].Run().valid, "job owns original exact transaction despite source mutation");
    Check(!Engine::Bind(view,f.tx,f.genesis,FLAGS,200,prepared.proof).jobs.size(), "source mutation cannot rebind proof"); f.tx=original;
    std::mt19937_64 rng{0x574f30344f524445ULL};
    std::vector<uint32_t> order{0,1,2,3,4,5,6};
    for (size_t n=0;n<20;++n) {
        std::shuffle(order.begin(),order.end(),rng);
        Check(Engine::Validate(view,f.tx,f.genesis,FLAGS,200,Policy::ConsensusFixture,order).valid,"permuted real Script checks");
    }
    order.back()=order.front();
    Check(!Engine::Validate(view,f.tx,f.genesis,FLAGS,200,Policy::ConsensusFixture,order).valid,"cannot reuse one input success for another index");
    std::barrier start{4};
    std::vector<std::future<bool>> workers;
    for (uint32_t w=0;w<4;++w) workers.push_back(std::async(std::launch::async,[&,w] {
        start.arrive_and_wait();
        for(uint32_t round=0;round<100;++round) for(uint32_t i=0;i<7;++i)
            if(!bound.jobs[(i*3+w)%7].Run().valid) return false;
        return true;
    }));
    for(auto& worker:workers) Check(worker.get(),"barrier-parallel genuine VerifyScript"); concurrent += 2800;
    workers.clear();
    for(int w=0;w<4;++w) workers.push_back(std::async(std::launch::async,[&] {
        for(int n=0;n<10;++n) if(!Engine::Validate(view,f.tx,f.genesis,FLAGS).valid) return false;
        return true;
    }));
    for(auto& worker:workers) Check(worker.get(),"reentrant full validator");
    view->SetTip(uint256{});
    Check(!bound.jobs[0].Run().valid && bound.jobs[0].Run().failure.stage==Stage::StaleView,"tip mutation invalidates existing immutable jobs");
    reject(view,f.tx,f.genesis,FLAGS,200,Stage::StaleView);
    Check(Engine::Validate(view,f.tx,f.genesis,FLAGS).valid,"fresh view proof after tip change");
}
void Negatives()
{
    auto f=MixedFixture(6,2,1);
    auto v=fixture::Funded(f);
    const auto tx=f.tx;
    f.tx.vin[5].scriptWitness.stack[0][0]^=1; Bad(v,f.tx,f.genesis,Stage::Group,Error::Ownership); f.tx=tx;
    f.tx.vin[1].scriptWitness.stack[0][100]^=1;
    Bad(v,f.tx,f.genesis,Stage::Group,Error::Signature);
    const auto late=Engine::Prepare(v,f.tx,f.genesis,FLAGS);
    Check(!late.proof && late.work.verification_calls==2,"late group failure publishes nothing"); f.tx=tx;
    f.tx.vout[0].nValue++; Bad(v,f.tx,f.genesis,Stage::Group,Error::Signature); f.tx=tx;
    f.tx.vin[0].scriptWitness.stack[0].pop_back(); Bad(v,f.tx,f.genesis,Stage::Group,Error::Witness); f.tx=tx;
    f.tx.vin[0].scriptWitness.stack[1].pop_back(); Bad(v,f.tx,f.genesis,Stage::Group,Error::Witness); f.tx=tx;
    f.tx.vin[2].scriptWitness.stack[1]={0,0,0xff,0xff}; Bad(v,f.tx,f.genesis,Stage::Group,Error::Reference); f.tx=tx;
    f.tx.vin[1].scriptWitness.stack[1]=f.tx.vin[0].scriptWitness.stack[1]; Bad(v,f.tx,f.genesis,Stage::Group,Error::DuplicateKey); f.tx=tx;
    f.tx.vin[0].scriptSig<<OP_0; Bad(v,f.tx,f.genesis,Stage::Group,Error::ScriptSig); f.tx=tx;
    std::swap(f.tx.vin[0],f.tx.vin[2]); Bad(v,f.tx,f.genesis,Stage::Group,Error::Reference); f.tx=tx;
    auto g=f.genesis;g[0]^=1; Bad(v,f.tx,g,Stage::Network);
    Check(Engine::Prepare(v,f.tx,f.genesis,SCRIPT_VERIFY_NONE).failure.stage==Stage::Flags,"flag downgrade rejects before Script");
    auto coin=*v->CoinAt(f.tx.vin[0].prevout);
    auto changed=coin; changed.out.nValue++;
    v->Insert(f.tx.vin[0].prevout,changed,true); Bad(v,f.tx,f.genesis,Stage::Group,Error::Signature);
    v->Insert(f.tx.vin[0].prevout,coin,true);
    changed=coin;changed.out.scriptPubKey[3]^=1;
    v->Insert(f.tx.vin[0].prevout,changed,true); Bad(v,f.tx,f.genesis,Stage::Group,Error::Ownership);
    changed=coin;changed.out.scriptPubKey=CScript{}<<OP_3<<Bytes(31,0x42);
    v->Insert(f.tx.vin[0].prevout,changed,true); Bad(v,f.tx,f.genesis,Stage::Group,Error::Program);
    changed=coin;changed.out.nValue=-2; // -1 is the native spent/null sentinel.
    v->Insert(f.tx.vin[0].prevout,changed,true); Bad(v,f.tx,f.genesis,Stage::Inputs);
    v->Insert(f.tx.vin[0].prevout,coin,true);
    const Bytes redeem(coin.out.scriptPubKey.begin(),coin.out.scriptPubKey.end());
    std::array<unsigned char,20> wrapped_hash;CHash160().Write(redeem).Finalize(wrapped_hash);
    changed=coin;changed.out.scriptPubKey=CScript{}<<OP_HASH160<<Bytes(wrapped_hash.begin(),wrapped_hash.end())<<OP_EQUAL;
    v->Insert(f.tx.vin[0].prevout,changed,true);f.tx.vin[0].scriptSig=CScript{}<<redeem;
    Bad(v,f.tx,f.genesis,Stage::Group,Error::Wrapped);f.tx=tx;
    v->Insert(f.tx.vin[0].prevout,coin,true);
    f.tx.vin[5].scriptWitness.stack.resize(1004); Bad(v,f.tx,f.genesis,Stage::Transaction); f.tx=tx;
    f.tx.vin[5].scriptWitness.stack[0].resize(Consensus::MAX_WITNESS_ITEM_BYTES+1); Bad(v,f.tx,f.genesis,Stage::Transaction); f.tx=tx;
    for(const auto [n,g,e]:{std::tuple<size_t,size_t,Error>{4097,5,Error::InputsLimit},{257,257,Error::GroupsLimit},{1025,1,Error::MembersLimit}}) {
        auto structural=Make(n,g,false,true); Bad(fixture::Funded(structural),structural.tx,structural.genesis,Stage::Group,e);
    }
    auto preceding=MixedFixture(3,1,1);
    std::rotate(preceding.tx.vin.begin(),preceding.tx.vin.begin()+3,preceding.tx.vin.end());
    std::rotate(preceding.spent.begin(),preceding.spent.begin()+3,preceding.spent.end());
    for(size_t i=2;i<4;++i) WriteLE32(preceding.tx.vin[i].scriptWitness.stack[1].data(),1);
    SignFixture(preceding);
    NativeSession native(preceding.tx,preceding.spent,preceding.genesis);
    auto sig=SignDigest(native.Digest(0),*preceding.keys[0],B32{},"Mercatura/PQAuth/v1");
    preceding.tx.vin[0].scriptWitness.stack[0].assign(sig.begin(),sig.end());
    auto pv=fixture::Funded(preceding);
    Check(Engine::Validate(pv,preceding.tx,preceding.genesis,FLAGS).valid,"preceding native-v2 input works");
    preceding.tx.vin[0].scriptWitness.stack[0][100]^=1;
    Bad(pv,preceding.tx,preceding.genesis,Stage::Group,Error::NonV3);
    const auto wire=Wire(tx);
    auto noncanonical=wire;
    noncanonical.erase(noncanonical.begin()+6);
    noncanonical.insert(noncanonical.begin()+6,{0xfd,7,0});
    Check(!Engine::ValidateWire(v,noncanonical,f.genesis,FLAGS).valid,"noncanonical CompactSize never reaches authorization");
    auto truncated=wire;truncated.pop_back();
    Check(Engine::ValidateWire(v,truncated,f.genesis,FLAGS).failure.reason=="bounded-decode","truncation provenance");
    std::mt19937_64 rng{0x574f3034494e5447ULL};
    for(size_t i=0;i<2000;++i) {
        auto bytes=wire;bytes[rng()%bytes.size()]^=static_cast<unsigned char>(1+rng()%255);
        auto result=Engine::ValidateWire(v,bytes,f.genesis,FLAGS);
        Check(!result.valid && !result.preparation.proof,"seeded integrated mutation cannot mint proof");
        ++mutation_stages[static_cast<int>(result.failure.stage)]; ++mutations;
    }
}
void StateAndPolicy()
{
    auto f=Make(3,1);SignFixture(f);
    auto v=fixture::Funded(f);
    auto p=Engine::Prepare(v,f.tx,f.genesis,FLAGS);
    auto jobs=Engine::Bind(v,f.tx,f.genesis,FLAGS,200,p.proof);
    const auto coin=*v->CoinAt(f.tx.vin[0].prevout);
    Check(v->Spend(f.tx.vin[0].prevout),"spend real cached coin");
    Check(jobs.jobs[0].Run().failure.stage==Stage::StaleView,"spent coin invalidates old job");
    Bad(v,f.tx,f.genesis,Stage::Inputs);
    v->Insert(f.tx.vin[0].prevout,coin);
    Check(Engine::Validate(v,f.tx,f.genesis,FLAGS).valid && !jobs.jobs[0].Run().valid,"restored coins require new proof");
    // All new follower UTXOs are present BEFORE the original proof is captured.
    B32 nonce{};nonce[0]=0x7a;
    auto extra=Make(1,1);extra.tx.vin[0].prevout.n=90000;
    extra.tx.vin[0].scriptWitness.stack={Bytes(nonce.begin(),nonce.end()),Bytes(4)};
    auto extra_out=CTxOut{1000,Output(f.genesis,f.keys[0]->pk,nonce)};
    v->Insert(extra.tx.vin[0].prevout,Coin{CTxOut(extra_out),1,false});
    auto before=Engine::Prepare(v,f.tx,f.genesis,FLAGS);
    f.tx.vin.push_back(extra.tx.vin[0]);f.spent.push_back(extra_out);
    Check(Engine::Bind(v,f.tx,f.genesis,FLAGS,200,before.proof).failure.stage==Stage::Binding,"appended follower cannot reuse matching-view proof");
    Bad(v,f.tx,f.genesis,Stage::Group,Error::Signature);
    SignFixture(f);Check(Engine::Validate(v,f.tx,f.genesis,FLAGS).valid,"new follower requires real fresh signature");
    auto policy=Engine::Validate(v,f.tx,f.genesis,FLAGS,200,Policy::CurrentPolicy);
    Check(!policy.valid && policy.failure.stage==Stage::Policy && policy.failure.reason=="inputs-not-standard","experimental Script cannot bypass current unknown-v3 policy");
    auto large=Make(100,100);SignFixture(large);
    const auto large_policy=Engine::Prepare(fixture::Funded(large),large.tx,large.genesis,FLAGS,200,Policy::CurrentPolicy);
    Check(!large_policy.proof && large_policy.failure.reason=="tx-size" && MAX_STANDARD_TX_WEIGHT==400000,"400000-WU standard limit unchanged");
    auto v2=Make(2,1);ConvertToV2(v2);
    auto v2view=fixture::Funded(v2);
    auto v2result=Engine::Validate(v2view,v2.tx,v2.genesis,FLAGS,200,Policy::CurrentPolicy);
    Check(v2result.valid && v2result.preparation.native_prepare_calls==2,"native v2 genuine full input/witness policy passes");
    Check(Engine::Bind(v,f.tx,f.genesis,FLAGS,200,v2result.preparation.proof).jobs.empty(),"native-v2 proof cannot authorize v3");
    auto immature=View::Create(v2.genesis);fixture::Fund(immature,v2,199,true);
    Bad(immature,v2.tx,v2.genesis,Stage::Inputs);
    Check(Engine::Validate(immature,v2.tx,v2.genesis,FLAGS,400).valid,"genuine coinbase maturity check");
}
void Blocks()
{
    auto f=Make(3,1);
    auto view=View::Create(f.genesis);
    auto tiny=Budget{};tiny.bytes=0;
    const auto empty=Engine::Candidate(view,{},f.genesis,FLAGS,false,200,tiny);
    Check(!empty.valid && empty.failure.budget_error==BudgetError::Bytes,"empty candidate must reserve its block envelope");
    std::vector<CMutableTransaction> txs;
    for(uint32_t serial=1;serial<=3;++serial) { fixture::UniqueAndSign(f,serial,3);fixture::Fund(view,f);txs.push_back(f.tx); }
    const auto old_epoch=view->Epoch();
    auto last=txs;last.back().vin[0].scriptWitness.stack[0][100]^=1;
    auto invalid=Engine::Candidate(view,last,f.genesis,FLAGS,true);
    Check(!invalid.valid && !invalid.committed && invalid.charged.transactions==3 && view->Epoch()==old_epoch,"late invalid candidate retains charges and rolls back all prior writes");
    Check(view->CoinAt(txs[0].vin[0].prevout).has_value(),"rollback leaves original UTXO unspent");
    auto conflict=txs;conflict[1]=conflict[0];
    auto double_spend=Engine::Candidate(view,conflict,f.genesis,FLAGS,true);
    Check(!double_spend.valid && double_spend.failure.stage==Stage::Inputs && view->Epoch()==old_epoch,"candidate conflicting spend rejected atomically");
    auto valid=Engine::Candidate(view,txs,f.genesis,FLAGS,true);
    Check(valid.valid && valid.committed && !view->CoinAt(txs[0].vin[0].prevout),"actual layered coin-cache commit spends inputs");
    Check(view->CoinAt({CTransaction(txs[0]).GetHash(),0}).has_value(),"candidate commit creates output");
    auto maxgroups=Make(256,256);
    auto bigview=View::Create(maxgroups.genesis);
    std::vector<CMutableTransaction> large;
    for(uint32_t serial=1;serial<=4;++serial) { fixture::UniqueAndSign(maxgroups,serial,256);fixture::Fund(bigview,maxgroups);large.push_back(maxgroups.tx); }
    auto exceeded=Engine::Candidate(bigview,large,maxgroups.genesis,FLAGS,true);
    Check(!exceeded.valid && exceeded.failure.stage==Stage::Budget && exceeded.failure.budget_error==BudgetError::Bytes,"real 4MiB candidate cap cannot be bypassed");
    Check(MERCATURA_INITIAL_BLOCK_CAPACITY_BYTES==4194304 && exceeded.charged.transactions==3,"existing capacity constant and failed-prefix semantics");
    auto budget=Budget{};budget.sigops=1;
    Check(!Engine::Candidate(bigview,std::span<const CMutableTransaction>(large).first(1),maxgroups.genesis,FLAGS,false,200,budget).valid,"group cost ledger enforces checked combined budget");
}
void Malleability()
{
    for(int form:{0,1}) {
        auto parent=Make(2,1);
        B32 nonce{};nonce[31]=0x90;
        parent.tx.vout[0].scriptPubKey=Output(parent.genesis,parent.keys[0]->pk,nonce);
        auto extra=Make(1,1);extra.tx.vin[0].prevout.n=999;extra.tx.vin[0].scriptWitness.stack.clear();
        CScript predicate=CScript{}<<OP_DROP<<OP_TRUE;Bytes redeem(predicate.begin(),predicate.end());
        CScript locking;
        if(form==0) {
            std::array<unsigned char,20> hash;CHash160().Write(redeem).Finalize(hash);
            locking<<OP_HASH160<<Bytes(hash.begin(),hash.end())<<OP_EQUAL;
            extra.tx.vin[0].scriptSig=CScript{}<<Bytes{0x41}<<redeem;
        } else {
            B32 hash;CSHA256().Write(redeem.data(),redeem.size()).Finalize(hash.data());
            locking<<OP_0<<Bytes(hash.begin(),hash.end());extra.tx.vin[0].scriptWitness.stack={Bytes{0x41},redeem};
        }
        parent.tx.vin.push_back(extra.tx.vin[0]);parent.spent.emplace_back(1000,locking);SignFixture(parent);
        auto original_view=fixture::Funded(parent);auto changed_view=fixture::Funded(parent);
        const auto before=parent.tx;const auto id=CTransaction(before).GetHash();
        auto proof=Engine::Prepare(original_view,parent.tx,parent.genesis,FLAGS);
        Check(bool(proof.proof),"integrated mixed-predicate parent authorized");
        auto child=Make(1,1);child.tx.vin[0].prevout={id,0};child.spent[0]=parent.tx.vout[0];SignFixture(child);
        auto block=Engine::Candidate(original_view,std::span<const CMutableTransaction>(&before,1),parent.genesis,FLAGS,true);
        Check(block.valid && Engine::Validate(original_view,child.tx,child.genesis,FLAGS).valid,"real coin-view original parent/child dependency");
        if(form==0) parent.tx.vin[2].scriptSig=CScript{}<<Bytes{0x42}<<redeem;
        else parent.tx.vin[2].scriptWitness.stack[0][0]=0x42;
        Check(Engine::Validate(changed_view,parent.tx,parent.genesis,FLAGS).valid,"permitted identity mutation preserves integrated authorization");
        auto mutated=Engine::Candidate(changed_view,std::span<const CMutableTransaction>(&parent.tx,1),parent.genesis,FLAGS,true);
        auto descendant=Engine::Validate(changed_view,child.tx,child.genesis,FLAGS);
        Check(mutated.valid && descendant.valid==(form==1),"txid mutation disrupts child in authoritative cache, witness-only mutation does not");
        if(form==0) Check(descendant.failure.stage==Stage::Inputs && descendant.failure.reason=="bad-txns-inputs-missingorspent","actual native descendant rejection reason");
        std::cout<<"{\"case\":\"integrated_"<<(form==0?"p2sh":"p2wsh")<<"\",\"before_txid\":\""<<id.ToString()
                 <<"\",\"after_txid\":\""<<CTransaction(parent.tx).GetHash().ToString()<<"\",\"child_valid\":"<<(descendant.valid?"true":"false")<<"}\n";
        // Reorg model: remove this parent's outputs and restore its spent coins.
        auto child_proof=Engine::Prepare(original_view,child.tx,child.genesis,FLAGS);
        auto child_jobs=Engine::Bind(original_view,child.tx,child.genesis,FLAGS,200,child_proof.proof);
        original_view->Spend({id,0});
        for(size_t i=0;i<before.vin.size();++i) original_view->Insert(before.vin[i].prevout,Coin{CTxOut(parent.spent[i]),1,false});
        original_view->SetTip(uint256{});
        Check(child_jobs.jobs[0].Run().failure.stage==Stage::StaleView,"synthetic reorg invalidates queued child job");
        Check(!Engine::Validate(original_view,child.tx,child.genesis,FLAGS).valid,"disconnected parent removes child's actual coin");
        std::vector<CMutableTransaction> order{child.tx,before};
        Check(!Engine::Candidate(original_view,order,parent.genesis,FLAGS).valid,"child-before-parent candidate order rejects");
        std::reverse(order.begin(),order.end());
        Check(Engine::Candidate(original_view,order,parent.genesis,FLAGS).valid,"parent-before-child candidate validates without network");
    }
    auto native=Make(2,1);SignFixture(native);
    auto view=fixture::Funded(native);
    auto original=CTransaction(native.tx);
    Sign(native.tx,native.spent,native.genesis,native.KeyRefs(),[](B32& r){r.fill(7);return true;});
    Check(original.GetHash()==CTransaction(native.tx).GetHash() && original.GetWitnessHash()!=CTransaction(native.tx).GetWitnessHash() && Engine::Validate(view,native.tx,native.genesis,FLAGS).valid,
          "randomized valid MLDSA preserves native txid in integrated validator");
}
}
int main()
{
    try {
        PositiveAndBindings();Negatives();StateAndPolicy();Blocks();Malleability();
        std::cout<<"{\"assertions\":"<<assertions<<",\"rejected_cases\":"<<rejected<<",\"mutations\":"<<mutations<<",\"concurrent_script_calls\":"<<concurrent<<",\"mutation_stages\":{";
        bool first=true;for(auto [stage,n]:mutation_stages){if(!first)std::cout<<',';first=false;std::cout<<'\"'<<stage<<"\":"<<n;}
        std::cout<<"},\"passed\":true}\n";
    } catch(const std::exception& e) {std::cerr<<"FAIL integration after "<<assertions<<" assertions: "<<e.what()<<'\n';return 1;}
}
