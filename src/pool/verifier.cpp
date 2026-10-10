// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <pool/verifier.h>

#include <crypto/mercahash.h>
#include <mining/cpu_miner.h>
#include <streams.h>

#include <array>
#include <chrono>
#include <stdexcept>

namespace pool {
Verifier::Verifier(size_t workers, size_t queue, size_t memory_mib) : m_capacity{queue}
{
    if (workers == 0 || workers > 64 || queue == 0 || queue > 4096 || memory_mib < workers * 160) throw std::invalid_argument("invalid verifier resource budget");
    if (workers > mining::DetectWorkerLimits().maximum) throw std::invalid_argument("verification workers exceed detected M2 CPU/memory budget");
    try {
        for (size_t i = 0; i < workers; ++i) {
            // Allocate before launching: constructor failures cancel/join started
            // workers. Exactly one scratchpad per worker, reused for every request.
            std::vector<unsigned char> scratch(mercahash::SCRATCHPAD_BYTES);
            m_workers.emplace_back([this, s = std::move(scratch)]() mutable { Worker(std::move(s)); });
        }
    } catch (...) {
        Stop();
        throw;
    }
}
Verifier::~Verifier() { Stop(); }
void Verifier::Stop()
{
    {
        std::lock_guard lock{m_mutex};
        m_stopping = true;
        for (auto& task : m_queue)
            task.promise.set_value({{}, true, {}});
        m_queue.clear();
    }
    m_cv.notify_all();
    for (auto& thread : m_workers)
        if (thread.joinable()) thread.join();
}
size_t Verifier::Depth()
{
    std::lock_guard lock{m_mutex};
    return m_queue.size();
}
std::optional<std::future<Verification>> Verifier::Submit(const CBlockHeader& header)
{
    std::lock_guard lock{m_mutex};
    if (m_stopping || m_queue.size() >= m_capacity) return {};
    Task task{header, {}};
    auto future = task.promise.get_future();
    m_queue.push_back(std::move(task));
    m_cv.notify_one();
    return future;
}
void Verifier::Worker(std::vector<unsigned char> scratch)
{
    while (true) {
        Task task;
        {
            std::unique_lock lock{m_mutex};
            m_cv.wait(lock, [&] { return m_stopping || !m_queue.empty(); });
            if (m_stopping) return;
            task = std::move(m_queue.front());
            m_queue.pop_front();
        }
        const auto start = std::chrono::steady_clock::now();
        Verification result;
        try {
            std::vector<unsigned char> header;
            VectorWriter{header, 0, task.header};
            if (header.size() != mercahash::HEADER_SIZE) throw std::runtime_error("noncanonical header");
            std::array<unsigned char, 32> output{};
            mercahash::HashV1(header, scratch, output);
            std::copy(output.begin(), output.end(), result.hash.begin());
        } catch (const std::exception&) {
            result.error = "MercaHash verification failed";
        }
        microseconds.fetch_add(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
        ++completed;
        task.promise.set_value(std::move(result));
    }
}
} // namespace pool
