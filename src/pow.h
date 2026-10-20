// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_POW_H
#define BITCOIN_POW_H

#include <consensus/params.h>
#include <crypto/mercahash.h>

#include <cstdint>
#include <span>
#include <vector>

class CBlockHeader;
class CBlockIndex;
class uint256;
class arith_uint256;

/**
 * Convert nBits value to target.
 *
 * @param[in] nBits     compact representation of the target
 * @param[in] pow_limit PoW limit (consensus parameter)
 *
 * @return              the proof-of-work target or nullopt if the nBits value
 *                      is invalid (due to overflow or exceeding pow_limit)
 */
std::optional<arith_uint256> DeriveTarget(unsigned int nBits, uint256 pow_limit);

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params&);

/**
 * Compute MercaHash-v1 over the canonical serialized 80-byte block header.
 *
 * The caller owns the 128 MiB scratchpad and may reuse it between calls.
 * CBlockHeader::GetHash() remains the SHA256d block-identity hash.
 */
uint256 GetPoWHash(
    const CBlockHeader& header,
    std::span<unsigned char> scratchpad,
    mercahash::Version version = mercahash::Version::V1);

/**
 * Reusable, version-aware MercaHash proof-of-work context.
 *
 * Lazily owns one correctly sized private scratchpad so repeated
 * header hashes do not allocate scratchpad memory for every invocation.
 *
 * A context is not intended for concurrent use by multiple threads.
 */
class PoWHashContext
{
public:
    PoWHashContext() = default;

    PoWHashContext(const PoWHashContext&) = delete;
    PoWHashContext& operator=(const PoWHashContext&) = delete;

    PoWHashContext(PoWHashContext&&) noexcept = default;
    PoWHashContext& operator=(PoWHashContext&&) noexcept = default;

    // The one-argument overload retains its permanent V1 contract.
    uint256 GetHash(const CBlockHeader& header, mercahash::Version version = mercahash::Version::V1);
    std::size_t ScratchpadSize() const { return m_scratchpad.size(); }

    /** Number of completed memory-hard hashes (caller must serialize access). */
    uint64_t HashesComputed() const { return m_hashes_computed; }

private:
    std::vector<unsigned char> m_scratchpad;
    uint64_t m_hashes_computed{0};
};

/**
 * Check the actual Mercatura proof of work for a block header.
 *
 * Normal validation selects from verified candidate height. Unknown height fails
 * closed when V2 activation is configured. With activation disabled, V1 is fixed.
 * Fuzz determinism is handled before MercaHash is invoked, so fuzz targets do
 * not allocate or process the 128 MiB scratchpad.
 */
bool CheckProofOfWork(
    const CBlockHeader& header,
    const Consensus::Params&,
    PoWHashContext& context,
    std::optional<int64_t> candidate_height = std::nullopt);

/** Only call with a height derived from the candidate's verified ancestry. */
mercahash::Version GetPoWVersion(const Consensus::Params& params, int64_t candidate_height);

/**
 * Generic target comparison for an already-computed hash.
 *
 * This overload remains available for target arithmetic tests and callers
 * which already possess the hash value to compare.
 */
bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params&);
bool CheckProofOfWorkImpl(uint256 hash, unsigned int nBits, const Consensus::Params&);


#endif // BITCOIN_POW_H
