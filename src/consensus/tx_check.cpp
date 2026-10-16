// Copyright (c) 2017-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/tx_check.h>

#include <consensus/amount.h>
#include <primitives/transaction.h>
#include <consensus/validation.h>
#include <primitives/block.h>

#include <limits>

bool CheckTransactionWitnessResources(const CTransaction& tx, TxValidationState& state)
{
    for (const auto& input : tx.vin) {
        if (const char* reason = Consensus::WitnessStackViolation(input.scriptWitness.stack)) {
            return state.Invalid(TxValidationResult::TX_WITNESS_MUTATED, reason);
        }
    }
    return true;
}

uint64_t GetTransactionWitnessItems(const CTransaction& tx)
{
    uint64_t count{0};
    for (const auto& input : tx.vin) {
        const uint64_t size{input.scriptWitness.stack.size()};
        if (size > std::numeric_limits<uint64_t>::max() - count) return std::numeric_limits<uint64_t>::max();
        count += size;
    }
    return count;
}

bool CheckBlockWitnessResources(const CBlock& block, BlockValidationState& state,
                                const Consensus::Params& params, int64_t height)
{
    if (!Consensus::WitnessLimitsActive(params, height)) return true;
    uint64_t remaining{Consensus::GetMaxBlockWitnessItems(params, height)};
    for (const auto& tx : block.vtx) {
        TxValidationState tx_state;
        if (!CheckTransactionWitnessResources(*tx, tx_state)) {
            return state.Invalid(BlockValidationResult::BLOCK_CONSENSUS, tx_state.GetRejectReason());
        }
        for (const auto& input : tx->vin) {
            const uint64_t count{input.scriptWitness.stack.size()};
            if (count > remaining) return state.Invalid(BlockValidationResult::BLOCK_CONSENSUS, "bad-blk-witness-items");
            remaining -= count;
        }
    }
    return true;
}

bool CheckTransaction(const CTransaction& tx, TxValidationState& state)
{
    // Basic checks that don't depend on any context
    if (tx.vin.empty())
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-vin-empty");
    if (tx.vout.empty())
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-vout-empty");
    // Size limits (this doesn't take the witness into account, as that hasn't been checked for malleability)
    if (::GetSerializeSize(TX_NO_WITNESS(tx)) * WITNESS_SCALE_FACTOR > MAX_BLOCK_WEIGHT) {
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-oversize");
    }

    // Check for negative or overflow output values (see CVE-2010-5139)
    CAmount nValueOut = 0;
    for (const auto& txout : tx.vout)
    {
        if (txout.nValue < 0)
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-vout-negative");
        if (txout.nValue > MAX_MONEY)
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-vout-toolarge");
        nValueOut += txout.nValue;
        if (!MoneyRange(nValueOut))
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-txouttotal-toolarge");
    }

    // Check for duplicate inputs (see CVE-2018-17144)
    // While Consensus::CheckTxInputs does check if all inputs of a tx are available, and UpdateCoins marks all inputs
    // of a tx as spent, it does not check if the tx has duplicate inputs.
    // Failure to run this check will result in either a crash or an inflation bug, depending on the implementation of
    // the underlying coins database.
    std::set<COutPoint> vInOutPoints;
    for (const auto& txin : tx.vin) {
        if (!vInOutPoints.insert(txin.prevout).second)
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-inputs-duplicate");
    }

    if (tx.IsCoinBase())
    {
        if (tx.vin[0].scriptSig.size() < 2 || tx.vin[0].scriptSig.size() > 100)
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-cb-length");
    }
    else
    {
        for (const auto& txin : tx.vin)
            if (txin.prevout.IsNull())
                return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-prevout-null");
    }

    return true;
}
