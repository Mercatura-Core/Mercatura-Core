// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include "wallet_fixtures.h"
#include "wallet_funding.h"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <set>
#include <util/translation.h>
const TranslateFn G_TRANSLATION_FUN{nullptr};
using namespace mercaweave;
using namespace mercaweave::wallet_research;
namespace wt=mercaweave::wallet_research::test;
namespace {
using Clock=std::chrono::steady_clock;
double Ms(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
std::string Hex(std::span<const unsigned char> bytes){std::string s;for(auto b:bytes){s.push_back("0123456789abcdef"[b>>4]);s.push_back("0123456789abcdef"[b&15]);}return s;}
struct Case {const char* name;size_t inputs,keys,outputs;bool mining{false},mixed{false},change{false};};
void Run(const Case& c,bool native)
{
    auto w=wt::Owner();auto view=integration::View::Create(wt::Genesis());std::vector<FundingCoin> coins;std::vector<Address> bank;
    for(size_t i=0;i<c.keys;++i){if(!native&&i)w->Rotate(0,c.mining?2:0);bank.push_back(w->Issue(0,(!native&&c.mining)?2:0,native?2:3));}
    std::optional<Address> mixed;if(c.mixed)mixed=w->Issue(0,0,2);
    for(size_t i=0;i<c.inputs;++i){auto a=mixed&&i>=c.inputs/2?*mixed:bank[i%c.keys];auto point=wt::Point(static_cast<uint32_t>(10000+i));view->Insert(point,Coin{CTxOut{10000,a.script},1,c.mining});coins.push_back({point});}
    auto dest=w->Issue(7,0,2),change=w->Issue(0,1,2);std::vector<Address> recipients_public{dest};for(size_t i=1;i<c.outputs;++i)recipients_public.push_back(w->Issue(7,0,2));std::vector<Funded> planned;auto start=Clock::now();
    if(c.outputs==1&&!c.change)planned=Funding::Sweep(*w,view,coins,dest.script);
    else{std::vector<CTxOut> recipients;for(size_t i=0;i<c.outputs;++i)recipients.emplace_back(2,recipients_public[i].script);
        if(c.change)planned.push_back(Funding::Exact(*w,view,coins,recipients,&change));
        else{auto provisional=Funding::Exact(*w,view,coins,recipients,nullptr);recipients.back().nValue=static_cast<CAmount>(c.inputs*10000-2*(c.outputs-1))-Funding::Fee(provisional.sizes.total,{});planned.push_back(Funding::Exact(*w,view,coins,recipients,nullptr));}}
    const double construction=Ms(start);size_t inputs=0,outputs=0,groups=0,natives=0,signatures=0,bytes=0,weight=0,native_prepare=0,native_script=0,max_weight=0,max_bytes=0;CAmount estimated=0,actual=0;double signing=0,verification=0;std::set<PublicKey> keys;Writer deterministic;
    for(auto& f:planned){auto p=GroupPSBT::Decode(GroupPSBT::Encode(f.psbt));auto info=GroupPSBT::Inspect(p,view,wt::Genesis());for(const auto& spent:info.spent)keys.insert(w->Find(spent.scriptPubKey)->pk);
        start=Clock::now();auto result=GroupPSBT::Sign(p,*w,view,wt::ZeroFixture());GroupPSBT::FinalizeAvailable(p,view,wt::Genesis());signing+=Ms(start);
        start=Clock::now();auto tx=GroupPSBT::Extract(p,view,wt::Genesis());auto valid=integration::Engine::Validate(view,tx,wt::Genesis(),MANDATORY_SCRIPT_VERIFY_FLAGS);verification+=Ms(start);
        Require(valid.valid&&result.complete,Code::Signature,"workload validation");auto size=Measure(tx);Require(size.total==f.sizes.total&&size.weight==f.sizes.weight,Code::Fee,"workload size estimate");
        CAmount fee=0;for(const auto& in:tx.vin)fee+=view->CoinAt(in.prevout)->out.nValue;for(const auto& out:tx.vout)fee-=out.nValue;Require(fee==f.fee,Code::Fee,"workload actual fee");
        inputs+=tx.vin.size();outputs+=tx.vout.size();groups+=f.groups;natives+=f.native_inputs;signatures+=result.group_signatures+result.native_signatures;bytes+=size.total;weight+=size.weight;max_weight=std::max(max_weight,size.weight);max_bytes=std::max(max_bytes,size.total);estimated+=f.fee;actual+=fee;native_prepare+=valid.preparation.native_prepare_calls;for(const auto& script:valid.scripts)native_script+=script.native_calls;
        deterministic<<Wire(tx);}
    auto digest=PQH384("MercaWeave/ResearchWalletWorkloadEvidence/v1",deterministic.bytes);
    std::cout<<c.name<<','<<(native?"native_v2":"grouped_or_mixed")<<','<<planned.size()<<','<<inputs<<','<<outputs<<','<<keys.size()<<','<<groups<<','<<natives<<','<<signatures<<','<<bytes<<','<<weight<<','<<max_weight<<','<<max_bytes<<','<<estimated<<','<<actual<<",true,"<<std::fixed<<std::setprecision(3)<<construction<<','<<signing<<','<<verification<<','<<native_prepare<<','<<native_script<<','<<Hex(digest)<<'\n';
}
}
int main(int argc,char** argv)
{
    try{bool smoke=argc==2&&std::string_view(argv[1])=="--smoke";
        std::cout<<"workload,scheme,transactions,inputs,outputs,distinct_keys,groups,native_inputs,signatures,serialized_bytes,sum_standard_weight,max_standard_weight,max_transaction_bytes,estimated_fee_units,actual_fee_units,verified,construction_ms,sign_and_finalize_ms,extract_and_verify_ms,native_prepare_calls,native_script_calls,deterministic_public_fixture_hash\n";
        std::vector<Case> cases{{"A_mining_rewards_reused_address",100,1,1,true},{"B_deposits_reused_address",100,1,1},{"C_independent_deposit_keys",100,100,1},{"D_ten_group_keys",100,10,1},{"E_two_group_keys",100,2,1},{"F_mixed_funding",20,2,2,false,true},{"G_one_input_100_recipients",1,1,100},{"H_ten_inputs_100_recipients",10,1,100},{"I_fifty_grouped_100_recipients",50,1,100},{"J_independent_exchange_sweep",200,200,1},{"K_withdrawals_with_change",20,2,20,false,false,true},{"L_grouped_mining_to_native_savings",100,1,1,true}};
        if(smoke)cases={cases[5],cases[6]};for(const auto& c:cases){Run(c,false);Run(c,true);}}
    catch(const std::exception& e){std::cerr<<"FAIL wallet workload: "<<e.what()<<'\n';return 1;}
}
