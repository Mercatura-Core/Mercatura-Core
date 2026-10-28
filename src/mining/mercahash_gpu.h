// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
// Exact staged, explicitly versioned MercaHash device arithmetic. Also compiled as C++ for parity tests.
#ifndef BITCOIN_MINING_MERCAHASH_GPU_H
#define BITCOIN_MINING_MERCAHASH_GPU_H
#undef MH_GLOBAL
#undef MH_CONSTANT
#undef MH_INLINE
#undef MH_KERNEL
#undef MH_ID
#undef MH_LANE
#undef MH_GROUP_SYNC
#ifdef MH_OPENCL
typedef ulong mh_u64;
#define MH_GLOBAL __global
#define MH_CONSTANT __constant
#define MH_INLINE static inline
#ifdef MH_AMD_SPECIALIZED
#define MH_KERNEL static inline
#else
#define MH_KERNEL __kernel
#endif
#define MH_ID ((unsigned)get_group_id(0))
#define MH_LANE ((unsigned)get_local_id(0))
#define MH_GROUP_SYNC() barrier(CLK_GLOBAL_MEM_FENCE)
#elif defined(MH_CUDA)
typedef unsigned long long mh_u64;
#define MH_GLOBAL
#define MH_CONSTANT __device__ __constant__
#define MH_INLINE static __device__ __forceinline__
#define MH_KERNEL extern "C" __global__
#define MH_ID ((unsigned)blockIdx.x)
#define MH_LANE ((unsigned)threadIdx.x)
#define MH_GROUP_SYNC() __syncwarp()
#else
#include <cstdint>
typedef unsigned long long mh_u64;
#define MH_GLOBAL
#define MH_CONSTANT static const
#define MH_INLINE static inline
#define MH_KERNEL static inline
#define MH_ID mh_test_id
#define MH_LANE 0
#define MH_GROUP_SYNC() ((void)0)
static unsigned mh_test_id;
#endif
// Each compiled instance has frozen geometry/domains. WorkParams never enter
// device code. The host allocates and launches using the same selected version.
#ifndef MH_VERSION
#define MH_VERSION 1
#endif
#undef MH_LINE_COUNT
#undef MH_LINE_MASK
#undef MH_BIND_BLOCK_COUNT
#undef MH_STATE_DOMAIN
#undef MH_CHECKPOINT_DOMAIN
#undef MH_FINAL_DOMAIN
#if MH_VERSION == 1
#define MH_LINE_COUNT 2097152U
#define MH_LINE_MASK 0x1fffffULL
#define MH_BIND_BLOCK_COUNT 32768U
#define MH_STATE_DOMAIN "MercaHash-v1/state/0"
#define MH_CHECKPOINT_DOMAIN "MercaHash-v1/checkpoint"
#define MH_FINAL_DOMAIN "MercaHash-v1/final"
#elif MH_VERSION == 2
#define MH_LINE_COUNT 262144U
#define MH_LINE_MASK 0x3ffffULL
#define MH_BIND_BLOCK_COUNT 4096U
#define MH_STATE_DOMAIN "MercaHash-v2/state/0"
#define MH_CHECKPOINT_DOMAIN "MercaHash-v2/checkpoint"
#define MH_FINAL_DOMAIN "MercaHash-v2/final"
#else
#error Unsupported MercaHash device version
#endif
#define MH_LINE_WORDS 8
#define MH_BIND_BLOCK_LINES 64
#define MH_BIND_ARX_INTERVAL 16
#define MH_SECONDARY_INTERVAL 16
#define MH_CROSS_LANE_INTERVAL 64
#define MH_CHECKPOINT_INTERVAL 1024
#define MH_FINAL_READS 256
MH_CONSTANT mh_u64 CONSTANTS[8] = {0x9E3779B97F4A7C15ULL, 0xBF58476D1CE4E5B9ULL, 0x94D049BB133111EBULL,
                                   0xD6E8FEB86659FD93ULL, 0xA0761D6478BD642FULL, 0xE7037ED1A0B428DBULL,
                                   0x8EBC6AF09C88C6E3ULL, 0x589965CC75374CC3ULL};
MH_CONSTANT unsigned ROTATION_1[8] = {13, 29, 43, 57, 17, 31, 47, 59};
MH_CONSTANT unsigned ROTATION_2[8] = {37, 53, 11, 23, 41, 7, 19, 61};
MH_CONSTANT unsigned CHECKPOINT_ROTATION[8] = {7, 19, 31, 43, 11, 23, 37, 53};
MH_INLINE mh_u64 mh_rotl(mh_u64 value, unsigned shift)
{
    return shift ? (value << shift) | (value >> (64 - shift)) : value;
}
MH_INLINE mh_u64 mh_read64(const unsigned char *bytes)
{
    mh_u64 value = 0;
    for (unsigned i = 0; i < 8; ++i)
        value |= (mh_u64)bytes[i] << (8 * i);
    return value;
}
MH_INLINE void mh_write64(unsigned char *bytes, mh_u64 value)
{
    for (unsigned i = 0; i < 8; ++i)
        bytes[i] = (unsigned char)(value >> (8 * i));
}
MH_CONSTANT mh_u64 mh_rc[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808aULL, 0x8000000080008000ULL, 0x000000000000808bULL,
    0x0000000080000001ULL, 0x8000000080008081ULL, 0x8000000000008009ULL, 0x000000000000008aULL, 0x0000000000000088ULL,
    0x0000000080008009ULL, 0x000000008000000aULL, 0x000000008000808bULL, 0x800000000000008bULL, 0x8000000000008089ULL,
    0x8000000000008003ULL, 0x8000000000008002ULL, 0x8000000000000080ULL, 0x000000000000800aULL, 0x800000008000000aULL,
    0x8000000080008081ULL, 0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL};
MH_CONSTANT unsigned mh_rotation[25] = {0,  1,  62, 28, 27, 36, 44, 6,  55, 20, 3,  10, 43,
                                        25, 39, 41, 45, 15, 21, 8,  18, 2,  61, 56, 14};
MH_INLINE void mh_keccak(mh_u64 *a)
{
    for (unsigned round = 0; round < 24; ++round) {
        mh_u64 c[5], d[5], b[25];
        for (unsigned x = 0; x < 5; ++x)
            c[x] = a[x] ^ a[x + 5] ^ a[x + 10] ^ a[x + 15] ^ a[x + 20];
        for (unsigned x = 0; x < 5; ++x)
            d[x] = c[(x + 4) % 5] ^ mh_rotl(c[(x + 1) % 5], 1);
        for (unsigned y = 0; y < 5; ++y)
            for (unsigned x = 0; x < 5; ++x) {
                unsigned i = x + 5 * y;
                b[y + 5 * ((2 * x + 3 * y) % 5)] = mh_rotl(a[i] ^ d[x], mh_rotation[i]);
            }
        for (unsigned y = 0; y < 5; ++y)
            for (unsigned x = 0; x < 5; ++x)
                a[x + 5 * y] = b[x + 5 * y] ^ ((~b[(x + 1) % 5 + 5 * y]) & b[(x + 2) % 5 + 5 * y]);
        a[0] ^= mh_rc[round];
    }
}
MH_INLINE void mh_sha3(const unsigned char *input, unsigned length, unsigned char *output, unsigned size)
{
    mh_u64 a[25] = {0};
    unsigned rate = size == 64 ? 72 : 136;
    unsigned pos = 0;
    for (unsigned i = 0; i < length; ++i) {
        a[pos / 8] ^= (mh_u64)input[i] << (8 * (pos % 8));
        if (++pos == rate) {
            mh_keccak(a);
            pos = 0;
        }
    }
    a[pos / 8] ^= (mh_u64)6 << (8 * (pos % 8));
    a[(rate - 1) / 8] ^= (mh_u64)0x80 << 56;
    mh_keccak(a);
    for (unsigned i = 0; i < size; ++i)
        output[i] = (unsigned char)(a[i / 8] >> (8 * (i % 8)));
}
MH_INLINE void G(mh_u64 *pa, mh_u64 *pb, mh_u64 *pc, mh_u64 *pd)
{
    (*pa) += (*pb);
    (*pd) = mh_rotl((*pd) ^ (*pa), 32);

    (*pc) += (*pd);
    (*pb) = mh_rotl((*pb) ^ (*pc), 24);

    (*pa) += (*pb);
    (*pd) = mh_rotl((*pd) ^ (*pa), 16);

    (*pc) += (*pd);
    (*pb) = mh_rotl((*pb) ^ (*pc), 63);
}

MH_INLINE void MercaARXColumns(mh_u64 *state)
{
    G(&state[0], &state[4], &state[8], &state[12]);
    G(&state[1], &state[5], &state[9], &state[13]);
    G(&state[2], &state[6], &state[10], &state[14]);
    G(&state[3], &state[7], &state[11], &state[15]);
}

MH_INLINE void MercaARXDiagonals(mh_u64 *state)
{
    G(&state[0], &state[5], &state[10], &state[15]);
    G(&state[1], &state[6], &state[11], &state[12]);
    G(&state[2], &state[7], &state[8], &state[13]);
    G(&state[3], &state[4], &state[9], &state[14]);
}

MH_INLINE void MercaARXRound(mh_u64 *state)
{
    MercaARXColumns(state);
    MercaARXDiagonals(state);
}

typedef struct {
    mh_u64 v[8];
} Line;
MH_INLINE Line ReadLine(MH_GLOBAL mh_u64 *scratchpad, mh_u64 line_index, unsigned stride, unsigned id)
{
    (void)stride;
    Line result;
    for (unsigned j = 0; j < 8; ++j)
        result.v[j] = scratchpad[(mh_u64)id * MH_LINE_COUNT * 8 + line_index * 8 + j];
    return result;
}
MH_INLINE void WriteLine(MH_GLOBAL mh_u64 *scratchpad, mh_u64 line_index, Line line, unsigned stride, unsigned id)
{
    (void)stride;
#if (defined(MH_CUDA) || defined(MH_OPENCL)) && !defined(MH_AMD_SPECIALIZED)
    // Every lane must finish reading the old line before any lane mutates it.
    // The second barrier publishes the complete replacement to the next step.
    MH_GROUP_SYNC();
    // Eight distinct lanes cooperatively store one complete 64-byte line.
    // Every lane carries the same private state; no lane races another writer.
    const unsigned lane = MH_LANE;
    if (lane < 8)
        scratchpad[(mh_u64)id * MH_LINE_COUNT * 8 + line_index * 8 + lane] = line.v[lane];
    // A later data-dependent read may observe any word just written. This is
    // required even when SIMD lanes happen to execute in lockstep on a device.
    MH_GROUP_SYNC();
#else
    for (unsigned j = 0; j < 8; ++j)
        scratchpad[(mh_u64)id * MH_LINE_COUNT * 8 + line_index * 8 + j] = line.v[j];
#endif
}
MH_INLINE void FillScratchpad(mh_u64 *state, MH_GLOBAL mh_u64 *scratchpad, unsigned begin, unsigned end,
                              unsigned stride, unsigned id)
{

    if (begin == 0) {
        // Domain-separated transition from initial state into scratchpad fill.
        state[0] ^= 0x9E3779B97F4A7C15ULL;
        state[5] ^= 0xBF58476D1CE4E5B9ULL;
        state[10] ^= 0x94D049BB133111EBULL;
        state[15] ^= 0xD6E8FEB86659FD93ULL;
    }
    for (unsigned i = begin; i < end; ++i) {
        const mh_u64 x = (mh_u64)(i);

        state[0] += x;
        state[7] ^= mh_rotl(x, 17);
        state[8] += x ^ 0x9E3779B97F4A7C15ULL;
        state[15] ^= mh_rotl(x, 41);

        if ((i & 1) == 0)
            MercaARXColumns(state);
        else
            MercaARXDiagonals(state);

        Line line = {{0}};

        for (mh_u64 j = 0; j < MH_LINE_WORDS; ++j) {
            line.v[j] = state[j] ^ state[j + 8];
        }

        WriteLine(scratchpad, i, line, stride, id);

        // Feed the newly generated line back into the evolving state.
        state[0] += line.v[3];
        state[1] ^= mh_rotl(line.v[4], 11);
        state[2] += line.v[5];
        state[3] ^= mh_rotl(line.v[6], 23);

        state[4] += line.v[7];
        state[5] ^= mh_rotl(line.v[0], 37);
        state[6] += line.v[1];
        state[7] ^= mh_rotl(line.v[2], 53);

        state[8] ^= line.v[5];
        state[9] += mh_rotl(line.v[6], 7);
        state[10] ^= line.v[7];
        state[11] += mh_rotl(line.v[0], 19);

        state[12] ^= line.v[1];
        state[13] += mh_rotl(line.v[2], 31);
        state[14] ^= line.v[3];
        state[15] += mh_rotl(line.v[4], 47);
    }
}

MH_INLINE void BindScratchpad(mh_u64 *state, MH_GLOBAL mh_u64 *scratchpad, unsigned begin, unsigned end,
                              unsigned stride, unsigned id)
{

    const mh_u64 pass = 0;
    {
        if (begin == 0) {
            state[0] ^= pass + CONSTANTS[4];
            state[8] += (pass + 1) * CONSTANTS[5];
            MercaARXRound(state);
        }
        for (unsigned block = begin; block < end; ++block) {
            const mh_u64 base_line = block * MH_BIND_BLOCK_LINES;

            const Line head = ReadLine(scratchpad, base_line, stride, id);

            const mh_u64 block_u64 = (mh_u64)(block);

            const mh_u64 reference_seed = state[0] ^ mh_rotl(state[7], 19) ^ state[13] ^ head.v[0] ^
                                          mh_rotl(head.v[7], 31) ^ (block_u64 * CONSTANTS[1]) ^
                                          ((pass + 1) * CONSTANTS[6]);

            const mh_u64 previous_block = block == 0 ? MH_BIND_BLOCK_COUNT - 1 : block - 1;

            const mh_u64 reference_block = block == 0 ? MH_BIND_BLOCK_COUNT - 1 : (mh_u64)(reference_seed % block_u64);

            const mh_u64 reference_rotation = (mh_u64)((reference_seed >> 17) & (MH_BIND_BLOCK_LINES - 1));

            Line block_accumulator = {{0}};

            for (mh_u64 line = 0; line < MH_BIND_BLOCK_LINES; ++line) {
                const mh_u64 current_line = base_line + line;

                const mh_u64 previous_line = previous_block * MH_BIND_BLOCK_LINES +
                                             ((line + MH_BIND_BLOCK_LINES - 1) & (MH_BIND_BLOCK_LINES - 1));

                const mh_u64 reference_line =
                    reference_block * MH_BIND_BLOCK_LINES + ((line + reference_rotation) & (MH_BIND_BLOCK_LINES - 1));

                const Line current = ReadLine(scratchpad, current_line, stride, id);

                const Line previous = ReadLine(scratchpad, previous_line, stride, id);

                const Line reference = ReadLine(scratchpad, reference_line, stride, id);

                const mh_u64 line_tag = block_u64 ^ ((mh_u64)(line)*CONSTANTS[0]) ^ (pass * CONSTANTS[3]);

                Line bound = {{0}};

                for (mh_u64 j = 0; j < MH_LINE_WORDS; ++j) {
                    const mh_u64 x = current.v[j] + previous.v[(j + 1) & 7];

                    const mh_u64 y = reference.v[(j + 3) & 7] + state[(j + line) & 15];

                    bound.v[j] = x ^ mh_rotl(y, ROTATION_1[j]) ^ mh_rotl(line_tag, ROTATION_2[j]);
                }

                WriteLine(scratchpad, current_line, bound, stride, id);

                for (mh_u64 j = 0; j < MH_LINE_WORDS; ++j) {
                    block_accumulator.v[j] ^= bound.v[j] + mh_rotl(bound.v[(j + 1) & 7], ROTATION_2[j]);
                }
            }

            for (mh_u64 j = 0; j < MH_LINE_WORDS; ++j) {
                state[j] ^= block_accumulator.v[j] + reference_seed;

                state[j + 8] += mh_rotl(block_accumulator.v[(j + 3) & 7], ROTATION_1[j]) ^ block_u64;
            }

            if ((block + 1) % MH_BIND_ARX_INTERVAL == 0) {
                MercaARXRound(state);
            }
        }

        if (end == MH_BIND_BLOCK_COUNT)
            MercaARXRound(state);
    }
}

typedef struct {
    mh_u64 line_index;
    Line written;
} PrimaryResult;

MH_INLINE PrimaryResult PrimaryStep(mh_u64 *state, MH_GLOBAL mh_u64 *scratchpad, mh_u64 step, unsigned stride,
                                    unsigned id)
{
    const mh_u64 primary_seed =
        state[0] ^ mh_rotl(state[5], 17) ^ state[10] ^ mh_rotl(state[15], 41) ^ (step * CONSTANTS[0]);

    const mh_u64 primary_index = (mh_u64)(primary_seed & MH_LINE_MASK);

    const Line memory = ReadLine(scratchpad, primary_index, stride, id);

    for (mh_u64 j = 0; j < MH_LINE_WORDS; ++j) {
        const mh_u64 x = memory.v[j] ^ mh_rotl(state[j + 8], ROTATION_1[j]) ^ (step * CONSTANTS[j]);

        state[j] += x;
    }

    for (mh_u64 j = 0; j < MH_LINE_WORDS; ++j) {
        const mh_u64 y = state[j] + memory.v[(j + 3) & 7] + mh_rotl(state[(j + 5) & 7], ROTATION_2[j]);

        state[j + 8] = mh_rotl(state[j + 8] ^ y, ROTATION_1[(j + 3) & 7]);
    }

    Line written = {{0}};

    for (mh_u64 j = 0; j < MH_LINE_WORDS; ++j) {
        written.v[j] = memory.v[j] ^ state[j] ^ mh_rotl(state[j + 8], ROTATION_2[j]) ^ (step * CONSTANTS[(j + 1) & 7]);
    }

    WriteLine(scratchpad, primary_index, written, stride, id);

    state[0] ^= written.v[5];
    state[1] += mh_rotl(written.v[6], 9);
    state[2] ^= written.v[7];
    state[3] += mh_rotl(written.v[0], 21);

    state[4] ^= written.v[1];
    state[5] += mh_rotl(written.v[2], 33);
    state[6] ^= written.v[3];
    state[7] += mh_rotl(written.v[4], 45);

    state[8] += written.v[3];
    state[9] ^= mh_rotl(written.v[4], 15);
    state[10] += written.v[5];
    state[11] ^= mh_rotl(written.v[6], 27);

    state[12] += written.v[7];
    state[13] ^= mh_rotl(written.v[0], 39);
    state[14] += written.v[1];
    state[15] ^= mh_rotl(written.v[2], 51);

    PrimaryResult result = {primary_index, written};
    return result;
}

typedef struct {
    mh_u64 line_index;
    Line written;
    int forced_distinct;
} SecondaryResult;

MH_INLINE SecondaryResult SecondaryStep(mh_u64 *state, MH_GLOBAL mh_u64 *scratchpad, mh_u64 step, PrimaryResult primary,
                                        unsigned stride, unsigned id)
{
    const mh_u64 secondary_seed = state[2] ^ mh_rotl(state[7], 23) ^ state[9] ^ mh_rotl(state[14], 37) ^
                                  primary.written.v[step & 7] ^ (step * CONSTANTS[3]);

    mh_u64 secondary_index = (mh_u64)(secondary_seed & MH_LINE_MASK);

    const int forced_distinct = secondary_index == primary.line_index;

    if (forced_distinct) {
        secondary_index ^= 1U;
    }

    const Line memory = ReadLine(scratchpad, secondary_index, stride, id);

    for (mh_u64 j = 0; j < MH_LINE_WORDS; ++j) {
        const mh_u64 z = memory.v[j] + state[(j + 11) & 15] + (step ^ CONSTANTS[(j + 5) & 7]);

        state[j + 8] += mh_rotl(z, ROTATION_2[j]);
    }

    for (mh_u64 j = 0; j < MH_LINE_WORDS; ++j) {
        state[j] = mh_rotl(state[j] ^ memory.v[(j + 5) & 7] ^ state[j + 8], ROTATION_1[j]);
    }

    Line written = {{0}};

    for (mh_u64 j = 0; j < MH_LINE_WORDS; ++j) {
        written.v[j] =
            memory.v[j] + state[j] + mh_rotl(state[j + 8], ROTATION_1[(j + 2) & 7]) + (step * CONSTANTS[(j + 6) & 7]);
    }

    WriteLine(scratchpad, secondary_index, written, stride, id);

    state[0] += written.v[7];
    state[1] ^= mh_rotl(written.v[0], 13);
    state[2] += written.v[1];
    state[3] ^= mh_rotl(written.v[2], 25);

    state[4] += written.v[3];
    state[5] ^= mh_rotl(written.v[4], 35);
    state[6] += written.v[5];
    state[7] ^= mh_rotl(written.v[6], 49);

    state[8] ^= written.v[2];
    state[9] += mh_rotl(written.v[3], 17);
    state[10] ^= written.v[4];
    state[11] += mh_rotl(written.v[5], 29);

    state[12] ^= written.v[6];
    state[13] += mh_rotl(written.v[7], 43);
    state[14] ^= written.v[0];
    state[15] += mh_rotl(written.v[1], 55);

    SecondaryResult result = {secondary_index, written, forced_distinct};
    return result;
}

MH_INLINE void CrossLaneMix(mh_u64 *state, mh_u64 step)
{
    state[0] ^= step;
    state[8] += step * CONSTANTS[2];

    MercaARXRound(state);
    MercaARXRound(state);
}

MH_INLINE void Checkpoint(mh_u64 *state, mh_u64 step, PrimaryResult primary, SecondaryResult secondary)
{
    unsigned char bytes[303];
    const char domain[] = MH_CHECKPOINT_DOMAIN;
    unsigned pos = 0;
    for (unsigned i = 0; i < sizeof(domain) - 1; ++i)
        bytes[pos++] = (unsigned char)domain[i];
    mh_write64(bytes + pos, step);
    pos += 8;
    mh_write64(bytes + pos, primary.line_index);
    pos += 8;
    mh_write64(bytes + pos, secondary.line_index);
    pos += 8;
    for (unsigned i = 0; i < 16; ++i) {
        mh_write64(bytes + pos, state[i]);
        pos += 8;
    }
    for (unsigned i = 0; i < 8; ++i) {
        mh_write64(bytes + pos, primary.written.v[i]);
        pos += 8;
    }
    for (unsigned i = 0; i < 8; ++i) {
        mh_write64(bytes + pos, secondary.written.v[i]);
        pos += 8;
    }
    unsigned char digest[64];
    mh_sha3(bytes, pos, digest, 64);
    for (unsigned j = 0; j < 8; ++j) {
        state[j] ^= mh_read64(digest + j * 8);
        state[j + 8] += mh_rotl(mh_read64(digest + ((j + 3) & 7) * 8), CHECKPOINT_ROTATION[j]);
    }
    MercaARXRound(state);
}
MH_INLINE void MixScratchpad(mh_u64 *state, MH_GLOBAL mh_u64 *scratchpad, unsigned begin, unsigned end, unsigned stride,
                             unsigned id)
{
    for (mh_u64 step = begin; step <= end; ++step) {
        const PrimaryResult primary = PrimaryStep(state, scratchpad, step, stride, id);

        if (step % MH_SECONDARY_INTERVAL == 0) {
            const SecondaryResult secondary = SecondaryStep(state, scratchpad, step, primary, stride, id);

            if (step % MH_CROSS_LANE_INTERVAL == 0) {
                CrossLaneMix(state, step);
            }

            if (step % MH_CHECKPOINT_INTERVAL == 0) {
                Checkpoint(state, step, primary, secondary);
            }
        }
    }
}

MH_INLINE void FinalSample(mh_u64 *state, MH_GLOBAL mh_u64 *scratchpad, unsigned stride, unsigned id)
{
    for (mh_u64 sample = 0; sample < MH_FINAL_READS; ++sample) {
        const mh_u64 sample_seed =
            state[0] ^ mh_rotl(state[5], 17) ^ state[10] ^ mh_rotl(state[15], 41) ^ (sample * CONSTANTS[0]);

        const mh_u64 sample_index = (mh_u64)(sample_seed & MH_LINE_MASK);

        const Line memory = ReadLine(scratchpad, sample_index, stride, id);

        for (mh_u64 j = 0; j < MH_LINE_WORDS; ++j) {
            const mh_u64 x = memory.v[j] ^ mh_rotl(state[j + 8], ROTATION_1[j]) ^ (sample + CONSTANTS[j]);

            state[j] += x;
        }

        for (mh_u64 j = 0; j < MH_LINE_WORDS; ++j) {
            state[j + 8] = mh_rotl(state[j + 8] ^ state[j] ^ memory.v[(j + 3) & 7], ROTATION_2[j]);
        }

        state[0] ^= (mh_u64)(sample_index);
        state[8] += mh_rotl((mh_u64)(sample_index), 29);

        if ((sample + 1) % 16 == 0) {
            MercaARXRound(state);
        }
    }
}

// A bounded launch advances every independent context in the batch. State is
// replicated privately within its group; groups own disjoint scratchpads.
MH_KERNEL void mh_init(MH_GLOBAL const unsigned char *headers, MH_GLOBAL mh_u64 *states, unsigned count)
{
    unsigned id = MH_ID;
    if (id >= count || MH_LANE != 0)
        return;
    unsigned char bytes[100], digest[64];
    const char domain[] = MH_STATE_DOMAIN;
    for (unsigned i = 0; i < 20; ++i)
        bytes[i] = (unsigned char)domain[i];
    for (unsigned i = 0; i < 80; ++i)
        bytes[20 + i] = headers[id * 80 + i];
    for (unsigned state_half = 0; state_half < 2; ++state_half) {
        bytes[19] = (unsigned char)('0' + state_half);
        mh_sha3(bytes, 100, digest, 64);
        for (unsigned j = 0; j < 8; ++j)
            states[id * 16 + state_half * 8 + j] = mh_read64(digest + j * 8);
    }
}
MH_KERNEL void mh_stage(MH_GLOBAL const unsigned char *headers, MH_GLOBAL mh_u64 *states, MH_GLOBAL mh_u64 *scratchpad,
                        MH_GLOBAL unsigned char *outputs, unsigned count, unsigned stage, unsigned begin, unsigned end)
{
    unsigned id = MH_ID;
    if (id >= count || (stage == 3 && MH_LANE != 0))
        return;
    mh_u64 state[16];
    for (unsigned j = 0; j < 16; ++j)
        state[j] = states[id * 16 + j];
    if (stage == 0)
        FillScratchpad(state, scratchpad, begin, end, count, id);
    if (stage == 1)
        BindScratchpad(state, scratchpad, begin, end, count, id);
    if (stage == 2)
        MixScratchpad(state, scratchpad, begin, end, count, id);
    if (stage == 3) {
        FinalSample(state, scratchpad, count, id);
        unsigned char bytes[226], digest[32];
        const char domain[] = MH_FINAL_DOMAIN;
        unsigned pos = 0;
        for (unsigned j = 0; j < sizeof(domain) - 1; ++j)
            bytes[pos++] = (unsigned char)domain[j];
        for (unsigned j = 0; j < 80; ++j)
            bytes[pos++] = headers[id * 80 + j];
        for (unsigned j = 0; j < 16; ++j) {
            mh_write64(bytes + pos, state[j]);
            pos += 8;
        }
        mh_sha3(bytes, pos, digest, 32);
        for (unsigned j = 0; j < 32; ++j)
            outputs[id * 32 + j] = digest[j];
    }
    if (MH_LANE == 0)
        for (unsigned j = 0; j < 16; ++j)
            states[id * 16 + j] = state[j];
}
#endif // BITCOIN_MINING_MERCAHASH_GPU_H
