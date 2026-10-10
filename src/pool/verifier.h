// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_POOL_VERIFIER_H
#define BITCOIN_POOL_VERIFIER_H

#include <primitives/block.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace pool {
struct Verification {
    uint256 hash;
    bool cancelled{false};
    std::string error;
};
class Verifier
{
    struct Task {
        CBlockHeader header;
        std::promise<Verification> promise;
    };
    size_t m_capacity;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<Task> m_queue;
    bool m_stopping{false};
    std::vector<std::thread> m_workers;
    void Worker(std::vector<unsigned char> scratch);

public:
    std::atomic<uint64_t> completed{0};
    std::atomic<uint64_t> microseconds{0};
    Verifier(size_t workers, size_t queue, size_t memory_mib);
    ~Verifier();
    void Stop();
    size_t Depth();
    std::optional<std::future<Verification>> Submit(const CBlockHeader& header);
};
} // namespace pool
#endif // BITCOIN_POOL_VERIFIER_H
