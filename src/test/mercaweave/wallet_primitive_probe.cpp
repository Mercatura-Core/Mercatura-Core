// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include "wallet_seed_bridge.h"
#include "context_fixtures.h"
#include <psbt.h>
#include <wallet/coinselection.h>
#include <util/translation.h>
#include <iostream>
const TranslateFn G_TRANSLATION_FUN{nullptr};
int main()
{
    wallet::CKeyingMaterial key(32,0x31), seed(32,0x42), recovered;
    wallet::MercaturaPQCryptedSeed encrypted;
    if(!wallet::ResearchEncryptPQSeed(key,seed,encrypted)||!wallet::ResearchDecryptPQSeed(key,encrypted,recovered)||seed!=recovered)return 1;
    auto f=mercaweave::test::MixedFixture(2,1,1);
    auto tx=f.tx;for(auto& in:tx.vin)in.scriptWitness.stack.clear();
    PartiallySignedTransaction psbt(tx);
    for(size_t i=0;i<f.spent.size();++i)psbt.inputs[i].witness_utxo=f.spent[i];
    auto bytes=mercaweave::SerializeBytes(psbt);
    SpanReader reader{bytes};PartiallySignedTransaction parsed;reader>>parsed;
    if(!reader.empty()||!parsed.Merge(psbt))return 1;
    wallet::OutputGroup group;
    group.Insert(std::make_shared<wallet::COutput>(f.tx.vin[0].prevout,f.spent[0],10,5342,true,true,0,false,CAmount{6},5465),0,1);
    std::vector<wallet::OutputGroup> pool{group};
    auto result=wallet::SelectCoinsBnB(pool,900,100,400000);
    if(!result||result->GetInputSet().size()!=1)return 1;
    std::cout<<"PASS native seed envelope, PSBT serialization/merge and coin selection\n";
}
