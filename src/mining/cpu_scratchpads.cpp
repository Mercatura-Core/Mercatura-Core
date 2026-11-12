// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <mining/cpu_scratchpads.h>

#ifdef __linux__
#include <sys/mman.h>
#endif

namespace mining {
int CpuScratchpads::AdviseHugePages(void* data, size_t size)
{
#if defined(__linux__) && defined(MADV_HUGEPAGE)
    return madvise(data, size, MADV_HUGEPAGE);
#else
    return -1;
#endif
}

CpuScratchpads::CpuScratchpads(std::pmr::memory_resource* resource, PageAdvice advice) : m_resource{resource}
{
    try {
        for (auto& pad : m_pads) {
            pad = static_cast<unsigned char*>(m_resource->allocate(mercahash::V2_SCRATCHPAD_BYTES, ALIGNMENT));
            if (advice) (void)advice(pad, mercahash::V2_SCRATCHPAD_BYTES);
        }
    } catch (...) {
        for (auto* pad : m_pads) if (pad) m_resource->deallocate(pad, mercahash::V2_SCRATCHPAD_BYTES, ALIGNMENT);
        throw;
    }
}
CpuScratchpads::~CpuScratchpads()
{
    for (auto* pad : m_pads) if (pad) m_resource->deallocate(pad, mercahash::V2_SCRATCHPAD_BYTES, ALIGNMENT);
}
} // namespace mining
