// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <test/mercaweave/allocation_probe.h>
#include <test/mercaweave/integration_fixtures.h>
#include <consensus/validation.h>
#include <algorithm>
#include <chrono>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sys/resource.h>

using namespace mercaweave;
using namespace mercaweave::research;
using namespace mercaweave::integration;
namespace fixture = mercaweave::integration::test;
namespace {
constexpr auto FLAGS = mercaweave::integration::FIXTURE_SCRIPT_VERIFY_FLAGS;
void Require(bool b, const char* name) { if (!b) throw std::runtime_error(name); }
struct Timing { double median{-1}, worst{-1}, user{-1}, system{-1}; };
double Seconds(const timeval& t) { return t.tv_sec + t.tv_usec / 1e6; }
Timing Time(const std::function<void()>& run)
{
    run();
    std::vector<double> times;
    struct rusage before{}, after{};getrusage(RUSAGE_SELF,&before);
    for(int i=0;i<7;++i) {
        auto begin=std::chrono::steady_clock::now();run();
        times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count());
    }
    getrusage(RUSAGE_SELF,&after);std::sort(times.begin(),times.end());
    return {times[3],times.back(),(Seconds(after.ru_utime)-Seconds(before.ru_utime))*1000/7,
        (Seconds(after.ru_stime)-Seconds(before.ru_stime))*1000/7};
}
void Print(const Timing& t) { std::cout<<','<<t.median<<','<<t.worst<<','<<t.user<<','<<t.system; }
void Run(std::string_view name, bool smoke)
{
    size_t n{1},g{1},v2{0};
    if(name=="consolidate53")n=53;
    else if(name=="consolidate100"||name=="early_invalid")n=100;
    else if(name=="independent100")n=g=100;
    else if(name=="max_groups"||name=="late_invalid")n=g=256;
    else if(name=="max_members")n=1024;
    else if(name=="max_inputs"){n=4096;g=4;}
    else if(name=="mixed"){n=53;v2=2;}
    else Require(name=="small","unknown integration workload");
    auto f=mercaweave::test::MixedFixture(n,g,v2);
    auto view=View::Create(f.genesis);
    Ledger planning;
    std::vector<CMutableTransaction> txs;
    for(uint32_t serial=1;;++serial) {
        fixture::UniqueAndSign(f,serial,n);
        auto r=Capture(f.tx,f.spent,f.genesis,FLAGS);
        if(planning.Reserve(Estimate(*r))!=BudgetError::None)break;
        fixture::Fund(view,f);txs.push_back(f.tx);
        if(smoke && txs.size()==2)break;
    }
    const bool valid=name!="early_invalid"&&name!="late_invalid";
    if(name=="early_invalid")txs.front().vin[0].scriptWitness.stack.pop_back();
    if(name=="late_invalid")txs.back().vin[g-1].scriptWitness.stack[0][100]^=1;
    std::vector<RequestPtr> requests;
    uint64_t full_fee_bytes{0}, submitted_items{0}, submitted_groups{0};
    for(const auto& tx:txs) {
        auto r=Capture(tx,f.spent,f.genesis,FLAGS);auto c=Estimate(*r);
        full_fee_bytes=Add(full_fee_bytes,c.tx_bytes);submitted_items=Add(submitted_items,c.witness_items);submitted_groups=Add(submitted_groups,c.groups);
        requests.push_back(std::move(r));
    }
    const auto sample=name=="late_invalid"?requests.back():requests.front();
    Preparation alone;
    auto engine_time=Time([&]{alone=research::Prepare(sample);Require(bool(alone.context)==valid,"engine sample verdict");});
    auto prepared=Engine::Prepare(view,sample->tx,f.genesis,FLAGS);
    Timing dispatch;
    if(valid) {
        auto jobs=Engine::Bind(view,sample->tx,f.genesis,FLAGS,200,prepared.proof);
        Require(jobs.jobs.size()==sample->tx.vin.size(),"bound script workload");
        dispatch=Time([&]{for(const auto& job:jobs.jobs)Require(job.Run().valid,"real Script dispatch");});
    }
    TransactionResult transaction;
    auto transaction_time=Time([&]{transaction=Engine::Validate(view,sample->tx,f.genesis,FLAGS);Require(transaction.valid==valid,"integrated sample verdict");});
    Workload baseline;
    auto baseline_time=Time([&]{baseline=VerifyWorkload(requests);Require(baseline.authorized==valid,"WO03 block comparator verdict");});
    BlockResult candidate;
    auto candidate_run=[&]{candidate=Engine::Candidate(view,txs,f.genesis,FLAGS);Require(candidate.valid==valid&&!candidate.committed,"integrated candidate verdict");};
    const auto candidate_time=Time(candidate_run);
    allocation::calls=allocation::bytes=allocation::largest=0;allocation::active=true;
    try{candidate_run();}catch(...){allocation::active=false;throw;}allocation::active=false;
    uint64_t groups{0},owners{0},native_prepare{0},native_dispatch{0},scripts{0},hooks{0},body{0},manifest{0};
    for(const auto& tx:candidate.transactions) {
        groups=Add(groups,tx.preparation.work.verification_calls);owners=Add(owners,tx.preparation.work.ownership_checks);
        native_prepare=Add(native_prepare,tx.preparation.native_prepare_calls);
        body=Add(body,tx.preparation.work.body_hash_passes);manifest=Add(manifest,tx.preparation.work.manifest_hash_passes);
        Require(!tx.preparation.proof,"staging proof escaped candidate");
        for(const auto& script:tx.scripts){native_dispatch=Add(native_dispatch,script.native_calls);scripts=Add(scripts,script.script_calls);hooks=Add(hooks,script.hook_calls);}
    }
    if(valid)Require(groups==Multiply(g,txs.size())&&owners==Multiply(n,txs.size())&&native_prepare==Multiply(v2,txs.size())&&native_dispatch==native_prepare,"integrated full workload counters");
    if(name=="early_invalid")Require(candidate.charged.transactions==1&&groups==0,"early rejection is charged and stops candidate");
    if(name=="late_invalid")Require(candidate.charged.transactions==txs.size()&&groups==Multiply(g,txs.size()),"invalid final transaction incurs all group checks");
    const auto block=SerializeWorkload(requests);
    struct rusage usage{};getrusage(RUSAGE_SELF,&usage);
    auto policy=Engine::Prepare(view,sample->tx,f.genesis,FLAGS,200,Policy::CurrentPolicy);
    std::cout<<"scenario,smoke,v3_per_tx,groups_per_tx,native_per_tx,submitted_txs,submitted_inputs,submitted_groups,full_block_bytes,full_legacy_weight,tx_full_fee_bytes,submitted_witness_items,valid,stage,processed_txs,charged_txs,charged_group_cost,charged_existing_cost,charged_prefix_bytes,group_verifies,ownership_checks,native_prepare_verifies,native_dispatch_verifies,script_calls,hook_calls,body_passes,manifest_passes,ownership_pk_bytes,current_policy_pass,engine_tx_ms,engine_tx_worst_ms,engine_tx_user_ms,engine_tx_system_ms,dispatch_ms,dispatch_worst_ms,dispatch_user_ms,dispatch_system_ms,integrated_tx_ms,integrated_tx_worst_ms,integrated_tx_user_ms,integrated_tx_system_ms,wo03_block_ms,wo03_block_worst_ms,wo03_block_user_ms,wo03_block_system_ms,integrated_block_ms,integrated_block_worst_ms,integrated_block_user_ms,integrated_block_system_ms,peak_rss_kib,cpp_new_calls,cpp_requested_bytes,cpp_largest_request\n";
    std::cout<<name<<','<<smoke<<','<<n<<','<<g<<','<<v2<<','<<txs.size()<<','<<Multiply(n+v2,txs.size())<<','<<submitted_groups
        <<','<<GetSerializeSize(TX_WITH_WITNESS(block))<<','<<GetBlockWeight(block)<<','<<full_fee_bytes<<','<<submitted_items
        <<','<<candidate.valid<<','<<static_cast<int>(candidate.failure.stage)<<','<<candidate.transactions.size()<<','<<candidate.charged.transactions
        <<','<<candidate.charged.group_cost<<','<<candidate.charged.existing_cost<<','<<candidate.bytes<<','<<groups<<','<<owners<<','<<native_prepare<<','<<native_dispatch
        <<','<<scripts<<','<<hooks<<','<<body<<','<<manifest<<','<<Multiply(1952,owners)<<','<<bool(policy.proof);
    Print(engine_time);Print(dispatch);Print(transaction_time);Print(baseline_time);Print(candidate_time);
    std::cout<<','<<usage.ru_maxrss<<','<<allocation::calls<<','<<allocation::bytes<<','<<allocation::largest<<'\n';
}
}
int main(int argc,char** argv)
{
    try {
        Require(argc==2||(argc==3&&std::string_view(argv[2])=="--smoke"),"usage: integration_bench SCENARIO [--smoke]");
        std::cout<<std::fixed<<std::setprecision(6);Run(argv[1],argc==3);
    }catch(const std::exception& e){allocation::active=false;std::cerr<<"FAIL integration benchmark: "<<e.what()<<'\n';return 1;}
}
