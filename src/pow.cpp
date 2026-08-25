// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <primitives/block.h>
#include <uint256.h>
#include <util/check.h>

static unsigned int DarkGravityWave(const CBlockIndex* pindexLast, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    assert(params.nDGWPastBlocks > 0);
    assert(params.nDGWTargetTimespan > 0);
    assert(params.nDGWMinTimespan > 0);
    assert(params.nDGWMaxTimespan >= params.nDGWMinTimespan);

    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);

    // Keep the launch difficulty unchanged until a complete DGW window exists.
    // With a 24-block window, DGW first calculates the target for block 25.
    if (pindexLast->nHeight < params.nDGWPastBlocks) {
        return pindexLast->nBits;
    }

    const CBlockIndex* pindex = pindexLast;
    arith_uint256 bnPastTargetAvg;

    // Preserve the established DGWv3 averaging recurrence exactly.
    for (int64_t nCountBlocks = 1; nCountBlocks <= params.nDGWPastBlocks; ++nCountBlocks) {
        arith_uint256 bnTarget;
        bnTarget.SetCompact(pindex->nBits);

        if (nCountBlocks == 1) {
            bnPastTargetAvg = bnTarget;
        } else {
            bnPastTargetAvg =
                (bnPastTargetAvg * nCountBlocks + bnTarget) / (nCountBlocks + 1);
        }

        if (nCountBlocks != params.nDGWPastBlocks) {
            assert(pindex->pprev);
            pindex = pindex->pprev;
        }
    }

    int64_t nActualTimespan =
        pindexLast->GetBlockTime() - pindex->GetBlockTime();

    if (nActualTimespan < params.nDGWMinTimespan) {
        nActualTimespan = params.nDGWMinTimespan;
    }
    if (nActualTimespan > params.nDGWMaxTimespan) {
        nActualTimespan = params.nDGWMaxTimespan;
    }

    arith_uint256 bnNew{bnPastTargetAvg};
    bnNew *= nActualTimespan;
    bnNew /= params.nDGWTargetTimespan;

    if (bnNew > bnPowLimit) {
        bnNew = bnPowLimit;
    }

    return bnNew.GetCompact();
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);

    // Regtest and other no-retarget test configurations retain their current
    // difficulty so local block generation remains fast and predictable.
    if (params.fPowNoRetargeting) {
        return pindexLast->nBits;
    }

    const unsigned int nProofOfWorkLimit =
        UintToArith256(params.powLimit).GetCompact();

    // Test-chain minimum-difficulty exception: preserve Bitcoin Core's
    // proportional two-target-spacing delay rule. A normally timed block
    // immediately returns to the ordinary DGW calculation.
    if (params.fPowAllowMinDifficultyBlocks && pblock != nullptr &&
        pblock->GetBlockTime() >
            pindexLast->GetBlockTime() + params.nPowTargetSpacing * 2) {
        return nProofOfWorkLimit;
    }

    return DarkGravityWave(pindexLast, params);
}

// Bypasses the actual proof of work check during fuzz testing with a simplified validation checking whether
// the most significant bit of the last byte of the hash is set.
bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    if (EnableFuzzDeterminism()) return (hash.data()[31] & 0x80) == 0;
    return CheckProofOfWorkImpl(hash, nBits, params);
}

std::optional<arith_uint256> DeriveTarget(unsigned int nBits, const uint256 pow_limit)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(pow_limit))
        return {};

    return bnTarget;
}

bool CheckProofOfWorkImpl(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    auto bnTarget{DeriveTarget(nBits, params.powLimit)};
    if (!bnTarget) return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}
