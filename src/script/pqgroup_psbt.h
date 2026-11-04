// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_SCRIPT_PQGROUP_PSBT_H
#define BITCOIN_SCRIPT_PQGROUP_PSBT_H
#include <psbt.h>
#include <script/pqgroup_context.h>

namespace mercaweave::psbt {
struct InputRecord { PublicKey pk; B32 nonce; uint8_t witness_version{3}; };
struct Inspection {
    CMutableTransaction tx;
    std::vector<CTxOut> spent;
    Mapping mapping;
    Hashes hashes;
    std::vector<bool> complete;
    size_t verified_groups{0}, verified_native{0};
};
PSBTProprietary Field(uint64_t, std::span<const unsigned char>);
const Bytes* Get(const PSBTInput&, uint64_t);
void Put(PSBTInput&, uint64_t, std::span<const unsigned char>);
PartiallySignedTransaction Create(const CMutableTransaction&, std::span<const CTxOut>,
                                std::span<const InputRecord>, const IsolatedExecution&);
// Spent outputs MUST come from a trustworthy chain/wallet source in vin order.
// witness_utxo and the complete previous transaction are compared to that source.
Inspection Inspect(const PartiallySignedTransaction&, std::span<const CTxOut>, const IsolatedExecution&);
bool VerifyNative(const CMutableTransaction&, const std::vector<CTxOut>&,
                  const B32&, uint32_t, const PrecomputedTransactionData&);
}

// Explicit capability overloads. Ordinary PSBT functions and RPCs do not call
// these, and no PSBT field can carry or reconstruct the capability.
bool FinalizePSBT(PartiallySignedTransaction&, const mercaweave::IsolatedExecution&,
                  std::span<const CTxOut>);
bool FinalizeAndExtractPSBT(PartiallySignedTransaction&, CMutableTransaction&,
                           const mercaweave::IsolatedExecution&, std::span<const CTxOut>);
bool CombinePSBTs(PartiallySignedTransaction&, const std::vector<PartiallySignedTransaction>&,
                  const mercaweave::IsolatedExecution&, std::span<const CTxOut>);
#endif // BITCOIN_SCRIPT_PQGROUP_PSBT_H
