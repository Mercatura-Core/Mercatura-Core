// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
// In-process only. No node, networking, activation, CWallet or real funding.
#include <test/mercaweave/decimal_argument.h>
#include <test/mercaweave/integration_fixtures.h>
#include <crypto/sha256.h>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sys/resource.h>
using namespace mercaweave;
using namespace mercaweave::test;
using namespace mercaweave::integration;
namespace fixture=mercaweave::integration::test;
namespace {
using Clock=std::chrono::steady_clock;
double Ms(Clock::time_point t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
void Check(bool v){if(!v)throw std::runtime_error("research simulation invariant");}
void Run(size_t inputs,size_t groups,size_t natives,uint32_t seed)
{
    const auto begin=Clock::now();auto f=natives?MixedFixture(inputs,groups,natives):Make(inputs,groups);
    f.tx.nLockTime=seed;double construct=Ms(begin);
    auto start=Clock::now();
    if(natives){NativeSession native{f.tx,f.spent,f.genesis};for(size_t i=inputs;i<f.tx.vin.size();++i){auto sig=SignDigest(native.Digest(i),*f.keys[0],B32{},"Mercatura/PQAuth/v1");f.tx.vin[i].scriptWitness.stack[0].assign(sig.begin(),sig.end());}}
    SignFixture(f);double sign=Ms(start);auto view=fixture::Funded(f);
    start=Clock::now();auto valid=Engine::Validate(view,f.tx,f.genesis,STANDARD_SCRIPT_VERIFY_FLAGS);double verify=Ms(start);Check(valid.valid);
    size_t calls=valid.preparation.work.verification_calls+valid.preparation.native_prepare_calls;
    for(const auto& result:valid.scripts)calls+=result.native_calls;
    Check(calls==groups+2*natives);
    auto policy=Engine::AcquireInputs(view,f.tx,f.genesis,STANDARD_SCRIPT_VERIFY_FLAGS,200,Policy::CurrentPolicy);Check(!policy.acquired);
    auto bad=f.tx;bad.vin[groups-1].scriptWitness.stack[0][100]^=1;start=Clock::now();auto rejected=Engine::Validate(view,bad,f.genesis,STANDARD_SCRIPT_VERIFY_FLAGS);double invalid=Ms(start);Check(!rejected.valid&&rejected.failure.stage==Stage::Group);
    auto sizes=Measure(f.tx);auto wire=Wire(f.tx);B32 digest;CSHA256{}.Write(wire.data(),wire.size()).Finalize(digest.data());struct rusage r{};getrusage(RUSAGE_SELF,&r);
    std::cout<<inputs<<','<<groups<<','<<natives<<','<<groups+natives<<','<<calls<<','<<sizes.total<<','<<sizes.weight<<','<<seed<<",true,false,false,"<<construct<<','<<sign<<','<<verify<<','<<invalid<<','<<r.ru_maxrss<<','<<Hex(digest)<<'\n';
}
}
int main(int argc,char** argv)
{
    try{Check(argc<=2);const auto parsed=argc>1?mercaweave::test::DecimalArgument(argv[1]):7;Check(parsed<=0xffffffffULL);uint32_t seed=parsed;
        std::cout<<std::fixed<<std::setprecision(3)<<"v3_inputs,groups,native_inputs,witness_signatures,actual_verifications,bytes,weight,seed,research_valid,standard_relay,real_node_v3_execution,construction_ms,signing_ms,verification_ms,late_invalid_ms,maxrss_kib,wire_sha256\n";
        for(size_t n:{1,10,53,100,1000,1024})Run(n,1,0,seed);
        Run(100,10,0,seed);Run(100,100,0,seed);Run(53,1,2,seed);Run(256,256,0,seed);Run(4096,4,0,seed);
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
