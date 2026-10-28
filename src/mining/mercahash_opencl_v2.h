// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
// One group owns one private 16 MiB context. No subgroup/warp assumptions.
#ifndef BITCOIN_MINING_MERCAHASH_OPENCL_V2_H
#define BITCOIN_MINING_MERCAHASH_OPENCL_V2_H
#define MH_AMD_SPECIALIZED
#define MH_VERSION 2
#include <mining/mercahash_gpu.h>

__kernel void mh_v2_init(__global const unsigned char* headers, __global mh_u64* states, unsigned count)
{
    mh_init(headers, states, count);
}
__kernel void mh_v2_fill(__global const unsigned char* headers, __global mh_u64* states,
                       __global mh_u64* scratchpad, __global unsigned char* outputs,
                       unsigned count, unsigned begin, unsigned end)
{
    // Fill feedback is sequential. All stores are owned by this work-item;
    // other work-items return without participating in any barriers.
    if (MH_LANE == 0) mh_stage(headers, states, scratchpad, outputs, count, 0, begin, end);
}
__kernel void mh_v2_mix(__global const unsigned char* headers, __global mh_u64* states,
                      __global mh_u64* scratchpad, __global unsigned char* outputs,
                      unsigned count, unsigned begin, unsigned end)
{
    // Mixing has data-dependent read/modify/write dependencies. Keep a single
    // owner rather than duplicating private state and synchronizing line stores.
    if (MH_LANE == 0) mh_stage(headers, states, scratchpad, outputs, count, 2, begin, end);
}
__kernel void mh_v2_final(__global const unsigned char* headers, __global mh_u64* states,
                        __global mh_u64* scratchpad, __global unsigned char* outputs,
                        unsigned count, unsigned begin, unsigned end)
{
    if (MH_LANE == 0) mh_stage(headers, states, scratchpad, outputs, count, 3, begin, end);
}
__kernel void mh_v2_bind(__global const unsigned char* headers, __global mh_u64* states,
                       __global mh_u64* scratchpad, __global unsigned char* outputs,
                       unsigned count, unsigned begin, unsigned end)
{
    const unsigned id = MH_ID, lane = MH_LANE, lanes = (unsigned)get_local_size(0);
    // Uniform group exit only. Host selects a power of two in [8,64], checked
    // against every kernel and device limit. Local storage is 4096 bytes.
    if (id >= count) return;
    // Keep adjacent lanes contiguous for each word, without assuming a wave
    // width or a particular local-memory bank arrangement.
    __local mh_u64 reduction[8][64];
    mh_u64 state[16];
    for (unsigned j = 0; j < 16; ++j) state[j] = states[id * 16 + j];
    if (begin == 0) {
        state[0] ^= CONSTANTS[4];
        state[8] += CONSTANTS[5];
        MercaARXRound(state);
    }
    for (unsigned block = begin; block < end; ++block) {
        const mh_u64 base_line = (mh_u64)block * MH_BIND_BLOCK_LINES;
        const Line head = ReadLine(scratchpad, base_line, count, id);
        const mh_u64 reference_seed = state[0] ^ mh_rotl(state[7], 19) ^ state[13] ^ head.v[0] ^
            mh_rotl(head.v[7], 31) ^ ((mh_u64)block * CONSTANTS[1]) ^ CONSTANTS[6];
        const mh_u64 previous_block = block == 0 ? MH_BIND_BLOCK_COUNT - 1 : block - 1;
        const mh_u64 reference_block = block == 0 ? MH_BIND_BLOCK_COUNT - 1 : reference_seed % block;
        const mh_u64 rotation = (reference_seed >> 17) & (MH_BIND_BLOCK_LINES - 1);
        Line accumulator = {{0}};
        // Every lane must snapshot head before lane zero may overwrite it.
        barrier(CLK_GLOBAL_MEM_FENCE);
        for (mh_u64 line = lane; line < MH_BIND_BLOCK_LINES; line += lanes) {
            const mh_u64 current_line = base_line + line;
            const Line current = ReadLine(scratchpad, current_line, count, id);
            const Line previous = ReadLine(scratchpad, previous_block * MH_BIND_BLOCK_LINES +
                ((line + MH_BIND_BLOCK_LINES - 1) & (MH_BIND_BLOCK_LINES - 1)), count, id);
            const Line reference = ReadLine(scratchpad, reference_block * MH_BIND_BLOCK_LINES +
                ((line + rotation) & (MH_BIND_BLOCK_LINES - 1)), count, id);
            const mh_u64 tag = (mh_u64)block ^ (line * CONSTANTS[0]);
            Line bound = {{0}};
            for (unsigned j = 0; j < 8; ++j) {
                const mh_u64 x = current.v[j] + previous.v[(j + 1) & 7];
                const mh_u64 y = reference.v[(j + 3) & 7] + state[(j + line) & 15];
                bound.v[j] = x ^ mh_rotl(y, ROTATION_1[j]) ^ mh_rotl(tag, ROTATION_2[j]);
                scratchpad[(mh_u64)id * MH_LINE_COUNT * 8 + current_line * 8 + j] = bound.v[j];
            }
            // Previous/reference blocks are strictly outside the current block.
            // Each current line has one owner, so these writes cannot destroy
            // another lane's input (including the block-zero wraparound).
            for (unsigned j = 0; j < 8; ++j)
                accumulator.v[j] ^= bound.v[j] + mh_rotl(bound.v[(j + 1) & 7], ROTATION_2[j]);
        }
        for (unsigned j = 0; j < 8; ++j) reduction[j][lane] = accumulator.v[j];
        barrier(CLK_GLOBAL_MEM_FENCE | CLK_LOCAL_MEM_FENCE);
        for (unsigned delta = lanes / 2; delta; delta >>= 1) {
            if (lane < delta)
                for (unsigned j = 0; j < 8; ++j) reduction[j][lane] ^= reduction[j][lane + delta];
            barrier(CLK_LOCAL_MEM_FENCE);
        }
        // All lanes use the identical reduced accumulator and private state.
        for (unsigned j = 0; j < 8; ++j) {
            state[j] ^= reduction[j][0] + reference_seed;
            state[j + 8] += mh_rotl(reduction[(j + 3) & 7][0], ROTATION_1[j]) ^ (mh_u64)block;
        }
        if ((block + 1) % MH_BIND_ARX_INTERVAL == 0) MercaARXRound(state);
        // No fast lane may reuse local storage while another reads lane zero's values.
        barrier(CLK_LOCAL_MEM_FENCE);
    }
    if (end == MH_BIND_BLOCK_COUNT) MercaARXRound(state);
    if (lane == 0)
        for (unsigned j = 0; j < 16; ++j) states[id * 16 + j] = state[j];
}
#endif // BITCOIN_MINING_MERCAHASH_OPENCL_V2_H
