// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_TEST_MERCAWEAVE_WALLET_FUNDING_H
#define BITCOIN_TEST_MERCAWEAVE_WALLET_FUNDING_H
#include <test/mercaweave/wallet_psbt.h>
namespace mercaweave::wallet_research {
enum class FundingMode { Automatic, StandardPQ, MercaWeave };
struct FundingCoin {COutPoint point;int depth{10};bool safe{true};size_t ancestors{0},cluster_count{1};};
struct FeePolicy {CAmount per_k{1},minimum{1},maximum{MAX_MONEY};};
struct Funded {
    PartiallySignedTransaction psbt;CMutableTransaction estimate;Sizes sizes{};
    CAmount fee{0};size_t groups{0},native_inputs{0},selected{0},selection_attempts{0};
    bool current_policy{false};std::string policy_reason;
};
class Funding final {
public:
    static CAmount Fee(size_t bytes,const FeePolicy&);
    // Exact selection is also the sweep/batch assembly path; all values/scripts
    // come from View, ownership from verified wallet records. No arbitrary coins.
    static Funded Exact(const Wallet&,const integration::ViewPtr&,std::span<const FundingCoin>,std::span<const CTxOut>,const Address* change,FundingMode=FundingMode::Automatic,FeePolicy={},int spend_height=200);
    static Funded Fund(const Wallet&,const integration::ViewPtr&,std::span<const FundingCoin>,std::span<const CTxOut>,const Address& change,FundingMode=FundingMode::Automatic,FeePolicy={},int spend_height=200);
    // Spend the requested eligible set, partitioned under existing standard
    // weight/resource limits. Each output receives its batch total minus fee.
    static std::vector<Funded> Sweep(const Wallet&,const integration::ViewPtr&,std::span<const FundingCoin>,const CScript& destination,FundingMode=FundingMode::Automatic,FeePolicy={},int spend_height=200);
};
}
#endif // BITCOIN_TEST_MERCAWEAVE_WALLET_FUNDING_H
