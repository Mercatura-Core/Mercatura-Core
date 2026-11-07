// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <test/mercaweave/node_validation.h>
#include <validation.h>
#include <test/mercaweave/integration_fixtures.h>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sys/resource.h>
using namespace mercaweave;
using namespace mercaweave::test;
using namespace mercaweave::integration;
using node_research::Context;
namespace fixture=mercaweave::integration::test;
namespace {
constexpr auto FLAGS=mercaweave::integration::FIXTURE_SCRIPT_VERIFY_FLAGS;
void Require(bool v){if(!v)throw std::runtime_error("WO07 benchmark invariant");}
double Ms(const timeval& v){return v.tv_sec*1000.+v.tv_usec/1000.;}
bool Run(const Fixture& f,const ViewPtr& view,ValidationCache& cache)
{
    auto context=Context::Prepare(view,f.tx,f.genesis,FLAGS);if(!context)return false;
    CCoinsView empty;CCoinsViewCache coins{&empty};for(size_t i=0;i<f.tx.vin.size();++i)coins.AddCoin(f.tx.vin[i].prevout,Coin{CTxOut{f.spent[i]},1,false},false);
    TxValidationState state;LOCK(cs_main);
    return CheckInputScripts(context->Transaction(),state,coins,FLAGS,true,true,context->Native(),cache,nullptr,uint256{std::span<const unsigned char>{f.genesis}},context);
}
void Case(std::string_view name,size_t n,size_t g,size_t native,bool many_outputs=false,bool invalid=false,bool reorg=false)
{
    Fixture f=(n?MixedFixture(n,g,native):Make(native,1));if(!n)ConvertToV2(f);
    if(many_outputs){f.tx.vout.clear();for(int i=0;i<100;++i)f.tx.vout.emplace_back(2,CScript{}<<OP_2<<Bytes(32,0x55));SignFixture(f);}
    if(invalid)f.tx.vin[g-1].scriptWitness.stack[0][100]^=1;
    auto view=fixture::Funded(f);ValidationCache cache{1<<20,1<<20};const auto sizes=Measure(f.tx);
    if(name=="J-near-standard-weight")Require(sizes.weight<=MAX_STANDARD_TX_WEIGHT&&sizes.weight>MAX_STANDARD_TX_WEIGHT*99/100);
    std::vector<double> times;struct rusage a{},b{};getrusage(RUSAGE_SELF,&a);
    for(int i=0;i<3;++i){const auto begin=std::chrono::steady_clock::now();if(reorg)view->SetTip(uint256::ONE);Require(Run(f,view,cache)==!invalid);times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count());}
    getrusage(RUSAGE_SELF,&b);std::sort(times.begin(),times.end());
    std::cout<<name<<','<<f.tx.vin.size()<<','<<f.tx.vout.size()<<','<<g<<','<<(g+native)<<','<<sizes.total<<','<<sizes.weight<<','<<(!invalid)<<','<<times[1]<<','<<times.back()<<','<<(Ms(b.ru_utime)-Ms(a.ru_utime))/3<<','<<(Ms(b.ru_stime)-Ms(a.ru_stime))/3<<','<<b.ru_maxrss<<",actual-script-components\n";
}
void NearBlock()
{
    auto f=MixedFixture(1,1,0);auto view=View::Create(f.genesis);Ledger ledger;std::vector<CMutableTransaction> txs;
    for(uint32_t serial=1;;++serial){fixture::UniqueAndSign(f,serial,1);auto request=Capture(f.tx,f.spent,f.genesis,FLAGS);if(ledger.Reserve(Estimate(*request))!=research::BudgetError::None)break;fixture::Fund(view,f);txs.push_back(f.tx);}
    struct rusage before{};getrusage(RUSAGE_SELF,&before);
    auto start=std::chrono::steady_clock::now();const auto result=Engine::Candidate(view,txs,f.genesis,FLAGS);const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    Require(result.valid&&result.bytes>4*1024*1024*99/100&&result.bytes<=4*1024*1024);struct rusage r{};getrusage(RUSAGE_SELF,&r);
    std::cout<<"K-near-4MiB-block,"<<txs.size()<<','<<txs.size()<<','<<txs.size()<<','<<txs.size()<<','<<result.bytes<<','<<result.weight<<",1,"<<elapsed<<','<<elapsed<<','<<(Ms(r.ru_utime)-Ms(before.ru_utime))<<','<<(Ms(r.ru_stime)-Ms(before.ru_stime))<<','<<r.ru_maxrss<<",WO04-candidate-components\n";
}
}
int main(int argc,char** argv)
{
    try{std::cout<<std::fixed<<std::setprecision(3)<<"case,inputs,outputs,groups,signatures,bytes,weight,accepted,median_ms,worst_ms,user_ms,system_ms,maxrss_kib,path\n";
        if(argc==2&&std::string_view{argv[1]}=="--smoke"){Case("C-one-v3",1,1,0);return 0;}
        Case("A-one-v2",0,0,1);Case("B-ten-v2",0,0,10);Case("C-one-v3",1,1,0);Case("D-53-one-group",53,1,0);Case("E-100-one-group",100,1,0);Case("F-100-ten-groups",100,10,0);Case("G-100-independent",100,100,0);Case("H-mixed",53,1,2);Case("I-100-outputs",1,1,0,true);Case("J-near-standard-weight",1900,2,0);NearBlock();Case("L-late-invalid",100,100,0,false,true);Case("M-reorg-warm-cache",100,1,0,false,false,true);Case("N-100-native-comparator",0,0,100);
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
