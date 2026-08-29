// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <crypto/mercahash.h>
#include <primitives/block.h>
#include <streams.h>
#include <uint256.h>
#include <util/check.h>

#include <array>
#include <cassert>
#include <vector>

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
            // Preserve the established DGW averaging recurrence exactly:
            //
            //   floor((average * n + target) / (n + 1))
            //
            // without forming the potentially overflowing average * n
            // intermediate.
            //
            // If target >= average:
            //
            //   result = average
            //          + floor((target - average) / (n + 1))
            //
            // If target < average:
            //
            //   result = average
            //          - ceil((average - target) / (n + 1))
            //
            // Both forms stay within the original target range and therefore
            // avoid 256-bit multiplication overflow.
            const uint32_t divisor{
                static_cast<uint32_t>(nCountBlocks + 1)};

            if (bnTarget >= bnPastTargetAvg) {
                arith_uint256 delta{
                    bnTarget - bnPastTargetAvg};

                delta /= divisor;
                bnPastTargetAvg += delta;
            } else {
                arith_uint256 delta{
                    bnPastTargetAvg - bnTarget};

                arith_uint256 quotient{delta};
                quotient /= divisor;

                arith_uint256 remainder_base{quotient};
                remainder_base *= divisor;

                const arith_uint256 remainder{
                    delta - remainder_base};

                if (remainder != 0) {
                    quotient += 1;
                }

                bnPastTargetAvg -= quotient;
            }
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

    // Calculate:
    //
    //     floor(bnPastTargetAvg * nActualTimespan /
    //           params.nDGWTargetTimespan)
    //
    // without overflowing the fixed-width 256-bit arithmetic.
    //
    // Decompose:
    //
    //     target = quotient * target_timespan + remainder
    //
    // so the exact scaled result becomes:
    //
    //     quotient * actual_timespan
    //       + floor(remainder * actual_timespan / target_timespan)
    //
    // The remainder term is always small. Before multiplying the quotient,
    // compare against powLimit / actual_timespan so a result that would
    // exceed the consensus ceiling saturates without overflowing.
    const uint32_t target_timespan{
        static_cast<uint32_t>(params.nDGWTargetTimespan)};
    const uint32_t actual_timespan{
        static_cast<uint32_t>(nActualTimespan)};

    arith_uint256 quotient{bnPastTargetAvg};
    quotient /= target_timespan;

    arith_uint256 remainder_base{quotient};
    remainder_base *= target_timespan;

    arith_uint256 remainder{
        bnPastTargetAvg - remainder_base};

    arith_uint256 tail{remainder};
    tail *= actual_timespan;
    tail /= target_timespan;

    arith_uint256 quotient_limit{bnPowLimit};
    quotient_limit /= actual_timespan;

    if (quotient > quotient_limit) {
        return bnPowLimit.GetCompact();
    }

    quotient *= actual_timespan;

    if (tail > bnPowLimit - quotient) {
        return bnPowLimit.GetCompact();
    }

    arith_uint256 bnNew{quotient + tail};

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

uint256 GetPoWHash(
    const CBlockHeader& header,
    std::span<unsigned char> scratchpad)
{
    std::vector<unsigned char> serialized_header;
    serialized_header.reserve(mercahash::HEADER_SIZE);

    VectorWriter{
        serialized_header,
        0,
        header,
    };

    assert(
        serialized_header.size() ==
        mercahash::HEADER_SIZE);

    std::array<unsigned char, mercahash::OUTPUT_SIZE>
        output{};

    mercahash::HashV1(
        serialized_header,
        scratchpad,
        output);

    // Preserve the raw MercaHash bytes exactly. uint256's span
    // constructor copies the bytes without reversing them.
    return uint256{
        std::span<const unsigned char>{output}
    };
}

uint256 PoWHashContext::GetHash(
    const CBlockHeader& header)
{
    // Allocate the memory-hard scratchpad only when real MercaHash work is
    // actually requested. This keeps construction cheap for fuzz targets and
    // code paths that never perform proof-of-work hashing.
    if (m_scratchpad.empty()) {
        m_scratchpad.resize(mercahash::SCRATCHPAD_BYTES);
    }

    assert(
        m_scratchpad.size() ==
        mercahash::SCRATCHPAD_BYTES);

    return GetPoWHash(
        header,
        m_scratchpad);
}

bool CheckProofOfWork(
    const CBlockHeader& header,
    const Consensus::Params& params,
    PoWHashContext& context)
{
    // Preserve Bitcoin Core's cheap deterministic fuzz behavior, but apply it
    // before MercaHash so fuzzing never allocates the 128 MiB scratchpad.
    if (EnableFuzzDeterminism()) {
        const uint256 identity_hash{header.GetHash()};
        return (identity_hash.data()[31] & 0x80) == 0;
    }

    return CheckProofOfWorkImpl(
        context.GetHash(header),
        header.nBits,
        params);
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
