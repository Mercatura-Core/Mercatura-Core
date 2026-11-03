// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef MERCATURA_MERCAWEAVE_ALLOCATION_PROBE_H
#define MERCATURA_MERCAWEAVE_ALLOCATION_PROBE_H
#include <cstddef>
namespace allocation {
// Ordinary C++ new/new[] requests only, in a separate single-thread untimed
// measurement. Excludes malloc/realloc/aligned new and does not measure liveness.
extern bool active;
extern size_t calls, bytes, largest;
}
#endif
