// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <crypto/mercahash.h>

#include <crypto/common.h>
#include <crypto/sha3.h>
#include <span.h>

#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#include <immintrin.h>
#if defined(__APPLE__)
#include <compat/cpuid.h>
#endif
#endif
#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <string_view>
#include <stdexcept>
#include <vector>

namespace mercahash {
namespace {
// V2 geometry is local to this implementation. V1's constants and source are unchanged.
constexpr std::size_t SCRATCHPAD_BYTES = V2_SCRATCHPAD_BYTES;
constexpr std::size_t LINE_COUNT = SCRATCHPAD_BYTES / LINE_BYTES;
constexpr std::uint64_t LINE_MASK = LINE_COUNT - 1;
constexpr std::size_t BIND_BLOCK_COUNT = SCRATCHPAD_BYTES / BIND_BLOCK_BYTES;
static_assert(LINE_COUNT == 262144 && BIND_BLOCK_COUNT == 4096);

constexpr std::string_view STATE_DOMAIN_0{"MercaHash-v2/state/0"};
constexpr std::string_view STATE_DOMAIN_1{"MercaHash-v2/state/1"};
constexpr std::string_view CHECKPOINT_DOMAIN{"MercaHash-v2/checkpoint"};
constexpr std::string_view FINAL_DOMAIN{"MercaHash-v2/final"};

constexpr std::array<std::uint64_t, 8> CONSTANTS{
    0x9E3779B97F4A7C15ULL,
    0xBF58476D1CE4E5B9ULL,
    0x94D049BB133111EBULL,
    0xD6E8FEB86659FD93ULL,
    0xA0761D6478BD642FULL,
    0xE7037ED1A0B428DBULL,
    0x8EBC6AF09C88C6E3ULL,
    0x589965CC75374CC3ULL,
};

constexpr std::array<int, 8> ROTATION_1{
    13, 29, 43, 57, 17, 31, 47, 59,
};

constexpr std::array<int, 8> ROTATION_2{
    37, 53, 11, 23, 41, 7, 19, 61,
};

constexpr std::array<int, 8> CHECKPOINT_ROTATION{
    7, 19, 31, 43, 11, 23, 37, 53,
};

using State = std::array<std::uint64_t, 16>;

std::span<const unsigned char> AsBytes(std::string_view str)
{
    return {
        reinterpret_cast<const unsigned char*>(str.data()),
        str.size(),
    };
}

State InitialState(std::span<const unsigned char> header)
{
    assert(header.size() == HEADER_SIZE);

    std::array<unsigned char, SHA3_512::OUTPUT_SIZE> digest0{};
    std::array<unsigned char, SHA3_512::OUTPUT_SIZE> digest1{};

    SHA3_512{}
        .Write(AsBytes(STATE_DOMAIN_0))
        .Write(header)
        .Finalize(digest0);

    SHA3_512{}
        .Write(AsBytes(STATE_DOMAIN_1))
        .Write(header)
        .Finalize(digest1);

    State state{};

    for (std::size_t i = 0; i < 8; ++i) {
        state[i] = ReadLE64(digest0.data() + i * sizeof(std::uint64_t));
        state[i + 8] = ReadLE64(digest1.data() + i * sizeof(std::uint64_t));
    }

    return state;
}

inline void G(
    std::uint64_t& a,
    std::uint64_t& b,
    std::uint64_t& c,
    std::uint64_t& d)
{
    a += b;
    d = std::rotl(d ^ a, 32);

    c += d;
    b = std::rotl(b ^ c, 24);

    a += b;
    d = std::rotl(d ^ a, 16);

    c += d;
    b = std::rotl(b ^ c, 63);
}

inline void MercaARXColumns(State& state)
{
    G(state[0], state[4], state[8],  state[12]);
    G(state[1], state[5], state[9],  state[13]);
    G(state[2], state[6], state[10], state[14]);
    G(state[3], state[7], state[11], state[15]);
}

inline void MercaARXDiagonals(State& state)
{
    G(state[0], state[5], state[10], state[15]);
    G(state[1], state[6], state[11], state[12]);
    G(state[2], state[7], state[8],  state[13]);
    G(state[3], state[4], state[9],  state[14]);
}

inline void MercaARXRound(State& state)
{
    MercaARXColumns(state);
    MercaARXDiagonals(state);
}

using Line = std::array<std::uint64_t, LINE_WORDS>;

void WriteLine(
    std::span<unsigned char> scratchpad,
    std::size_t line_index,
    const Line& line)
{
    assert(line_index < LINE_COUNT);

    const std::size_t offset = line_index * LINE_BYTES;

    for (std::size_t j = 0; j < LINE_WORDS; ++j) {
        WriteLE64(
            scratchpad.data() + offset + j * sizeof(std::uint64_t),
            line[j]);
    }
}


#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__)) && !defined(MERCAHASH_FORCE_SCALAR)
inline bool HasAVX2()
{
#if defined(__APPLE__)
    // Cross-built Darwin does not supply the compiler's __cpu_model runtime.
    // As in SHA256AutoDetect, require CPU support and OS-enabled XMM/YMM state.
    static const bool supported = [] {
        uint32_t a, b, c, d;
        GetCPUID(0, 0, a, b, c, d);
        if (a < 7) return false;
        GetCPUID(1, 0, a, b, c, d);
        if ((c & ((1U << 27) | (1U << 28))) != ((1U << 27) | (1U << 28))) return false;
        GetCPUID(7, 0, a, b, c, d);
        if (!(b & (1U << 5))) return false;
        __asm__("xgetbv" : "=a"(a), "=d"(d) : "c"(0));
        return (a & 6) == 6;
    }();
    return supported;
#else
    return __builtin_cpu_supports("avx2");
#endif
}

// Named target functions avoid GCC's untargeted noncapturing-lambda thunk ABI.
__attribute__((target("avx2")))
inline __m256i LoadAVX2(const uint64_t* p)
{
    return _mm256_loadu_si256(reinterpret_cast<const __m256i_u*>(p));
}

__attribute__((target("avx2")))
inline __m256i RotateLeftAVX2(__m256i x, int n)
{
    return _mm256_or_si256(_mm256_slli_epi64(x, n), _mm256_srli_epi64(x, 64 - n));
}

__attribute__((target("avx2")))
inline __m256i RotateLeftVariableAVX2(__m256i x, __m256i n)
{
    return _mm256_or_si256(_mm256_sllv_epi64(x, n), _mm256_srlv_epi64(x, _mm256_sub_epi64(_mm256_set1_epi64x(64), n)));
}

__attribute__((target("avx2")))
void FillScratchpadAVX2(State& state, std::span<unsigned char> scratchpad)
{
    auto g=[&](__m256i& a,__m256i& b,__m256i& c,__m256i& d) __attribute__((target("avx2"))) {
        a=_mm256_add_epi64(a,b);d=RotateLeftAVX2(_mm256_xor_si256(d,a),32);
        c=_mm256_add_epi64(c,d);b=RotateLeftAVX2(_mm256_xor_si256(b,c),24);
        a=_mm256_add_epi64(a,b);d=RotateLeftAVX2(_mm256_xor_si256(d,a),16);
        c=_mm256_add_epi64(c,d);b=RotateLeftAVX2(_mm256_xor_si256(b,c),63);
    };
    state[0]^=0x9E3779B97F4A7C15ULL;state[5]^=0xBF58476D1CE4E5B9ULL;
    state[10]^=0x94D049BB133111EBULL;state[15]^=0xD6E8FEB86659FD93ULL;
    __m256i a=LoadAVX2(state.data()),b=LoadAVX2(state.data()+4),c=LoadAVX2(state.data()+8),d=LoadAVX2(state.data()+12);
    for(uint64_t i=0;i<LINE_COUNT;i++) {
        a=_mm256_add_epi64(a,_mm256_setr_epi64x(i,0,0,0));
        b=_mm256_xor_si256(b,_mm256_setr_epi64x(0,0,0,std::rotl(i,17)));
        c=_mm256_add_epi64(c,_mm256_setr_epi64x(i^0x9E3779B97F4A7C15ULL,0,0,0));
        d=_mm256_xor_si256(d,_mm256_setr_epi64x(0,0,0,std::rotl(i,41)));
        if((i&1)==0) g(a,b,c,d);
        else {
            b=_mm256_permute4x64_epi64(b,0x39);c=_mm256_permute4x64_epi64(c,0x4e);d=_mm256_permute4x64_epi64(d,0x93);
            g(a,b,c,d);
            b=_mm256_permute4x64_epi64(b,0x93);c=_mm256_permute4x64_epi64(c,0x4e);d=_mm256_permute4x64_epi64(d,0x39);
        }
        const __m256i lo=_mm256_xor_si256(a,c),hi=_mm256_xor_si256(b,d);
        _mm256_storeu_si256(reinterpret_cast<__m256i_u*>(scratchpad.data()+i*64),lo);
        _mm256_storeu_si256(reinterpret_cast<__m256i_u*>(scratchpad.data()+i*64+32),hi);
        __m256i q=_mm256_blend_epi32(_mm256_permute4x64_epi64(lo,0xff),_mm256_permute4x64_epi64(hi,0x90),0xfc);
        q=RotateLeftVariableAVX2(q,_mm256_setr_epi64x(0,11,0,23));a=_mm256_blend_epi32(_mm256_add_epi64(a,q),_mm256_xor_si256(a,q),0xcc);
        q=_mm256_blend_epi32(_mm256_permute4x64_epi64(hi,0xff),_mm256_permute4x64_epi64(lo,0x90),0xfc);
        q=RotateLeftVariableAVX2(q,_mm256_setr_epi64x(0,37,0,53));b=_mm256_blend_epi32(_mm256_add_epi64(b,q),_mm256_xor_si256(b,q),0xcc);
        q=_mm256_blend_epi32(_mm256_permute4x64_epi64(hi,0x39),_mm256_permute4x64_epi64(lo,0),0xc0);
        q=RotateLeftVariableAVX2(q,_mm256_setr_epi64x(0,7,0,19));c=_mm256_blend_epi32(_mm256_xor_si256(c,q),_mm256_add_epi64(c,q),0xcc);
        q=_mm256_blend_epi32(_mm256_permute4x64_epi64(lo,0x39),_mm256_permute4x64_epi64(hi,0),0xc0);
        q=RotateLeftVariableAVX2(q,_mm256_setr_epi64x(0,31,0,47));d=_mm256_blend_epi32(_mm256_xor_si256(d,q),_mm256_add_epi64(d,q),0xcc);
    }
    _mm256_storeu_si256(reinterpret_cast<__m256i_u*>(state.data()),a);_mm256_storeu_si256(reinterpret_cast<__m256i_u*>(state.data()+4),b);
    _mm256_storeu_si256(reinterpret_cast<__m256i_u*>(state.data()+8),c);_mm256_storeu_si256(reinterpret_cast<__m256i_u*>(state.data()+12),d);
}
#endif

void FillScratchpad(
    State& state,
    std::span<unsigned char> scratchpad,
    ScratchpadFillMode mode)
{
    assert(scratchpad.size() == SCRATCHPAD_BYTES);
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__)) && !defined(MERCAHASH_FORCE_SCALAR)
    if (mode == ScratchpadFillMode::ALTERNATING_HALF_ROUND && HasAVX2()) { FillScratchpadAVX2(state,scratchpad); return; }
#endif

    // Domain-separated transition from initial state into scratchpad fill.
    state[0]  ^= 0x9E3779B97F4A7C15ULL;
    state[5]  ^= 0xBF58476D1CE4E5B9ULL;
    state[10] ^= 0x94D049BB133111EBULL;
    state[15] ^= 0xD6E8FEB86659FD93ULL;

    for (std::size_t i = 0; i < LINE_COUNT; ++i) {
        const std::uint64_t x = static_cast<std::uint64_t>(i);

        state[0] += x;
        state[7] ^= std::rotl(x, 17);
        state[8] += x ^ 0x9E3779B97F4A7C15ULL;
        state[15] ^= std::rotl(x, 41);

        switch (mode) {
        case ScratchpadFillMode::ALTERNATING_HALF_ROUND:
            if ((i & 1) == 0) {
                MercaARXColumns(state);
            } else {
                MercaARXDiagonals(state);
            }
            break;

        case ScratchpadFillMode::TWO_FULL_ROUNDS:
            MercaARXRound(state);
            MercaARXRound(state);
            break;
        }

        Line line{};

        for (std::size_t j = 0; j < LINE_WORDS; ++j) {
            line[j] = state[j] ^ state[j + 8];
        }

        WriteLine(scratchpad, i, line);

        // Feed the newly generated line back into the evolving state.
        state[0]  += line[3];
        state[1]  ^= std::rotl(line[4], 11);
        state[2]  += line[5];
        state[3]  ^= std::rotl(line[6], 23);

        state[4]  += line[7];
        state[5]  ^= std::rotl(line[0], 37);
        state[6]  += line[1];
        state[7]  ^= std::rotl(line[2], 53);

        state[8]  ^= line[5];
        state[9]  += std::rotl(line[6], 7);
        state[10] ^= line[7];
        state[11] += std::rotl(line[0], 19);

        state[12] ^= line[1];
        state[13] += std::rotl(line[2], 31);
        state[14] ^= line[3];
        state[15] += std::rotl(line[4], 47);
    }
}

Line ReadLine(
    std::span<const unsigned char> scratchpad,
    std::size_t line_index)
{
    assert(line_index < LINE_COUNT);

    const std::size_t offset = line_index * LINE_BYTES;
    Line line{};

    for (std::size_t j = 0; j < LINE_WORDS; ++j) {
        line[j] = ReadLE64(
            scratchpad.data() + offset + j * sizeof(std::uint64_t));
    }

    return line;
}

#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__)) && !defined(MERCAHASH_FORCE_SCALAR)
__attribute__((target("avx2")))
void BindScratchpadAVX2(
    State& state,
    std::span<unsigned char> scratchpad,
    std::uint32_t passes)
{
    assert(scratchpad.size() == SCRATCHPAD_BYTES);

    const __m256i r1lo=_mm256_setr_epi64x(13,29,43,57),r1hi=_mm256_setr_epi64x(17,31,47,59);
    const __m256i r2lo=_mm256_setr_epi64x(37,53,11,23),r2hi=_mm256_setr_epi64x(41,7,19,61);
    for (std::uint64_t pass = 0; pass < passes; ++pass) {
        state[0] ^= pass + CONSTANTS[4];
        state[8] += (pass + 1) * CONSTANTS[5];
        MercaARXRound(state);

        for (std::size_t block = 0; block < BIND_BLOCK_COUNT; ++block) {
            const std::size_t base_line =
                block * BIND_BLOCK_LINES;

            const Line head =
                ReadLine(scratchpad, base_line);

            const std::uint64_t block_u64 =
                static_cast<std::uint64_t>(block);

            const std::uint64_t reference_seed =
                state[0] ^
                std::rotl(state[7], 19) ^
                state[13] ^
                head[0] ^
                std::rotl(head[7], 31) ^
                (block_u64 * CONSTANTS[1]) ^
                ((pass + 1) * CONSTANTS[6]);

            const std::size_t previous_block =
                block == 0
                    ? BIND_BLOCK_COUNT - 1
                    : block - 1;

            const std::size_t reference_block =
                block == 0
                    ? BIND_BLOCK_COUNT - 1
                    : static_cast<std::size_t>(
                          reference_seed % block_u64);

            if (block != 0) {
                assert(reference_block < block);


            }

            const std::size_t reference_rotation =
                static_cast<std::size_t>(
                    (reference_seed >> 17) &
                    (BIND_BLOCK_LINES - 1));

            Line block_accumulator{};
            __m256i acc0=_mm256_setzero_si256(),acc1=_mm256_setzero_si256();
            std::array<uint64_t,32> window;
            for (size_t k=0;k<32;k++) window[k]=state[k&15];

            for (std::size_t line = 0;
                 line < BIND_BLOCK_LINES;
                 ++line) {
                const std::size_t current_line =
                    base_line + line;

                const std::size_t previous_line =
                    previous_block * BIND_BLOCK_LINES +
                    ((line + BIND_BLOCK_LINES - 1) &
                     (BIND_BLOCK_LINES - 1));

                const std::size_t reference_line =
                    reference_block * BIND_BLOCK_LINES +
                    ((line + reference_rotation) &
                     (BIND_BLOCK_LINES - 1));

                const Line current =
                    ReadLine(scratchpad, current_line);

                const Line previous =
                    ReadLine(scratchpad, previous_line);

                const Line reference =
                    ReadLine(scratchpad, reference_line);

                const std::uint64_t line_tag =
                    block_u64 ^
                    (static_cast<std::uint64_t>(line) *
                     CONSTANTS[0]) ^
                    (pass * CONSTANTS[3]);

                const __m256i cur0=_mm256_loadu_si256(reinterpret_cast<const __m256i_u*>(current.data()));
                const __m256i cur1=_mm256_loadu_si256(reinterpret_cast<const __m256i_u*>(current.data()+4));
                const __m256i p0=_mm256_loadu_si256(reinterpret_cast<const __m256i_u*>(previous.data()));
                const __m256i p1=_mm256_loadu_si256(reinterpret_cast<const __m256i_u*>(previous.data()+4));
                const __m256i p01=_mm256_permute2x128_si256(p0,p1,0x21),p10=_mm256_permute2x128_si256(p1,p0,0x21);
                const __m256i prev0=_mm256_alignr_epi8(p01,p0,8),prev1=_mm256_alignr_epi8(p10,p1,8);
                const __m256i q0=_mm256_loadu_si256(reinterpret_cast<const __m256i_u*>(reference.data()));
                const __m256i q1=_mm256_loadu_si256(reinterpret_cast<const __m256i_u*>(reference.data()+4));
                const __m256i q01=_mm256_permute2x128_si256(q0,q1,0x21),q10=_mm256_permute2x128_si256(q1,q0,0x21);
                const __m256i ref0=_mm256_alignr_epi8(q1,q01,8),ref1=_mm256_alignr_epi8(q0,q10,8);
                const __m256i st0=_mm256_loadu_si256(reinterpret_cast<const __m256i_u*>(window.data()+(line&15)));
                const __m256i st1=_mm256_loadu_si256(reinterpret_cast<const __m256i_u*>(window.data()+(line&15)+4));
                const __m256i tag=_mm256_set1_epi64x(line_tag);
                const __m256i bound0=_mm256_xor_si256(_mm256_xor_si256(_mm256_add_epi64(cur0,prev0),RotateLeftVariableAVX2(_mm256_add_epi64(ref0,st0),r1lo)),RotateLeftVariableAVX2(tag,r2lo));
                const __m256i bound1=_mm256_xor_si256(_mm256_xor_si256(_mm256_add_epi64(cur1,prev1),RotateLeftVariableAVX2(_mm256_add_epi64(ref1,st1),r1hi)),RotateLeftVariableAVX2(tag,r2hi));
                Line bound;
                _mm256_storeu_si256(reinterpret_cast<__m256i_u*>(bound.data()),bound0);
                _mm256_storeu_si256(reinterpret_cast<__m256i_u*>(bound.data()+4),bound1);
                WriteLine(scratchpad,current_line,bound);
                const __m256i b01=_mm256_permute2x128_si256(bound0,bound1,0x21),b10=_mm256_permute2x128_si256(bound1,bound0,0x21);
                const __m256i next0=_mm256_alignr_epi8(b01,bound0,8),next1=_mm256_alignr_epi8(b10,bound1,8);
                acc0=_mm256_xor_si256(acc0,_mm256_add_epi64(bound0,RotateLeftVariableAVX2(next0,r2lo)));
                acc1=_mm256_xor_si256(acc1,_mm256_add_epi64(bound1,RotateLeftVariableAVX2(next1,r2hi)));
            }

            _mm256_storeu_si256(reinterpret_cast<__m256i_u*>(block_accumulator.data()),acc0);
            _mm256_storeu_si256(reinterpret_cast<__m256i_u*>(block_accumulator.data()+4),acc1);
            for (std::size_t j = 0; j < LINE_WORDS; ++j) {
                state[j] ^=
                    block_accumulator[j] +
                    reference_seed;

                state[j + 8] +=
                    std::rotl(
                        block_accumulator[(j + 3) & 7],
                        ROTATION_1[j]) ^
                    block_u64;
            }

            if ((block + 1) % BIND_ARX_INTERVAL == 0) {
                MercaARXRound(state);
            }
        }

        MercaARXRound(state);
    }
}
#endif

void BindScratchpad(
    State& state,
    std::span<unsigned char> scratchpad,
    std::uint32_t passes)
{
    assert(scratchpad.size() == SCRATCHPAD_BYTES);
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__)) && !defined(MERCAHASH_FORCE_SCALAR)
    if (HasAVX2()) { BindScratchpadAVX2(state,scratchpad,passes); return; }
#endif

    for (std::uint64_t pass = 0; pass < passes; ++pass) {
        state[0] ^= pass + CONSTANTS[4];
        state[8] += (pass + 1) * CONSTANTS[5];
        MercaARXRound(state);

        for (std::size_t block = 0; block < BIND_BLOCK_COUNT; ++block) {
            const std::size_t base_line =
                block * BIND_BLOCK_LINES;

            const Line head =
                ReadLine(scratchpad, base_line);

            const std::uint64_t block_u64 =
                static_cast<std::uint64_t>(block);

            const std::uint64_t reference_seed =
                state[0] ^
                std::rotl(state[7], 19) ^
                state[13] ^
                head[0] ^
                std::rotl(head[7], 31) ^
                (block_u64 * CONSTANTS[1]) ^
                ((pass + 1) * CONSTANTS[6]);

            const std::size_t previous_block =
                block == 0
                    ? BIND_BLOCK_COUNT - 1
                    : block - 1;

            const std::size_t reference_block =
                block == 0
                    ? BIND_BLOCK_COUNT - 1
                    : static_cast<std::size_t>(
                          reference_seed % block_u64);

            if (block != 0) {
                assert(reference_block < block);


            }

            const std::size_t reference_rotation =
                static_cast<std::size_t>(
                    (reference_seed >> 17) &
                    (BIND_BLOCK_LINES - 1));

            Line block_accumulator{};

            for (std::size_t line = 0;
                 line < BIND_BLOCK_LINES;
                 ++line) {
                const std::size_t current_line =
                    base_line + line;

                const std::size_t previous_line =
                    previous_block * BIND_BLOCK_LINES +
                    ((line + BIND_BLOCK_LINES - 1) &
                     (BIND_BLOCK_LINES - 1));

                const std::size_t reference_line =
                    reference_block * BIND_BLOCK_LINES +
                    ((line + reference_rotation) &
                     (BIND_BLOCK_LINES - 1));

                const Line current =
                    ReadLine(scratchpad, current_line);

                const Line previous =
                    ReadLine(scratchpad, previous_line);

                const Line reference =
                    ReadLine(scratchpad, reference_line);

                const std::uint64_t line_tag =
                    block_u64 ^
                    (static_cast<std::uint64_t>(line) *
                     CONSTANTS[0]) ^
                    (pass * CONSTANTS[3]);

                Line bound{};

                for (std::size_t j = 0; j < LINE_WORDS; ++j) {
                    const std::uint64_t x =
                        current[j] +
                        previous[(j + 1) & 7];

                    const std::uint64_t y =
                        reference[(j + 3) & 7] +
                        state[(j + line) & 15];

                    bound[j] =
                        x ^
                        std::rotl(y, ROTATION_1[j]) ^
                        std::rotl(
                            line_tag,
                            ROTATION_2[j]);
                }

                WriteLine(scratchpad, current_line, bound);

                for (std::size_t j = 0; j < LINE_WORDS; ++j) {
                    block_accumulator[j] ^=
                        bound[j] +
                        std::rotl(
                            bound[(j + 1) & 7],
                            ROTATION_2[j]);
                }
            }

            for (std::size_t j = 0; j < LINE_WORDS; ++j) {
                state[j] ^=
                    block_accumulator[j] +
                    reference_seed;

                state[j + 8] +=
                    std::rotl(
                        block_accumulator[(j + 3) & 7],
                        ROTATION_1[j]) ^
                    block_u64;
            }

            if ((block + 1) % BIND_ARX_INTERVAL == 0) {
                MercaARXRound(state);
            }
        }

        MercaARXRound(state);
    }
}

struct PrimaryResult {
    std::size_t line_index;
    Line written;
};

PrimaryResult PrimaryStep(
    State& state,
    std::span<unsigned char> scratchpad,
    std::uint64_t step)
{
    const std::uint64_t primary_seed =
        state[0] ^
        std::rotl(state[5], 17) ^
        state[10] ^
        std::rotl(state[15], 41) ^
        (step * CONSTANTS[0]);

    const std::size_t primary_index =
        static_cast<std::size_t>(primary_seed & LINE_MASK);

    const Line memory = ReadLine(scratchpad, primary_index);

    for (std::size_t j = 0; j < LINE_WORDS; ++j) {
        const std::uint64_t x =
            memory[j] ^
            std::rotl(state[j + 8], ROTATION_1[j]) ^
            (step * CONSTANTS[j]);

        state[j] += x;
    }

    for (std::size_t j = 0; j < LINE_WORDS; ++j) {
        const std::uint64_t y =
            state[j] +
            memory[(j + 3) & 7] +
            std::rotl(state[(j + 5) & 7], ROTATION_2[j]);

        state[j + 8] =
            std::rotl(
                state[j + 8] ^ y,
                ROTATION_1[(j + 3) & 7]);
    }

    Line written{};

    for (std::size_t j = 0; j < LINE_WORDS; ++j) {
        written[j] =
            memory[j] ^
            state[j] ^
            std::rotl(state[j + 8], ROTATION_2[j]) ^
            (step * CONSTANTS[(j + 1) & 7]);
    }

    WriteLine(scratchpad, primary_index, written);

    state[0]  ^= written[5];
    state[1]  += std::rotl(written[6], 9);
    state[2]  ^= written[7];
    state[3]  += std::rotl(written[0], 21);

    state[4]  ^= written[1];
    state[5]  += std::rotl(written[2], 33);
    state[6]  ^= written[3];
    state[7]  += std::rotl(written[4], 45);

    state[8]  += written[3];
    state[9]  ^= std::rotl(written[4], 15);
    state[10] += written[5];
    state[11] ^= std::rotl(written[6], 27);

    state[12] += written[7];
    state[13] ^= std::rotl(written[0], 39);
    state[14] += written[1];
    state[15] ^= std::rotl(written[2], 51);

    return {primary_index, written};
}

struct SecondaryResult {
    std::size_t line_index;
    Line written;
    bool forced_distinct;
};

SecondaryResult SecondaryStep(
    State& state,
    std::span<unsigned char> scratchpad,
    std::uint64_t step,
    const PrimaryResult& primary)
{
    const std::uint64_t secondary_seed =
        state[2] ^
        std::rotl(state[7], 23) ^
        state[9] ^
        std::rotl(state[14], 37) ^
        primary.written[step & 7] ^
        (step * CONSTANTS[3]);

    std::size_t secondary_index =
        static_cast<std::size_t>(secondary_seed & LINE_MASK);

    const bool forced_distinct =
        secondary_index == primary.line_index;

    if (forced_distinct) {
        secondary_index ^= 1U;
    }

    const Line memory = ReadLine(scratchpad, secondary_index);

    for (std::size_t j = 0; j < LINE_WORDS; ++j) {
        const std::uint64_t z =
            memory[j] +
            state[(j + 11) & 15] +
            (step ^ CONSTANTS[(j + 5) & 7]);

        state[j + 8] += std::rotl(z, ROTATION_2[j]);
    }

    for (std::size_t j = 0; j < LINE_WORDS; ++j) {
        state[j] =
            std::rotl(
                state[j] ^
                memory[(j + 5) & 7] ^
                state[j + 8],
                ROTATION_1[j]);
    }

    Line written{};

    for (std::size_t j = 0; j < LINE_WORDS; ++j) {
        written[j] =
            memory[j] +
            state[j] +
            std::rotl(state[j + 8], ROTATION_1[(j + 2) & 7]) +
            (step * CONSTANTS[(j + 6) & 7]);
    }

    WriteLine(scratchpad, secondary_index, written);

    state[0]  += written[7];
    state[1]  ^= std::rotl(written[0], 13);
    state[2]  += written[1];
    state[3]  ^= std::rotl(written[2], 25);

    state[4]  += written[3];
    state[5]  ^= std::rotl(written[4], 35);
    state[6]  += written[5];
    state[7]  ^= std::rotl(written[6], 49);

    state[8]  ^= written[2];
    state[9]  += std::rotl(written[3], 17);
    state[10] ^= written[4];
    state[11] += std::rotl(written[5], 29);

    state[12] ^= written[6];
    state[13] += std::rotl(written[7], 43);
    state[14] ^= written[0];
    state[15] += std::rotl(written[1], 55);

    return {secondary_index, written, forced_distinct};
}

void CrossLaneMix(State& state, std::uint64_t step)
{
    state[0] ^= step;
    state[8] += step * CONSTANTS[2];

    MercaARXRound(state);
    MercaARXRound(state);
}

void WriteHashUint64(SHA3_512& hasher, std::uint64_t value)
{
    std::array<unsigned char, sizeof(std::uint64_t)> encoded{};
    WriteLE64(encoded.data(), value);
    hasher.Write(encoded);
}

void Checkpoint(
    State& state,
    std::uint64_t step,
    const PrimaryResult& primary,
    const SecondaryResult& secondary)
{
    SHA3_512 hasher;

    hasher.Write(AsBytes(CHECKPOINT_DOMAIN));

    WriteHashUint64(hasher, step);
    WriteHashUint64(hasher, primary.line_index);
    WriteHashUint64(hasher, secondary.line_index);

    for (const std::uint64_t value : state) {
        WriteHashUint64(hasher, value);
    }

    for (const std::uint64_t value : primary.written) {
        WriteHashUint64(hasher, value);
    }

    for (const std::uint64_t value : secondary.written) {
        WriteHashUint64(hasher, value);
    }

    std::array<unsigned char, SHA3_512::OUTPUT_SIZE> digest{};
    hasher.Finalize(digest);

    for (std::size_t j = 0; j < LINE_WORDS; ++j) {
        const std::uint64_t value =
            ReadLE64(digest.data() + j * sizeof(std::uint64_t));

        state[j] ^= value;
        state[j + 8] +=
            std::rotl(
                ReadLE64(
                    digest.data() +
                    ((j + 3) & 7) * sizeof(std::uint64_t)),
                CHECKPOINT_ROTATION[j]);
    }

    MercaARXRound(state);
}

[[maybe_unused]] void MixScratchpad(
    State& state,
    std::span<unsigned char> scratchpad,
    std::uint32_t mix_steps)
{
    for (std::uint64_t step = 1; step <= mix_steps; ++step) {
        const PrimaryResult primary =
            PrimaryStep(state, scratchpad, step);

        if (step % SECONDARY_INTERVAL == 0) {
            const SecondaryResult secondary =
                SecondaryStep(state, scratchpad, step, primary);

            if (step % CROSS_LANE_INTERVAL == 0) {
                CrossLaneMix(state, step);
            }

            if (step % CHECKPOINT_INTERVAL == 0) {
                Checkpoint(state, step, primary, secondary);
            }
        }
    }
}

void FinalSample(
    State& state,
    std::span<const unsigned char> scratchpad)
{
    for (std::uint64_t sample = 0; sample < FINAL_READS; ++sample) {
        const std::uint64_t sample_seed =
            state[0] ^
            std::rotl(state[5], 17) ^
            state[10] ^
            std::rotl(state[15], 41) ^
            (sample * CONSTANTS[0]);

        const std::size_t sample_index =
            static_cast<std::size_t>(sample_seed & LINE_MASK);

        const Line memory = ReadLine(scratchpad, sample_index);

        for (std::size_t j = 0; j < LINE_WORDS; ++j) {
            const std::uint64_t x =
                memory[j] ^
                std::rotl(state[j + 8], ROTATION_1[j]) ^
                (sample + CONSTANTS[j]);

            state[j] += x;
        }

        for (std::size_t j = 0; j < LINE_WORDS; ++j) {
            state[j + 8] =
                std::rotl(
                    state[j + 8] ^
                    state[j] ^
                    memory[(j + 3) & 7],
                    ROTATION_2[j]);
        }

        state[0] ^= static_cast<std::uint64_t>(sample_index);
        state[8] +=
            std::rotl(
                static_cast<std::uint64_t>(sample_index),
                29);

        if ((sample + 1) % 16 == 0) {
            MercaARXRound(state);
        }
    }
}

void WriteHashUint64(SHA3_256& hasher, std::uint64_t value)
{
    std::array<unsigned char, sizeof(std::uint64_t)> encoded{};
    WriteLE64(encoded.data(), value);
    hasher.Write(encoded);
}

void FinalizeHash(
    const State& state,
    std::span<const unsigned char> header,
    std::span<unsigned char> output)
{
    SHA3_256 hasher;

    hasher.Write(AsBytes(FINAL_DOMAIN));
    hasher.Write(header);

    for (const std::uint64_t value : state) {
        WriteHashUint64(hasher, value);
    }

    hasher.Finalize(output);
}

} // namespace

} // namespace mercahash

#include <mining/mercahash_cpu_batch.h>
#include <algorithm>
namespace mercahash::cpu2 {
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__)) && !defined(CPU2_FORCE_SCALAR) && !defined(MERCAHASH_FORCE_SCALAR)
// Preserve vector constant bits on C++20 toolchains without std::bit_cast.
// memcpy avoids relying on unsigned-to-signed conversion behavior.
static int64_t SignedBits(uint64_t value)
{
    int64_t result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}
#endif
static void Validate(std::span<Job> jobs,size_t capacity) {
 if(jobs.empty() || jobs.size()>capacity)throw std::invalid_argument("batch exceeds capacity");
 for(const auto& j:jobs)if(j.header.size()!=80||j.scratch.size()!=V2_SCRATCHPAD_BYTES||j.output.size()!=32)throw std::invalid_argument("invalid batch buffer size");
 auto overlap=[](const void* a,size_t n,const void* b,size_t m){const auto x=reinterpret_cast<uintptr_t>(a),y=reinterpret_cast<uintptr_t>(b);return x<=y?y-x<n:x-y<m;};
 for(size_t a=0;a<jobs.size();a++)for(size_t b=0;b<jobs.size();b++) {
  if(a!=b && overlap(jobs[a].scratch.data(),jobs[a].scratch.size(),jobs[b].scratch.data(),jobs[b].scratch.size()))throw std::invalid_argument("scratchpads overlap");
  if(overlap(jobs[a].scratch.data(),jobs[a].scratch.size(),jobs[b].header.data(),80)||overlap(jobs[a].scratch.data(),jobs[a].scratch.size(),jobs[b].output.data(),32)||overlap(jobs[a].output.data(),32,jobs[b].header.data(),80)||(a!=b&&overlap(jobs[a].output.data(),32,jobs[b].output.data(),32)))throw std::invalid_argument("batch buffers overlap");
 }
}
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__)) && !defined(CPU2_FORCE_SCALAR) && !defined(MERCAHASH_FORCE_SCALAR)
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
__attribute__((target("avx2")))
static void FillAcross4Streamed(std::array<State,4>& states,std::span<Job> jobs) {
 using V=__m256i;V s[16],line[8];
#define CPU2_ROL(x,n) _mm256_or_si256(_mm256_slli_epi64((x),(n)),_mm256_srli_epi64((x),64-(n)))
for(size_t j=0;j<16;j++)s[j]=_mm256_setr_epi64x(SignedBits(states[0][j]),SignedBits(states[1][j]),SignedBits(states[2][j]),SignedBits(states[3][j]));
s[0]=_mm256_xor_si256(s[0],_mm256_set1_epi64x(SignedBits(uint64_t(CONSTANTS[0]))));
s[5]=_mm256_xor_si256(s[5],_mm256_set1_epi64x(SignedBits(uint64_t(CONSTANTS[1]))));
s[10]=_mm256_xor_si256(s[10],_mm256_set1_epi64x(SignedBits(uint64_t(CONSTANTS[2]))));
s[15]=_mm256_xor_si256(s[15],_mm256_set1_epi64x(SignedBits(uint64_t(CONSTANTS[3]))));
#define CPU2_G(a,b,c,d) do{ a=_mm256_add_epi64(a,b);d=CPU2_ROL(_mm256_xor_si256(d,a),32);c=_mm256_add_epi64(c,d);b=CPU2_ROL(_mm256_xor_si256(b,c),24);a=_mm256_add_epi64(a,b);d=CPU2_ROL(_mm256_xor_si256(d,a),16);c=_mm256_add_epi64(c,d);b=CPU2_ROL(_mm256_xor_si256(b,c),63);}while(false)
 for(uint64_t i=0;i<V2_SCRATCHPAD_BYTES/LINE_BYTES;i++){
 s[0]=_mm256_add_epi64(s[0],_mm256_set1_epi64x(SignedBits(uint64_t(i))));s[7]=_mm256_xor_si256(s[7],_mm256_set1_epi64x(SignedBits(uint64_t(std::rotl(i,17)))));s[8]=_mm256_add_epi64(s[8],_mm256_set1_epi64x(SignedBits(uint64_t(i^CONSTANTS[0]))));s[15]=_mm256_xor_si256(s[15],_mm256_set1_epi64x(SignedBits(uint64_t(std::rotl(i,41)))));
 if((i&1)==0){CPU2_G(s[0],s[4],s[8],s[12]);CPU2_G(s[1],s[5],s[9],s[13]);CPU2_G(s[2],s[6],s[10],s[14]);CPU2_G(s[3],s[7],s[11],s[15]);}
 else{CPU2_G(s[0],s[5],s[10],s[15]);CPU2_G(s[1],s[6],s[11],s[12]);CPU2_G(s[2],s[7],s[8],s[13]);CPU2_G(s[3],s[4],s[9],s[14]);}
 for(size_t j=0;j<8;j++)line[j]=_mm256_xor_si256(s[j],s[j+8]);
for(size_t j=0;j<8;j+=4){
const V t0=_mm256_unpacklo_epi64(line[j+0],line[j+1]);
const V t1=_mm256_unpackhi_epi64(line[j+0],line[j+1]);
const V t2=_mm256_unpacklo_epi64(line[j+2],line[j+3]);
const V t3=_mm256_unpackhi_epi64(line[j+2],line[j+3]);
_mm256_stream_si256(reinterpret_cast<__m256i*>(jobs[0].scratch.data()+i*64+j*8),_mm256_permute2x128_si256(t0,t2,0x20));
_mm256_stream_si256(reinterpret_cast<__m256i*>(jobs[1].scratch.data()+i*64+j*8),_mm256_permute2x128_si256(t1,t3,0x20));
_mm256_stream_si256(reinterpret_cast<__m256i*>(jobs[2].scratch.data()+i*64+j*8),_mm256_permute2x128_si256(t0,t2,0x31));
_mm256_stream_si256(reinterpret_cast<__m256i*>(jobs[3].scratch.data()+i*64+j*8),_mm256_permute2x128_si256(t1,t3,0x31));
}
s[0]=_mm256_add_epi64(s[0],line[3]);
s[1]=_mm256_xor_si256(s[1],CPU2_ROL(line[4],11));
s[2]=_mm256_add_epi64(s[2],line[5]);
s[3]=_mm256_xor_si256(s[3],CPU2_ROL(line[6],23));
s[4]=_mm256_add_epi64(s[4],line[7]);
s[5]=_mm256_xor_si256(s[5],CPU2_ROL(line[0],37));
s[6]=_mm256_add_epi64(s[6],line[1]);
s[7]=_mm256_xor_si256(s[7],CPU2_ROL(line[2],53));
s[8]=_mm256_xor_si256(s[8],line[5]);
s[9]=_mm256_add_epi64(s[9],CPU2_ROL(line[6],7));
s[10]=_mm256_xor_si256(s[10],line[7]);
s[11]=_mm256_add_epi64(s[11],CPU2_ROL(line[0],19));
s[12]=_mm256_xor_si256(s[12],line[1]);
s[13]=_mm256_add_epi64(s[13],CPU2_ROL(line[2],31));
s[14]=_mm256_xor_si256(s[14],line[3]);
s[15]=_mm256_add_epi64(s[15],CPU2_ROL(line[4],47));
}
for(size_t j=0;j<16;j++){uint64_t temp[4];_mm256_storeu_si256(reinterpret_cast<__m256i_u*>(temp),s[j]);states[0][j]=temp[0];states[1][j]=temp[1];states[2][j]=temp[2];states[3][j]=temp[3];}
#undef CPU2_G
#undef CPU2_ROL
    _mm_mfence(); // Complete streaming writes before binding reads any pad.
}
#endif
__attribute__((target("avx2")))
static void FillAcross4(std::array<State,4>& states,std::span<Job> jobs) {
 using V=__m256i;V s[16],line[8];
#define CPU2_ROL(x,n) _mm256_or_si256(_mm256_slli_epi64((x),(n)),_mm256_srli_epi64((x),64-(n)))
for(size_t j=0;j<16;j++)s[j]=_mm256_setr_epi64x(SignedBits(states[0][j]),SignedBits(states[1][j]),SignedBits(states[2][j]),SignedBits(states[3][j]));
s[0]=_mm256_xor_si256(s[0],_mm256_set1_epi64x(SignedBits(uint64_t(CONSTANTS[0]))));
s[5]=_mm256_xor_si256(s[5],_mm256_set1_epi64x(SignedBits(uint64_t(CONSTANTS[1]))));
s[10]=_mm256_xor_si256(s[10],_mm256_set1_epi64x(SignedBits(uint64_t(CONSTANTS[2]))));
s[15]=_mm256_xor_si256(s[15],_mm256_set1_epi64x(SignedBits(uint64_t(CONSTANTS[3]))));
#define CPU2_G(a,b,c,d) do{ a=_mm256_add_epi64(a,b);d=CPU2_ROL(_mm256_xor_si256(d,a),32);c=_mm256_add_epi64(c,d);b=CPU2_ROL(_mm256_xor_si256(b,c),24);a=_mm256_add_epi64(a,b);d=CPU2_ROL(_mm256_xor_si256(d,a),16);c=_mm256_add_epi64(c,d);b=CPU2_ROL(_mm256_xor_si256(b,c),63);}while(false)
 for(uint64_t i=0;i<V2_SCRATCHPAD_BYTES/LINE_BYTES;i++){
 s[0]=_mm256_add_epi64(s[0],_mm256_set1_epi64x(SignedBits(uint64_t(i))));s[7]=_mm256_xor_si256(s[7],_mm256_set1_epi64x(SignedBits(uint64_t(std::rotl(i,17)))));s[8]=_mm256_add_epi64(s[8],_mm256_set1_epi64x(SignedBits(uint64_t(i^CONSTANTS[0]))));s[15]=_mm256_xor_si256(s[15],_mm256_set1_epi64x(SignedBits(uint64_t(std::rotl(i,41)))));
 if((i&1)==0){CPU2_G(s[0],s[4],s[8],s[12]);CPU2_G(s[1],s[5],s[9],s[13]);CPU2_G(s[2],s[6],s[10],s[14]);CPU2_G(s[3],s[7],s[11],s[15]);}
 else{CPU2_G(s[0],s[5],s[10],s[15]);CPU2_G(s[1],s[6],s[11],s[12]);CPU2_G(s[2],s[7],s[8],s[13]);CPU2_G(s[3],s[4],s[9],s[14]);}
 for(size_t j=0;j<8;j++)line[j]=_mm256_xor_si256(s[j],s[j+8]);
for(size_t j=0;j<8;j+=4){
const V t0=_mm256_unpacklo_epi64(line[j+0],line[j+1]);
const V t1=_mm256_unpackhi_epi64(line[j+0],line[j+1]);
const V t2=_mm256_unpacklo_epi64(line[j+2],line[j+3]);
const V t3=_mm256_unpackhi_epi64(line[j+2],line[j+3]);
_mm256_storeu_si256(reinterpret_cast<__m256i_u*>(jobs[0].scratch.data()+i*64+j*8),_mm256_permute2x128_si256(t0,t2,0x20));
_mm256_storeu_si256(reinterpret_cast<__m256i_u*>(jobs[1].scratch.data()+i*64+j*8),_mm256_permute2x128_si256(t1,t3,0x20));
_mm256_storeu_si256(reinterpret_cast<__m256i_u*>(jobs[2].scratch.data()+i*64+j*8),_mm256_permute2x128_si256(t0,t2,0x31));
_mm256_storeu_si256(reinterpret_cast<__m256i_u*>(jobs[3].scratch.data()+i*64+j*8),_mm256_permute2x128_si256(t1,t3,0x31));
}
s[0]=_mm256_add_epi64(s[0],line[3]);
s[1]=_mm256_xor_si256(s[1],CPU2_ROL(line[4],11));
s[2]=_mm256_add_epi64(s[2],line[5]);
s[3]=_mm256_xor_si256(s[3],CPU2_ROL(line[6],23));
s[4]=_mm256_add_epi64(s[4],line[7]);
s[5]=_mm256_xor_si256(s[5],CPU2_ROL(line[0],37));
s[6]=_mm256_add_epi64(s[6],line[1]);
s[7]=_mm256_xor_si256(s[7],CPU2_ROL(line[2],53));
s[8]=_mm256_xor_si256(s[8],line[5]);
s[9]=_mm256_add_epi64(s[9],CPU2_ROL(line[6],7));
s[10]=_mm256_xor_si256(s[10],line[7]);
s[11]=_mm256_add_epi64(s[11],CPU2_ROL(line[0],19));
s[12]=_mm256_xor_si256(s[12],line[1]);
s[13]=_mm256_add_epi64(s[13],CPU2_ROL(line[2],31));
s[14]=_mm256_xor_si256(s[14],line[3]);
s[15]=_mm256_add_epi64(s[15],CPU2_ROL(line[4],47));
}
for(size_t j=0;j<16;j++){uint64_t temp[4];_mm256_storeu_si256(reinterpret_cast<__m256i_u*>(temp),s[j]);states[0][j]=temp[0];states[1][j]=temp[1];states[2][j]=temp[2];states[3][j]=temp[3];}
#undef CPU2_G
#undef CPU2_ROL
}
#endif
static inline PrimaryResult PrimaryLoaded(State& state,std::span<unsigned char> scratchpad,uint64_t step,size_t primary_index,const Line& memory)
{
    for (std::size_t j = 0; j < LINE_WORDS; ++j) {
        const std::uint64_t x =
            memory[j] ^
            std::rotl(state[j + 8], ROTATION_1[j]) ^
            (step * CONSTANTS[j]);

        state[j] += x;
    }

    for (std::size_t j = 0; j < LINE_WORDS; ++j) {
        const std::uint64_t y =
            state[j] +
            memory[(j + 3) & 7] +
            std::rotl(state[(j + 5) & 7], ROTATION_2[j]);

        state[j + 8] =
            std::rotl(
                state[j + 8] ^ y,
                ROTATION_1[(j + 3) & 7]);
    }

    Line written{};

    for (std::size_t j = 0; j < LINE_WORDS; ++j) {
        written[j] =
            memory[j] ^
            state[j] ^
            std::rotl(state[j + 8], ROTATION_2[j]) ^
            (step * CONSTANTS[(j + 1) & 7]);
    }

    WriteLine(scratchpad, primary_index, written);

    state[0]  ^= written[5];
    state[1]  += std::rotl(written[6], 9);
    state[2]  ^= written[7];
    state[3]  += std::rotl(written[0], 21);

    state[4]  ^= written[1];
    state[5]  += std::rotl(written[2], 33);
    state[6]  ^= written[3];
    state[7]  += std::rotl(written[4], 45);

    state[8]  += written[3];
    state[9]  ^= std::rotl(written[4], 15);
    state[10] += written[5];
    state[11] ^= std::rotl(written[6], 27);

    state[12] += written[7];
    state[13] ^= std::rotl(written[0], 39);
    state[14] += written[1];
    state[15] ^= std::rotl(written[2], 51);

    return {primary_index, written};
}



size_t Capacity() {return 4;}
const char* ExecutionPath() {
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__)) && !defined(CPU2_FORCE_SCALAR) && !defined(MERCAHASH_FORCE_SCALAR)
 return HasAVX2() ? "b4-streamed-huge AVX2" : "b4-streamed-huge scalar";
#else
 return "b4-streamed-huge scalar";
#endif
}
size_t Batch(std::span<Job> jobs,const std::atomic<bool>* cancelled) {
 Validate(jobs,4);if(cancelled&&cancelled->load(std::memory_order_relaxed))return 0;
 std::array<State,4> states;
 for(size_t c=0;c<jobs.size();c++)states[c]=InitialState(jobs[c].header);
 #if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__)) && !defined(CPU2_FORCE_SCALAR) && !defined(MERCAHASH_FORCE_SCALAR)
 if(jobs.size()==4&&HasAVX2()){
 bool aligned=true;for(auto& j:jobs)aligned=aligned&&((reinterpret_cast<uintptr_t>(j.scratch.data())&31)==0);
 if(aligned)FillAcross4Streamed(states,jobs);else FillAcross4(states,jobs);
 }else
#endif
 {for(size_t c=0;c<jobs.size();c++)FillScratchpad(states[c],jobs[c].scratch,ScratchpadFillMode::ALTERNATING_HALF_ROUND);}
 for(size_t c=0;c<jobs.size();c++)BindScratchpad(states[c],jobs[c].scratch,1);
for(uint64_t step=1;step<=V2_MIX_STEPS;step++){
std::array<size_t,4> indices;std::array<Line,4> memory;
 for(size_t c=0;c<jobs.size();c++){const State& s=states[c];indices[c]=(s[0]^std::rotl(s[5],17)^s[10]^std::rotl(s[15],41)^(step*CONSTANTS[0]))&(V2_SCRATCHPAD_BYTES/LINE_BYTES-1);}
 for(size_t c=0;c<jobs.size();c++)memory[c]=ReadLine(jobs[c].scratch,indices[c]);
for(size_t c=0;c<jobs.size();c++){
auto primary=PrimaryLoaded(states[c],jobs[c].scratch,step,indices[c],memory[c]);
if(step%SECONDARY_INTERVAL==0){auto secondary=SecondaryStep(states[c],jobs[c].scratch,step,primary);if(step%CROSS_LANE_INTERVAL==0)CrossLaneMix(states[c],step);if(step%CHECKPOINT_INTERVAL==0)Checkpoint(states[c],step,primary,secondary);}
}
}
for(size_t c=0;c<jobs.size();c++){FinalSample(states[c],jobs[c].scratch);FinalizeHash(states[c],jobs[c].header,jobs[c].output);}
 return jobs.size();
}
}
