// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <kernel/chainparams.h>
#include <consensus/consensus.h>
#include <policy/policy.h>
#include <util/translation.h>
#include <iostream>
const TranslateFn G_TRANSLATION_FUN{nullptr};
int main()
{
    const auto print=[](const char* name,const std::unique_ptr<const CChainParams>& params){
        for(const int height:{0,200,7999,8000,1051200,8409600}){
            const auto& c=params->GetConsensus();
            std::cout<<"{\"network\":\""<<name<<"\",\"height\":"<<height<<",\"genesis\":\""<<c.hashGenesisBlock.ToString()<<"\",\"capacity_bytes\":"<<GetMaxBlockCapacityBytes(c,height)<<",\"standard_weight\":"<<MAX_STANDARD_TX_WEIGHT<<",\"sigops_limit\":"<<MAX_BLOCK_SIGOPS_COST<<",\"coinbase_maturity\":"<<COINBASE_MATURITY<<"}\n";
        }
    };
    print("main",CChainParams::Main());print("test",CChainParams::TestNet());print("testnet4",CChainParams::TestNet4());print("signet",CChainParams::SigNet({}));print("regtest",CChainParams::RegTest({}));
}
