// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_TEST_MERCAWEAVE_WALLET_SEED_BRIDGE_H
#define BITCOIN_TEST_MERCAWEAVE_WALLET_SEED_BRIDGE_H
#ifndef MERCATURA_MERCAWEAVE_WALLET_RESEARCH_TEST_ONLY
#error "Research wallet seed access is forbidden in normal builds"
#endif
#include <wallet/crypter.h>
#include <wallet/mercatura_pqwallet.h>
namespace wallet {
bool ResearchEncryptPQSeed(const CKeyingMaterial&, const CKeyingMaterial&, MercaturaPQCryptedSeed&);
bool ResearchDecryptPQSeed(const CKeyingMaterial&, const MercaturaPQCryptedSeed&, CKeyingMaterial&);
}
#endif // BITCOIN_TEST_MERCAWEAVE_WALLET_SEED_BRIDGE_H
