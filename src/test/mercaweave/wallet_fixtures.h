// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_TEST_MERCAWEAVE_WALLET_FIXTURES_H
#define BITCOIN_TEST_MERCAWEAVE_WALLET_FIXTURES_H
#include <test/mercaweave/wallet_psbt.h>
#include <crypto/common.h>
namespace mercaweave::wallet_research::test {
inline B32 Genesis(){B32 g{};g[0]=0x39;return g;}
inline std::unique_ptr<Wallet> Owner(uint8_t fill=0x42){auto w=Wallet::Create(wallet::CKeyingMaterial(32,fill),Genesis(),SecureString{"public research fixture"});if(!w->Unlock(SecureString{"public research fixture"}))throw std::runtime_error("fixture unlock");return w;}
inline COutPoint Point(uint32_t n){B32 id{};WriteLE32(id.data(),n+1);id[31]=0x55;return {Txid::FromUint256(uint256{std::span<const unsigned char>{id}}),0};}
inline void Add(CMutableTransaction& tx,std::vector<CTxOut>& spent,std::vector<Address>& addresses,const integration::ViewPtr& view,const Address& a,uint32_t n,CAmount value=10000,bool coinbase=false)
{auto point=Point(n);tx.vin.emplace_back(point);spent.emplace_back(value,a.script);addresses.push_back(a);view->Insert(point,Coin{CTxOut(spent.back()),1,coinbase});}
inline RandomSource ZeroFixture(){return [](B32& r){r.fill(0);return true;};}
}
#endif // BITCOIN_TEST_MERCAWEAVE_WALLET_FIXTURES_H
