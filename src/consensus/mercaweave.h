// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_CONSENSUS_MERCAWEAVE_H
#define BITCOIN_CONSENSUS_MERCAWEAVE_H

#include <consensus/params.h>

namespace Consensus {
/** The height is the block being checked, never an untrusted transaction field. */
inline bool MercaWeaveActive(const Params& params, int height)
{
    return height >= 0 && params.nMercaWeaveHeight && *params.nMercaWeaveHeight >= 0 &&
           height >= *params.nMercaWeaveHeight;
}
} // namespace Consensus

#endif // BITCOIN_CONSENSUS_MERCAWEAVE_H
