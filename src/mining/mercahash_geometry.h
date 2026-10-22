// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_MINING_MERCAHASH_GEOMETRY_H
#define BITCOIN_MINING_MERCAHASH_GEOMETRY_H
#include <crypto/mercahash.h>
namespace mercahash {
inline constexpr std::size_t V2_LINE_COUNT{V2_SCRATCHPAD_BYTES / LINE_BYTES};
inline constexpr std::uint64_t V2_LINE_MASK{V2_LINE_COUNT - 1};
inline constexpr std::size_t V2_BIND_BLOCK_COUNT{V2_SCRATCHPAD_BYTES / BIND_BLOCK_BYTES};
static_assert(V2_LINE_COUNT == 262144 && V2_LINE_MASK == 0x3ffff && V2_BIND_BLOCK_COUNT == 4096);
constexpr std::size_t LineCount(Version version) { return ScratchpadBytes(version) / LINE_BYTES; }
constexpr std::size_t BindBlockCount(Version version) { return ScratchpadBytes(version) / BIND_BLOCK_BYTES; }
constexpr std::uint32_t MixSteps(Version version) {
    (void)ScratchpadBytes(version);
    return version == Version::V1 ? 131072 : V2_MIX_STEPS;
}
}
#endif // BITCOIN_MINING_MERCAHASH_GEOMETRY_H
