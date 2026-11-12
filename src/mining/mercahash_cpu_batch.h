// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_MINING_MERCAHASH_CPU_BATCH_H
#define BITCOIN_MINING_MERCAHASH_CPU_BATCH_H
#include <crypto/mercahash.h>
#include <atomic>
#include <span>
namespace mercahash::cpu2 {
struct Job { std::span<const unsigned char> header;std::span<unsigned char> scratch;std::span<unsigned char> output; };
// Each job owns disjoint writable buffers. Cancellation before entry returns zero;
// an admitted batch finishes every hash and returns the actual completion count.
// Empty/oversized batches and overlapping or incorrectly sized buffers throw.
size_t Batch(std::span<Job> jobs,const std::atomic<bool>* cancelled=nullptr);
size_t Capacity();
const char* ExecutionPath();
}
#endif // BITCOIN_MINING_MERCAHASH_CPU_BATCH_H
