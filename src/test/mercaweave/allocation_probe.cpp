// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include "allocation_probe.h"
#include <algorithm>
#include <cstdlib>
#include <new>
namespace allocation {
bool active{false};
size_t calls{0}, bytes{0}, largest{0};
}
void* operator new(std::size_t n)
{
    void* p = std::malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    if (allocation::active) {
        ++allocation::calls;
        allocation::bytes += n;
        allocation::largest = std::max(allocation::largest, n);
    }
    return p;
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
