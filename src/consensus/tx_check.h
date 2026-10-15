// Copyright (c) 2017-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_TX_CHECK_H
#define BITCOIN_CONSENSUS_TX_CHECK_H

#include <cstdint>

/**
 * Context-independent transaction checking code that can be called outside the
 * bitcoin server and doesn't depend on chain or mempool state. Transaction
 * verification code that does call server functions or depend on server state
 * belongs in tx_verify.h/cpp instead.
 */

class CTransaction;
class TxValidationState;
class CBlock;
class BlockValidationState;
namespace Consensus { struct Params; }

bool CheckTransaction(const CTransaction& tx, TxValidationState& state);
/** Witness-dependent checks, kept separate from stripped transaction checks. */
bool CheckTransactionWitnessResources(const CTransaction& tx, TxValidationState& state);
uint64_t GetTransactionWitnessItems(const CTransaction& tx);
/** Caller must authenticate the witness commitment before permanently rejecting a header. */
bool CheckBlockWitnessResources(const CBlock& block, BlockValidationState& state,
                                const Consensus::Params& params, int64_t height);

#endif // BITCOIN_CONSENSUS_TX_CHECK_H
