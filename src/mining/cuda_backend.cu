// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <cuda_runtime.h>
#include <crypto/mercahash.h>
#include <mining/gpu_backend.h>
#define MH_CUDA
#include <algorithm>
#include <limits>
namespace mh_v1 {
#include <mining/mercahash_gpu_specialized.h>
static_assert(MH_LINE_COUNT == mercahash::LINE_COUNT && MH_LINE_MASK == mercahash::LINE_MASK);
static_assert(MH_BIND_BLOCK_COUNT == mercahash::BIND_BLOCK_COUNT);
}
#undef BITCOIN_MINING_MERCAHASH_GPU_SPECIALIZED_H
#undef MH_VERSION
#define MH_VERSION 2
#define mh_init mh_init_v2
#define mh_fill mh_fill_v2
#define mh_bind mh_bind_v2
#define mh_mix mh_mix_v2
#define mh_final mh_final_v2
namespace mh_v2 {
#include <mining/mercahash_gpu_specialized.h>
static_assert(MH_LINE_COUNT == mercahash::V2_LINE_COUNT && MH_LINE_MASK == mercahash::V2_LINE_MASK);
static_assert(MH_BIND_BLOCK_COUNT == mercahash::V2_BIND_BLOCK_COUNT);
}
#undef mh_init
#undef mh_fill
#undef mh_bind
#undef mh_mix
#undef mh_final
#undef MH_VERSION
using mh_u64 = unsigned long long;

namespace mh_v2 {
template<unsigned W> __device__ __forceinline__ mh_u64 exchange(mh_u64 x, unsigned src, unsigned mask) {
    return (mh_u64(__shfl_sync(mask, unsigned(x >> 32), src, W)) << 32) |
           __shfl_sync(mask, unsigned(x), src, W);
}

constexpr unsigned FILL_THREADS=16,HASHES_PER_BLOCK=16;
__global__ void research_fill(mh_u64* states,mh_u64* scratchpad,unsigned count,unsigned begin,unsigned end) {
 const unsigned id=(blockIdx.x*blockDim.x+threadIdx.x)*1;
 if(id>=count) return;
 const unsigned id0=id+0; mh_u64 s0[16]; for(unsigned j=0;j<16;++j) s0[j]=states[(id0<count?id0:id)*16+j];


    if (begin == 0) {
        // Domain-separated transition from initial s0 into scratchpad fill.
        s0[0] ^= 0x9E3779B97F4A7C15ULL;
        s0[5] ^= 0xBF58476D1CE4E5B9ULL;
        s0[10] ^= 0x94D049BB133111EBULL;
        s0[15] ^= 0xD6E8FEB86659FD93ULL;
    }

 for(unsigned k=begin;k<end;k+=2) {
{unsigned i=k;
{        const mh_u64 x = (mh_u64)(i);

        s0[0] += x;
        s0[7] ^= mh_rotl(x, 17);
        s0[8] += x ^ 0x9E3779B97F4A7C15ULL;
        s0[15] ^= mh_rotl(x, 41);

}
s0[0]=s0[0]+s0[4];
s0[1]=s0[1]+s0[5];
s0[2]=s0[2]+s0[6];
s0[3]=s0[3]+s0[7];
s0[12]=mh_rotl(s0[12]^s0[0],32);
s0[13]=mh_rotl(s0[13]^s0[1],32);
s0[14]=mh_rotl(s0[14]^s0[2],32);
s0[15]=mh_rotl(s0[15]^s0[3],32);
s0[8]=s0[8]+s0[12];
s0[9]=s0[9]+s0[13];
s0[10]=s0[10]+s0[14];
s0[11]=s0[11]+s0[15];
s0[4]=mh_rotl(s0[4]^s0[8],24);
s0[5]=mh_rotl(s0[5]^s0[9],24);
s0[6]=mh_rotl(s0[6]^s0[10],24);
s0[7]=mh_rotl(s0[7]^s0[11],24);
s0[0]=s0[0]+s0[4];
s0[1]=s0[1]+s0[5];
s0[2]=s0[2]+s0[6];
s0[3]=s0[3]+s0[7];
s0[12]=mh_rotl(s0[12]^s0[0],16);
s0[13]=mh_rotl(s0[13]^s0[1],16);
s0[14]=mh_rotl(s0[14]^s0[2],16);
s0[15]=mh_rotl(s0[15]^s0[3],16);
s0[8]=s0[8]+s0[12];
s0[9]=s0[9]+s0[13];
s0[10]=s0[10]+s0[14];
s0[11]=s0[11]+s0[15];
s0[4]=mh_rotl(s0[4]^s0[8],63);
s0[5]=mh_rotl(s0[5]^s0[9],63);
s0[6]=mh_rotl(s0[6]^s0[10],63);
s0[7]=mh_rotl(s0[7]^s0[11],63);
{
        Line line = {{0}};

        for (mh_u64 j = 0; j < MH_LINE_WORDS; ++j) {
            line.v[j] = s0[j] ^ s0[j + 8];
        }

        // Experimental single-thread scratchpad fill.
          for (unsigned j = 0; j < MH_LINE_WORDS; ++j) {
              if(id0<count) scratchpad[
                  (mh_u64)id0 * MH_LINE_COUNT * MH_LINE_WORDS
                  + (mh_u64)i * MH_LINE_WORDS + j
              ] = line.v[j];
          }

        // Feed the newly generated line back into the evolving s0.
        s0[0] += line.v[3];
        s0[1] ^= mh_rotl(line.v[4], 11);
        s0[2] += line.v[5];
        s0[3] ^= mh_rotl(line.v[6], 23);

        s0[4] += line.v[7];
        s0[5] ^= mh_rotl(line.v[0], 37);
        s0[6] += line.v[1];
        s0[7] ^= mh_rotl(line.v[2], 53);

        s0[8] ^= line.v[5];
        s0[9] += mh_rotl(line.v[6], 7);
        s0[10] ^= line.v[7];
        s0[11] += mh_rotl(line.v[0], 19);

        s0[12] ^= line.v[1];
        s0[13] += mh_rotl(line.v[2], 31);
        s0[14] ^= line.v[3];
        s0[15] += mh_rotl(line.v[4], 47);
}
}
{unsigned i=k+1;
{        const mh_u64 x = (mh_u64)(i);

        s0[0] += x;
        s0[7] ^= mh_rotl(x, 17);
        s0[8] += x ^ 0x9E3779B97F4A7C15ULL;
        s0[15] ^= mh_rotl(x, 41);

}
s0[0]=s0[0]+s0[5];
s0[1]=s0[1]+s0[6];
s0[2]=s0[2]+s0[7];
s0[3]=s0[3]+s0[4];
s0[15]=mh_rotl(s0[15]^s0[0],32);
s0[12]=mh_rotl(s0[12]^s0[1],32);
s0[13]=mh_rotl(s0[13]^s0[2],32);
s0[14]=mh_rotl(s0[14]^s0[3],32);
s0[10]=s0[10]+s0[15];
s0[11]=s0[11]+s0[12];
s0[8]=s0[8]+s0[13];
s0[9]=s0[9]+s0[14];
s0[5]=mh_rotl(s0[5]^s0[10],24);
s0[6]=mh_rotl(s0[6]^s0[11],24);
s0[7]=mh_rotl(s0[7]^s0[8],24);
s0[4]=mh_rotl(s0[4]^s0[9],24);
s0[0]=s0[0]+s0[5];
s0[1]=s0[1]+s0[6];
s0[2]=s0[2]+s0[7];
s0[3]=s0[3]+s0[4];
s0[15]=mh_rotl(s0[15]^s0[0],16);
s0[12]=mh_rotl(s0[12]^s0[1],16);
s0[13]=mh_rotl(s0[13]^s0[2],16);
s0[14]=mh_rotl(s0[14]^s0[3],16);
s0[10]=s0[10]+s0[15];
s0[11]=s0[11]+s0[12];
s0[8]=s0[8]+s0[13];
s0[9]=s0[9]+s0[14];
s0[5]=mh_rotl(s0[5]^s0[10],63);
s0[6]=mh_rotl(s0[6]^s0[11],63);
s0[7]=mh_rotl(s0[7]^s0[8],63);
s0[4]=mh_rotl(s0[4]^s0[9],63);
{
        Line line = {{0}};

        for (mh_u64 j = 0; j < MH_LINE_WORDS; ++j) {
            line.v[j] = s0[j] ^ s0[j + 8];
        }

        // Experimental single-thread scratchpad fill.
          for (unsigned j = 0; j < MH_LINE_WORDS; ++j) {
              if(id0<count) scratchpad[
                  (mh_u64)id0 * MH_LINE_COUNT * MH_LINE_WORDS
                  + (mh_u64)i * MH_LINE_WORDS + j
              ] = line.v[j];
          }

        // Feed the newly generated line back into the evolving s0.
        s0[0] += line.v[3];
        s0[1] ^= mh_rotl(line.v[4], 11);
        s0[2] += line.v[5];
        s0[3] ^= mh_rotl(line.v[6], 23);

        s0[4] += line.v[7];
        s0[5] ^= mh_rotl(line.v[0], 37);
        s0[6] += line.v[1];
        s0[7] ^= mh_rotl(line.v[2], 53);

        s0[8] ^= line.v[5];
        s0[9] += mh_rotl(line.v[6], 7);
        s0[10] ^= line.v[7];
        s0[11] += mh_rotl(line.v[0], 19);

        s0[12] ^= line.v[1];
        s0[13] += mh_rotl(line.v[2], 31);
        s0[14] ^= line.v[3];
        s0[15] += mh_rotl(line.v[4], 47);
}
}
}

 if(id0<count) for(unsigned j=0;j<16;++j) states[id0*16+j]=s0[j];
}
}


namespace mh_v2 {
constexpr unsigned MIX_WIDTH=1,MIX_THREADS=8,MIX_HASHES=8;
__device__ __forceinline__ void ResearchWriteLine(mh_u64* scratchpad,mh_u64 index,Line line,unsigned stride,unsigned id) {
    const unsigned lane=threadIdx.x%MIX_WIDTH;
for(unsigned j=lane;j<8;j+=MIX_WIDTH) scratchpad[mh_u64(id)*262144*8+index*8+j]=line.v[j];
}


MH_INLINE PrimaryResult ResearchPrimaryStep(mh_u64 *state, MH_GLOBAL mh_u64 *scratchpad, mh_u64 step, unsigned stride,
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

    ResearchWriteLine(scratchpad, primary_index, written, stride, id);

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



MH_INLINE SecondaryResult ResearchSecondaryStep(mh_u64 *state, MH_GLOBAL mh_u64 *scratchpad, mh_u64 step, PrimaryResult primary,
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

    ResearchWriteLine(scratchpad, secondary_index, written, stride, id);

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

MH_INLINE void ResearchCrossLaneMix(mh_u64 *state, mh_u64 step)
{
    state[0] ^= step;
    state[8] += step * CONSTANTS[2];

    MercaARXRound(state);
    MercaARXRound(state);
}

MH_INLINE void ResearchCheckpoint(mh_u64 *state, mh_u64 step, PrimaryResult primary, SecondaryResult secondary)
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
MH_INLINE void ResearchMixScratchpad(mh_u64 *state, MH_GLOBAL mh_u64 *scratchpad, unsigned begin, unsigned end, unsigned stride,
                             unsigned id)
{
    for (mh_u64 step = begin; step <= end; ++step) {
        const PrimaryResult primary = ResearchPrimaryStep(state, scratchpad, step, stride, id);

        if (step % MH_SECONDARY_INTERVAL == 0) {
            const SecondaryResult secondary = ResearchSecondaryStep(state, scratchpad, step, primary, stride, id);

            if (step % MH_CROSS_LANE_INTERVAL == 0) {
                ResearchCrossLaneMix(state, step);
            }

            if (step % MH_CHECKPOINT_INTERVAL == 0) {
                ResearchCheckpoint(state, step, primary, secondary);
            }
        }
    }
}


__global__ void research_mix(mh_u64* states,mh_u64* scratchpad,unsigned count,unsigned begin,unsigned end) {
    const unsigned id=blockIdx.x*MIX_HASHES+threadIdx.x/MIX_WIDTH;
    if(id>=count) return; // Whole subgroups exit together.
    mh_u64 state[16];
    for(unsigned j=0;j<16;++j) state[j]=states[id*16+j];
    ResearchMixScratchpad(state,scratchpad,begin,end,count,id);
    if(threadIdx.x%MIX_WIDTH==0) for(unsigned j=0;j<16;++j) states[id*16+j]=state[j];
}
}

namespace mh_v2 {
__device__ __forceinline__ void bind_arx8(mh_u64& a,mh_u64& b,unsigned j,unsigned mask,bool diagonal) {
{const mh_u64 v=exchange<8>(a,((1*4+((j%4+(diagonal?1:0)+8)%4))),mask); if(j/4==0) a=a+v;}
{const mh_u64 v=exchange<8>(a,((0*4+((j%4+(diagonal?-3:0)+8)%4))),mask); if(j/4==1) b=mh_rotl(b^v,32);}
{const mh_u64 v=exchange<8>(b,((1*4+((j%4+(diagonal?1:0)+8)%4))),mask); if(j/4==0) b=b+v;}
{const mh_u64 v=exchange<8>(b,((0*4+((j%4+(diagonal?1:0)+8)%4))),mask); if(j/4==1) a=mh_rotl(a^v,24);}
{const mh_u64 v=exchange<8>(a,((1*4+((j%4+(diagonal?1:0)+8)%4))),mask); if(j/4==0) a=a+v;}
{const mh_u64 v=exchange<8>(a,((0*4+((j%4+(diagonal?-3:0)+8)%4))),mask); if(j/4==1) b=mh_rotl(b^v,16);}
{const mh_u64 v=exchange<8>(b,((1*4+((j%4+(diagonal?1:0)+8)%4))),mask); if(j/4==0) b=b+v;}
{const mh_u64 v=exchange<8>(b,((0*4+((j%4+(diagonal?1:0)+8)%4))),mask); if(j/4==1) a=mh_rotl(a^v,63);}
}

__device__ __forceinline__ void bind_round8(mh_u64& a,mh_u64& b,unsigned j,unsigned mask) {
    bind_arx8(a,b,j,mask,false); bind_arx8(a,b,j,mask,true);
}
__global__ void research_bind(mh_u64* states,mh_u64* scratchpad,unsigned count,unsigned begin,unsigned end) {
    const unsigned id=blockIdx.x,j=threadIdx.x&7;
    if(id>=count) return;
    const unsigned mask=0xffffffffu;
    mh_u64 a=states[id*16+j],b=states[id*16+8+j];
    if(begin==0) {
        if(j==0) { a^=0xA0761D6478BD642FULL; b+=0xE7037ED1A0B428DBULL; }
        bind_round8(a,b,j,mask);
    }
    mh_u64* pad=scratchpad+mh_u64(id)*262144*8;
    const unsigned r1=ROTATION_1[j],r2=ROTATION_2[j];
    for(unsigned block=begin;block<end;++block) {
        const mh_u64 seed=exchange<8>(a,0,mask)^mh_rotl(exchange<8>(a,7,mask),19)^exchange<8>(b,5,mask)^
            pad[mh_u64(block)*64*8]^mh_rotl(pad[mh_u64(block)*64*8+7],31)^
            (mh_u64(block)*0xBF58476D1CE4E5B9ULL)^0x8EBC6AF09C88C6E3ULL;
        const unsigned previous=block==0?4095:block-1;
        const unsigned reference=block==0?4095:unsigned(seed%mh_u64(block));
        const unsigned rotation=(seed>>17)&63;
        mh_u64 acc=0;
        __syncwarp(mask);
        for(unsigned line=threadIdx.x/8;line<64;line+=4) {
            const mh_u64 current=pad[(mh_u64(block)*64+line)*8+j];
            const mh_u64 prev=pad[(mh_u64(previous)*64+((line+63)&63))*8+((j+1)&7)];
            const mh_u64 ref=pad[(mh_u64(reference)*64+((line+rotation)&63))*8+((j+3)&7)];
            const unsigned index=(j+line)&15;
            const mh_u64 lower=exchange<8>(a,index&7,mask),upper=exchange<8>(b,index&7,mask);
            const mh_u64 word=index<8?lower:upper;
            const mh_u64 tag=mh_u64(block)^(mh_u64(line)*0x9E3779B97F4A7C15ULL);
            const mh_u64 bound=(current+prev)^mh_rotl(ref+word,r1)^mh_rotl(tag,r2);
            pad[(mh_u64(block)*64+line)*8+j]=bound;
            const mh_u64 next=exchange<8>(bound,(j+1)&7,mask);
            acc^=bound+mh_rotl(next,r2);
        }
        __syncwarp(mask);
        acc^=exchange<32>(acc,threadIdx.x^8,mask);
        acc^=exchange<32>(acc,threadIdx.x^16,mask);
        const mh_u64 rotated=exchange<8>(acc,(j+3)&7,mask);
        a^=acc+seed;
        b+=mh_rotl(rotated,r1)^mh_u64(block);
        if((block+1)%16==0) bind_round8(a,b,j,mask);
    }
    if(end==4096) bind_round8(a,b,j,mask);
    if(threadIdx.x<8) { states[id*16+j]=a; states[id*16+j+8]=b; }
}
}

#include <chrono>
#include <thread>

#include <stdexcept>
#include <util/string.h>
#ifndef MERCATURA_CUDA_MIN_ARCH
#define MERCATURA_CUDA_MIN_ARCH 75
#endif
namespace mining
{
namespace
{
void Check(cudaError_t error, const char *operation)
{
    if (error != cudaSuccess)
        throw std::runtime_error{std::string{operation} + ": " + cudaGetErrorString(error)};
}
class CudaBackend final : public GpuBackend
{
    int device;
    const mercahash::Version version;
    unsigned int contexts{0}, estimated{0};
    unsigned int execution_limit{0}, next_context{0};
    GpuMemoryBudget allocation_budget;
    uint64_t memory_bytes{0};
    cudaStream_t stream{nullptr};
    // Each event is reused only after its preceding marker completed. Every
    // return/exception drains the stream before caller or device buffers die.
    cudaEvent_t completion_events[2]{};
    unsigned char *headers{nullptr};
    mh_u64 *states{nullptr};
    mh_u64 *scratch{nullptr};
    unsigned char *outputs{nullptr};

  public:
    CudaBackend(const GpuDevice &selected, unsigned int requested, const std::function<bool()>& cancelled) : device{static_cast<int>(selected.ordinal)}, version{selected.pow_version}
    {
        try {
            Check(cudaSetDevice(device), "Select NVIDIA device");
            cudaDeviceProp properties{};
            Check(cudaGetDeviceProperties(&properties, device), "Read NVIDIA device properties");
            if (selected.name != properties.name)
                throw std::runtime_error{"NVIDIA enumeration changed; refresh device selection"};
            if (properties.major * 10 + properties.minor < MERCATURA_CUDA_MIN_ARCH)
                throw std::runtime_error{"NVIDIA device no longer meets the required compute capability"};
            if (version == mercahash::Version::V2) {
                if (properties.multiProcessorCount <= 0 || properties.maxGridSize[0] <= 0)
                    throw std::runtime_error{"Invalid NVIDIA execution geometry"};
                int active_blocks{std::numeric_limits<int>::max()};
                const auto occupancy = [&](auto kernel, int threads) {
                    int blocks{0};
                    Check(cudaOccupancyMaxActiveBlocksPerMultiprocessor(&blocks, kernel, threads, 0), "Read NVIDIA V2 kernel occupancy");
                    if (blocks <= 0) throw std::runtime_error{"NVIDIA V2 kernel has no executable occupancy"};
                    active_blocks = std::min(active_blocks, blocks);
                };
                occupancy(mh_v2::mh_init_v2, 32); occupancy(mh_v2::research_fill, mh_v2::FILL_THREADS);
                occupancy(mh_v2::research_bind, 32); occupancy(mh_v2::research_mix, mh_v2::MIX_THREADS); occupancy(mh_v2::mh_final_v2, 32);
                execution_limit = GpuExecutionContexts(std::numeric_limits<unsigned int>::max(),
                    uint64_t{static_cast<unsigned>(properties.multiProcessorCount)} * active_blocks, properties.maxGridSize[0]);
            }
            // Initialize the stream first, then budget from a fresh query
            // immediately before allocating the mining buffers.
            Check(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking), "Create NVIDIA mining stream");
            if(version==mercahash::Version::V2) for(auto& event:completion_events) Check(cudaEventCreateWithFlags(&event,cudaEventDisableTiming),"Create V2 completion event");
            const auto allocation{AllocateGpuContexts(requested, [&] {
                size_t available{0}, total{0};
                Check(cudaMemGetInfo(&available, &total), "Read NVIDIA free memory");
                return CalculateGpuBudget(available, available, 0, GpuMemorySource::CUDA_FREE, version);
            }, [&](unsigned int count, const GpuMemoryBudget& budget) {
                contexts = count;
                memory_bytes = count * uint64_t{mercahash::ScratchpadBytes(version)};
                auto allocate = [&](void** buffer, size_t bytes, const char* operation) {
                    const auto error{cudaMalloc(buffer, bytes)};
                    if (error == cudaErrorMemoryAllocation) {
                        // Consume the handled OOM so it cannot poison the next
                        // kernel launch check. Never mask a different error.
                        const auto pending{cudaGetLastError()};
                        if (pending != cudaErrorMemoryAllocation) Check(pending, "Read NVIDIA allocation error state");
                        throw GpuMemoryAllocationError{std::string{operation} + ": " + cudaGetErrorString(error)};
                    }
                    Check(error, operation);
                };
                allocate(reinterpret_cast<void **>(&headers), contexts * size_t{80}, "Allocate NVIDIA headers");
                allocate(reinterpret_cast<void **>(&states), contexts * size_t{128}, "Allocate NVIDIA states");
                allocate(reinterpret_cast<void **>(&scratch), static_cast<size_t>(memory_bytes), "Allocate NVIDIA scratchpads");
                allocate(reinterpret_cast<void **>(&outputs), contexts * size_t{32}, "Allocate NVIDIA outputs");
                size_t available{0}, total{0};
                Check(cudaMemGetInfo(&available, &total), "Validate NVIDIA memory headroom");
                if (available < budget.reserved_bytes)
                    throw GpuMemoryAllocationError{"NVIDIA free VRAM fell below the required reserve"};
                allocation_budget = budget;
                allocation_budget.contexts = count;
                allocation_budget.scratchpad_bytes = memory_bytes;
                allocation_budget.overhead_bytes = (64ULL << 20) + uint64_t{count} * 4096;
                allocation_budget.headroom_bytes = available;
                allocation_budget.headroom_measured = true;
            }, [&] { CleanupBuffers(true); }, cancelled, version)};
            estimated = allocation.estimated;
        } catch (...) {
            Cleanup();
            throw;
        }
    }
    ~CudaBackend() override
    {
        Cleanup();
    }
    unsigned int Contexts() const override
    {
        return contexts;
    }
    mercahash::Version PoWVersion() const override { return version; }
    unsigned int EstimatedContexts() const override { return estimated; }
    unsigned int BatchContexts() const override { return version == mercahash::Version::V2 ? std::min({contexts, execution_limit, contexts - next_context}) : contexts; }
    GpuMemoryBudget Budget() const override { return allocation_budget; }
    uint64_t MemoryBytes() const override
    {
        return memory_bytes + (version == mercahash::Version::V2 ? uint64_t{contexts} * 240 : 0);
    }
    std::vector<GpuHash> Hash(std::span<const GpuHeader> input, const std::function<bool()> &cancelled) override
    {
        if (version == mercahash::Version::V2) return HashV2(input, cancelled);
        if (input.empty() || input.size() > contexts)
            throw std::runtime_error{"Invalid NVIDIA hashing batch size"};
        if (cancelled())
            return {};
        Check(cudaSetDevice(device), "Select NVIDIA mining device");
        const unsigned int count{static_cast<unsigned int>(input.size())};
        std::vector<GpuHash> hashes(count);
        struct DrainOnExit {
            cudaStream_t stream;
            bool complete{false};
            ~DrainOnExit()
            {
                // Do not throw during unwinding. Cleanup also handles device loss.
                if (!complete) cudaStreamSynchronize(stream);
            }
        } drain{stream};
        Check(cudaMemcpyAsync(headers, input.data(), input.size_bytes(), cudaMemcpyHostToDevice, stream),
              "Upload NVIDIA headers");
        auto synchronized = [&] {
            Check(cudaGetLastError(), "Launch NVIDIA MercaHash kernel");
            Check(cudaStreamSynchronize(stream), "Synchronize NVIDIA MercaHash kernel");
            return !cancelled();
        };
        mh_v1::mh_init<<<count, 32, 0, stream>>>(headers, states, count);
        if (!synchronized())
            return {};
        // Validated single-lane fill: queue 32 kernels on the same stream.
        for (unsigned int begin{0}; begin < 2097152; begin += 16384) {
            if (cancelled()) {
                Check(cudaStreamSynchronize(stream),
                      "Drain cancelled fill work");
                return {};
            }

            mh_v1::mh_fill<<<count, 1, 0, stream>>>(headers, states, scratch, outputs, count, begin, begin + 16384);

            Check(cudaGetLastError(),
                  "Launch NVIDIA fill kernel");

            if (((begin / 16384 + 1) % 32) == 0) {
                if (!synchronized())
                    return {};
            }
        }
        // Validated warp-parallel binding: synchronize every 32 chunks.
        // Same-stream ordering preserves kernel dependencies.
        for (unsigned int begin{0}; begin < 32768; begin += 64) {
            if (cancelled()) {
                Check(cudaStreamSynchronize(stream),
                      "Drain cancelled binding work");
                return {};
            }

            mh_v1::mh_bind<<<count, 32, 0, stream>>>(headers, states, scratch, outputs, count, begin, begin + 64);

            Check(cudaGetLastError(),
                  "Launch NVIDIA binding kernel");

            if (((begin / 64 + 1) % 32) == 0) {
                if (!synchronized())
                    return {};
            }
        }
        // Validated mixing cadence: synchronize every 16 kernels.
        for (unsigned int begin{1}; begin <= 131072; begin += 1024) {
            if (cancelled()) {
                Check(cudaStreamSynchronize(stream),
                      "Drain cancelled mixing work");
                return {};
            }

            mh_v1::mh_mix<<<count, 32, 0, stream>>>(
                headers, states, scratch, outputs,
                count, begin, begin + 1023);

            Check(cudaGetLastError(), "Launch NVIDIA mixing kernel");

            if ((((begin - 1) / 1024 + 1) % 16) == 0) {
                if (!synchronized())
                    return {};
            }
        }
        if (cancelled())
            return {};
        mh_v1::mh_final<<<count, 32, 0, stream>>>(headers, states, scratch, outputs, count, 0, 0);
        if (!synchronized())
            return {};
        Check(cudaMemcpyAsync(hashes.data(), outputs, hashes.size() * sizeof(GpuHash), cudaMemcpyDeviceToHost, stream),
              "Read NVIDIA MercaHash outputs");
        Check(cudaStreamSynchronize(stream), "Synchronize NVIDIA output transfer");
        drain.complete = true;
        if (cancelled())
            return {};
        return hashes;
    }

  private:
    std::vector<GpuHash> HashV2(std::span<const GpuHeader> input, const std::function<bool()>& cancelled)
    {
        if (input.empty() || input.size() > contexts) throw std::runtime_error{"Invalid NVIDIA V2 hashing batch size"};
        if (cancelled()) return {};
        Check(cudaSetDevice(device), "Select NVIDIA V2 mining device");
        std::vector<GpuHash> hashes(input.size());
        struct Drain {
            cudaStream_t stream;
            bool complete{false};
            ~Drain() { if (!complete) cudaStreamSynchronize(stream); }
        } drain{stream};
        // Rotate across the entire allocated pool. Every launch has at most
        // one occupancy-derived hardware wave and is drained separately.
        // Geometry and latency bounds never cap the total resident capacity.
        for (size_t completed{0}; completed < input.size();) {
            if (cancelled()) return {};
            const auto count{static_cast<unsigned int>(std::min({size_t{BatchContexts()}, input.size() - completed, size_t{contexts - next_context}}))};
            auto* h{headers + size_t{next_context} * 80};
            auto* s{states + size_t{next_context} * 16};
            auto* pad{scratch + size_t{next_context} * (mercahash::V2_SCRATCHPAD_BYTES / sizeof(mh_u64))};
            auto* out{outputs + size_t{next_context} * 32};
            next_context = static_cast<unsigned int>((uint64_t{next_context} + count) % contexts);
            Check(cudaMemcpyAsync(h, input.data() + completed, size_t{count} * 80, cudaMemcpyHostToDevice, stream), "Upload NVIDIA V2 headers");
            unsigned event_head=0,event_tail=0,pending_events=0;
            const auto synchronized = [&] {
                Check(cudaGetLastError(),"Launch sliding-window V2 kernel");
                Check(cudaEventRecord(completion_events[event_tail],stream),"Record sliding V2 completion");
                event_tail=(event_tail+1)%2;
                if(++pending_events==2) {
                    const unsigned event_index=event_head;

                    for (;;) {
                        const auto status=cudaEventQuery(completion_events[event_index]);
                        if(status==cudaSuccess) break;
                        if(status!=cudaErrorNotReady) Check(status,"Poll NVIDIA V2 completion");
                        if(cancelled()) return false;
                        std::this_thread::sleep_for(std::chrono::microseconds(50));
                    }

                    event_head=(event_head+1)%2;
                    --pending_events;
                }
                return !cancelled();
            };
            mh_v2::mh_init_v2<<<count, 32, 0, stream>>>(h, s, count);
            if (!synchronized()) return {};
            for (unsigned begin{0}; begin < mercahash::V2_LINE_COUNT; begin += 16384) {
                if (cancelled()) return {};
                mh_v2::research_fill<<<(count+mh_v2::HASHES_PER_BLOCK-1)/mh_v2::HASHES_PER_BLOCK, mh_v2::FILL_THREADS, 0, stream>>>(s, pad, count, begin, begin + 16384);
                if (!synchronized()) return {};
            }
            for (unsigned begin{0}; begin < mercahash::V2_BIND_BLOCK_COUNT; begin += 64) {
                if (cancelled()) return {};
                mh_v2::research_bind<<<count, 32, 0, stream>>>(s, pad, count, begin, begin + 64);
                if (!synchronized()) return {};
            }
            for (unsigned begin{1}; begin <= mercahash::V2_MIX_STEPS; begin += 256) {
                if (cancelled()) return {};
                mh_v2::research_mix<<<(count+mh_v2::MIX_HASHES-1)/mh_v2::MIX_HASHES, mh_v2::MIX_THREADS, 0, stream>>>(s, pad, count, begin, begin + 255);
                if (!synchronized()) return {};
            }
            mh_v2::mh_final_v2<<<count, 32, 0, stream>>>(h, s, pad, out, count, 0, 0);
            if (!synchronized()) return {};
            Check(cudaMemcpyAsync(hashes.data() + completed, out, size_t{count} * 32, cudaMemcpyDeviceToHost, stream), "Read NVIDIA V2 output");
            Check(cudaStreamSynchronize(stream), "Synchronize NVIDIA V2 output");
            if (cancelled()) return {};
            completed += count;
        }
        drain.complete = true;
        return hashes;
    }
    void CleanupBuffers(bool checked = false)
    {
        // Never reset a device: other workers/applications may own contexts.
        // Retry cleanup checks errors; final teardown remains non-throwing.
        auto failure{cudaSetDevice(device)};
        auto record = [&](cudaError_t error) { if (failure == cudaSuccess) failure = error; };
        if (stream) record(cudaStreamSynchronize(stream));
        auto release = [&](auto& buffer) {
            if (!buffer) return;
            const auto error{cudaFree(buffer)};
            record(error);
            // Retain a failed release for the constructor's final cleanup.
            if (error == cudaSuccess || !checked) buffer = nullptr;
        };
        release(headers); release(states); release(scratch); release(outputs);
        if (checked) Check(failure, "Clean up NVIDIA allocation attempt");
    }
    void Cleanup()
    {
        CleanupBuffers();
        for(auto& event:completion_events) { if(event) cudaEventDestroy(event); event=nullptr; }
        if (stream)
            cudaStreamDestroy(stream);
        stream = nullptr;
    }
};
} // namespace
GpuDiscovery DiscoverCudaDevices()
{
    GpuDiscovery result;
    int count{0};
    auto status{cudaGetDeviceCount(&count)};
    if (status != cudaSuccess) {
        result.errors.emplace_back(std::string{"NVIDIA CUDA unavailable: "} + cudaGetErrorString(status));
        return result;
    }
    for (int index{0}; index < count; ++index) {
        GpuDevice entry{GpuVendor::NVIDIA_CUDA};
        entry.ordinal = index;
        entry.id = "cuda:" + util::ToString(index);
        try {
            cudaDeviceProp properties{};
            Check(cudaGetDeviceProperties(&properties, index), "Read NVIDIA capability");
            entry.dedicated = !properties.integrated;
            entry.name = properties.name;
            entry.capability = "CUDA " + util::ToString(properties.major) + "." + util::ToString(properties.minor);
            entry.total_memory = properties.totalGlobalMem;
            Check(cudaSetDevice(index), "Select NVIDIA discovery device");
            size_t available{0}, total{0};
            Check(cudaMemGetInfo(&available, &total), "Read NVIDIA memory");
            entry.available_memory = available;
            entry.max_allocation = available;
            entry.supported = properties.major * 10 + properties.minor >= MERCATURA_CUDA_MIN_ARCH &&
                              CalculateGpuBudget(available, available, 0, GpuMemorySource::CUDA_FREE, mercahash::Version::V2).contexts > 0;
            if (!entry.supported)
                entry.error = "NVIDIA device needs compute capability at least " +
                              util::ToString(MERCATURA_CUDA_MIN_ARCH / 10) + "." +
                              util::ToString(MERCATURA_CUDA_MIN_ARCH % 10) + " and sufficient free VRAM";
        } catch (const std::exception &error) {
            entry.error = error.what();
        }
        result.devices.push_back(std::move(entry));
    }
    if (!count)
        result.errors.emplace_back("No NVIDIA CUDA GPU detected");
    return result;
}
std::unique_ptr<GpuBackend> CreateCudaBackend(const GpuDevice &device, unsigned int contexts, const std::function<bool()>& cancelled)
{
    return std::make_unique<CudaBackend>(device, contexts, cancelled);
}
} // namespace mining
