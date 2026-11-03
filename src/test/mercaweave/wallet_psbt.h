// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef MERCATURA_MERCAWEAVE_WALLET_PSBT_H
#define MERCATURA_MERCAWEAVE_WALLET_PSBT_H
#include "research_wallet.h"
#include <psbt.h>
namespace mercaweave::wallet_research {
inline constexpr std::string_view PSBT_ID="MercaWeave"; // ten ASCII bytes
PSBTProprietary Field(uint64_t subtype,std::span<const unsigned char>);
const Bytes* GetField(const PSBTInput&,uint64_t);
void PutField(PSBTInput&,uint64_t,std::span<const unsigned char>);
struct Inspection {
    CMutableTransaction tx; std::vector<CTxOut> spent; Mapping mapping; Hashes hashes;
    std::vector<bool> complete; uint64_t epoch{0}; size_t verified_groups{0},verified_native{0};
};
struct Progress { size_t group_signatures{0},native_signatures{0},complete_inputs{0}; bool complete{false}; };
class GroupPSBT final {
public:
    // Complete public metadata is necessary; private keys are not. Native PSBT
    // containers/parser/Merge remain the storage/transport implementation.
    static PartiallySignedTransaction Create(const CMutableTransaction&,std::span<const CTxOut>,std::span<const Address>);
    static Inspection Inspect(const PartiallySignedTransaction&,const integration::ViewPtr&,const B32&);
    static Progress Sign(PartiallySignedTransaction&,const Wallet&,const integration::ViewPtr&,const RandomSource& = SecureRandom);
    // Fail closed on different values for one key, including distinct valid
    // randomized signatures. This is an explicit local combine policy.
    static PartiallySignedTransaction Combine(const PartiallySignedTransaction&,const PartiallySignedTransaction&,const integration::ViewPtr&,const B32&);
    static Progress FinalizeAvailable(PartiallySignedTransaction&,const integration::ViewPtr&,const B32&);
    static CMutableTransaction Extract(const PartiallySignedTransaction&,const integration::ViewPtr&,const B32&);
    static Bytes Encode(const PartiallySignedTransaction&);
    static PartiallySignedTransaction Decode(std::span<const unsigned char>);
};
}
#endif
