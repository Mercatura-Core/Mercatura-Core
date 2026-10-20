// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.

#ifndef BITCOIN_TEST_UTIL_MERCAHASH_GPU_V2_H
#define BITCOIN_TEST_UTIL_MERCAHASH_GPU_V2_H

// Instantiate the actual portable device source with frozen V2 parameters in
// the caller's test namespace, independently of its V1 instance.
#undef BITCOIN_MINING_MERCAHASH_GPU_H
#undef MH_VERSION
#define MH_VERSION 2
#include <mining/mercahash_gpu.h>
#undef MH_VERSION

#endif // BITCOIN_TEST_UTIL_MERCAHASH_GPU_V2_H
