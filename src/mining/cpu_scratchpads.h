// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_MINING_CPU_SCRATCHPADS_H
#define BITCOIN_MINING_CPU_SCRATCHPADS_H

#include <crypto/mercahash.h>

#include <array>
#include <memory_resource>
#include <span>

namespace mining {
inline constexpr size_t CPU_BATCH_CONTEXTS{4};
inline constexpr size_t CPU_V2_SCRATCHPAD_BYTES{CPU_BATCH_CONTEXTS * mercahash::V2_SCRATCHPAD_BYTES};

// Over-alignment permits optional Linux transparent huge pages and satisfies
// the champion's 32-byte streaming stores. Advice never controls correctness.
class CpuScratchpads {
    static constexpr size_t ALIGNMENT{2ULL << 20};
    std::pmr::memory_resource* const m_resource;
    std::array<unsigned char*, CPU_BATCH_CONTEXTS> m_pads{};
public:
    using PageAdvice = int (*)(void*, size_t);
    static int AdviseHugePages(void* data, size_t size);
    explicit CpuScratchpads(std::pmr::memory_resource* resource = std::pmr::get_default_resource(),
                           PageAdvice advice = AdviseHugePages);
    ~CpuScratchpads();
    CpuScratchpads(const CpuScratchpads&) = delete;
    CpuScratchpads& operator=(const CpuScratchpads&) = delete;
    std::span<unsigned char> operator[](size_t index) { return {m_pads.at(index), mercahash::V2_SCRATCHPAD_BYTES}; }
};
} // namespace mining
#endif // BITCOIN_MINING_CPU_SCRATCHPADS_H
