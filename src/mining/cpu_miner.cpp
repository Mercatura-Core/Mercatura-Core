// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.

#include <mining/cpu_miner.h>

#include <pow.h>
#include <util/threadnames.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mining {
namespace {
constexpr uint64_t NONCE_CHUNK{256};
constexpr auto JOB_LIFETIME{std::chrono::seconds{30}};
}

std::optional<NonceRange> AllocateNonceRange(std::atomic<uint64_t>& cursor)
{
    const uint64_t begin{cursor.fetch_add(NONCE_CHUNK)};
    if (begin >= NONCE_SPACE) return {};
    return NonceRange{begin, std::min(begin + NONCE_CHUNK, NONCE_SPACE)};
}

struct MiningController::Work {
    explicit Work(MiningJob work) : job{std::move(work)}, expires{job.deadline.value_or(std::chrono::steady_clock::now() + JOB_LIFETIME)} {}
    const MiningJob job;
    const std::chrono::steady_clock::time_point expires;
    std::atomic<uint64_t> next_nonce{0};
    std::atomic<bool> cancelled{false};
    std::atomic<bool> solved{false};
};

MiningController::MiningController(HashFactory factory, LimitDetector limits, RangeAllocator ranges)
    : m_hash_factory{factory ? std::move(factory) : HashFactory{[] {
          auto context{std::make_shared<PoWHashContext>()};
          return [context](const CBlockHeader& header) { return context->GetHash(header); };
      }}},
      m_detect_limits{limits ? std::move(limits) : LimitDetector{DetectWorkerLimits}},
      m_allocate_range{ranges ? std::move(ranges) : RangeAllocator{AllocateNonceRange}}
{
}
MiningController::~MiningController() { RequestStop(); Wait(); }

bool MiningController::Start(ProviderFactory factory, unsigned int workers)
{
    {
        std::lock_guard lock{m_mutex};
        if (!m_finished) return false;
        if (m_stats.state == MiningState::STARTING || m_stats.state == MiningState::RUNNING || m_stats.state == MiningState::STOPPING) return false;
    }
    // A finished coordinator has already joined its workers; reaping it is cheap.
    Wait();
    m_stop = false;
    m_finished = false;
    m_hashes = 0;
    m_active = 0;
    {
        std::lock_guard lock{m_mutex};
        m_stats = {};
        m_stats.state = MiningState::STARTING;
        m_stats.status = "Preparing mining";
        m_solutions.clear();
    }
    try {
        m_coordinator = std::thread{[this, factory = std::move(factory), workers] { Run(factory, workers); }};
    } catch (const std::exception& e) {
        Fail(e.what());
        m_finished = true;
        return false;
    }
    return true;
}

void MiningController::RequestStop()
{
    m_stop = true;
    {
        std::lock_guard lock{m_mutex};
        if (m_stats.state == MiningState::STARTING || m_stats.state == MiningState::RUNNING) {
            m_stats.state = MiningState::STOPPING;
            m_stats.status = "Stopping mining";
        }
        if (m_work) m_work->cancelled = true;
    }
    m_cv.notify_all();
    std::lock_guard provider_lock{m_provider_mutex};
    if (m_provider) m_provider->Interrupt();
}
void MiningController::Wait() { if (m_coordinator.joinable()) m_coordinator.join(); }

MiningStats MiningController::GetStats() const
{
    std::lock_guard lock{m_mutex};
    MiningStats stats{m_stats};
    stats.busy = !m_finished.load();
    stats.hashes = m_hashes.load();
    stats.active_workers = m_active.load();
    return stats;
}
void MiningController::Fail(const std::string& error)
{
    m_stop = true;
    {
        std::lock_guard lock{m_mutex};
        m_stats.state = MiningState::FAILED;
        m_stats.status = error;
    }
    RequestStop();
}
void MiningController::CancelWork()
{
    std::lock_guard lock{m_mutex};
    if (m_work) m_work->cancelled = true;
    m_work.reset();
    m_solutions.clear();
    m_cv.notify_all();
}

void MiningController::Run(ProviderFactory factory, unsigned int requested)
{
    std::vector<std::thread> workers;
    try {
        util::ThreadRename("mca-mining");
        const auto limits{m_detect_limits()};
        const unsigned int count{requested == 0 ? limits.recommended : requested};
        if (count == 0) throw std::runtime_error{"Insufficient available memory for a mining worker"};
        if (count > limits.maximum) throw std::runtime_error{"Requested worker count exceeds the current safe CPU or memory limit"};
        auto provider{factory()};
        if (!provider) throw std::runtime_error{"Mining provider is unavailable"};
        {
            std::lock_guard lock{m_mutex};
            m_stats.destination = provider->Destination();
        }
        {
            std::lock_guard lock{m_provider_mutex};
            m_provider = std::move(provider);
            if (m_stop) m_provider->Interrupt();
        }
        if (!m_stop) {
            workers.reserve(count);
            for (unsigned int i{0}; i < count && !m_stop; ++i) workers.emplace_back([this] { Worker(); });
        }
        auto sampled_at{std::chrono::steady_clock::now()};
        uint64_t sampled_hashes{0};
        while (!m_stop) {
            if (m_provider->IsCancelled()) { RequestStop(); break; }
            std::shared_ptr<Work> work;
            std::optional<CBlockHeader> solution;
            {
                std::lock_guard lock{m_mutex};
                work = m_work;
                if (!m_solutions.empty()) {
                    solution = m_solutions.front();
                    m_solutions.pop_front();
                    m_cv.notify_all();
                }
            }
            const auto now{std::chrono::steady_clock::now()};
            // The provider is used only here; its lifetime is protected until
            // workers have joined and Interrupt callers release the mutex.
            if (work && (!m_provider->IsCurrent(work->job) || now >= work->expires)) {
                CancelWork();
                work.reset();
                solution.reset();
            }
            if (work && solution && !m_stop) {
                {
                    std::lock_guard lock{m_mutex};
                    ++m_stats.submitted;
                }
                const auto result{m_provider->Submit(work->job, *solution)};
                {
                    std::lock_guard lock{m_mutex};
                    if (result.active_chain) ++m_stats.accepted;
                    if (m_stats.state != MiningState::FAILED) m_stats.status = result.message;
                }
                if (!work->job.continuous) {
                    CancelWork();
                    work.reset();
                }
            }
            bool exhausted{false};
            if (work && work->next_nonce >= NONCE_SPACE && m_active == 0) {
                std::lock_guard lock{m_mutex};
                // Headers in the FIFO still belong to this job. Drain them
                // before installing a new job and its independent nonce cursor.
                exhausted = m_solutions.empty();
            }
            if (!work || exhausted) {
                auto job{m_provider->GetJob()};
                if (m_stop) break;
                std::lock_guard lock{m_mutex};
                if (m_stop) break;
                if (job) {
                    if (job->target == arith_uint256{}) throw std::runtime_error{"Mining job has an invalid zero target"};
                    m_work = std::make_shared<Work>(std::move(*job));
                    m_stats.state = MiningState::RUNNING;
                    m_stats.status = "Mining";
                    m_cv.notify_all();
                } else {
                    m_stats.status = m_provider->WaitingStatus();
                    m_stats.state = MiningState::STARTING;
                }
            }
            if (now - sampled_at >= std::chrono::milliseconds{500}) {
                const uint64_t hashes{m_hashes.load()};
                std::lock_guard lock{m_mutex};
                m_stats.hashes_per_second = (hashes - sampled_hashes) / std::chrono::duration<double>(now - sampled_at).count();
                sampled_hashes = hashes;
                sampled_at = now;
            }
            std::unique_lock lock{m_mutex};
            m_cv.wait_for(lock, std::chrono::milliseconds{100}, [this] { return m_stop.load() || !m_solutions.empty(); });
        }
    } catch (const std::bad_alloc&) {
        Fail("Unable to allocate mining memory; reduce the worker count or free memory");
    } catch (const std::exception& e) {
        Fail(e.what());
    } catch (...) {
        Fail("Unexpected mining controller failure");
    }
    m_stop = true;
    CancelWork();
    // Join every started worker, including after partial startup or failure.
    // This runs on the coordinator, never on UI Stop, before provider teardown.
    for (auto& worker : workers) worker.join();
    workers.clear();
    {
        std::lock_guard lock{m_provider_mutex};
        m_provider.reset();
    }
    std::lock_guard lock{m_mutex};
    m_stats.hashes_per_second = 0;
    if (m_stats.state != MiningState::FAILED) {
        m_stats.state = MiningState::STOPPED;
        m_stats.status = "Stopped";
    }
    m_finished = true;
}

void MiningController::Worker()
{
    bool active{false};
    try {
        util::ThreadRename("mca-cpu-worker");
        auto hash{m_hash_factory()}; // Exactly one independent context per worker.
        if (!hash) throw std::runtime_error{"Mining hash context is unavailable"};
        std::shared_ptr<Work> previous;
        while (!m_stop) {
            std::shared_ptr<Work> work;
            {
                std::unique_lock lock{m_mutex};
                m_cv.wait(lock, [this, &previous] { return m_stop.load() || (m_work && m_work != previous); });
                if (m_stop) break;
                work = m_work;
            }
            previous = work;
            ++m_active;
            active = true;
            CBlockHeader header{work->job.header};
            while (!m_stop && !work->cancelled && !work->solved && (!work->job.valid || *work->job.valid) && std::chrono::steady_clock::now() < work->expires) {
                const auto range{m_allocate_range(work->next_nonce)};
                if (!range) break;
                for (uint64_t nonce{range->begin}; nonce < range->end; ++nonce) {
                    // Do not alter MercaHash for cancellation; finish the current
                    // attempt, then check stop, replacement and expiry.
                    if (m_stop || work->cancelled || work->solved || (work->job.valid && !*work->job.valid) || std::chrono::steady_clock::now() >= work->expires) break;
                    header.nNonce = static_cast<uint32_t>(nonce);
                    const auto result{hash(header)};
                    ++m_hashes;
                    if (UintToArith256(result) <= work->job.target) {
                        if (work->job.continuous) {
                            std::unique_lock lock{m_mutex};
                            ++m_stats.solutions;
                            // Bounded backpressure, including during slow TLS replies.
                            while (!m_stop && !work->cancelled && (!work->job.valid || *work->job.valid) && std::chrono::steady_clock::now() < work->expires && m_solutions.size() >= 64)
                                m_cv.wait_for(lock, std::chrono::milliseconds{100});
                            if (!m_stop && m_work == work && !work->cancelled && (!work->job.valid || *work->job.valid) && std::chrono::steady_clock::now() < work->expires) {
                                m_solutions.push_back(header);
                                m_cv.notify_all();
                            }
                            continue;
                        }
                        bool expected{false};
                        if (work->solved.compare_exchange_strong(expected, true)) {
                            std::lock_guard lock{m_mutex};
                            ++m_stats.solutions;
                            if (!m_stop && m_work == work && !work->cancelled) {
                                m_solutions.push_back(header);
                                m_cv.notify_all();
                            }
                        }
                        break;
                    }
                }
            }
            --m_active;
            active = false;
        }
    } catch (const std::bad_alloc&) {
        Fail("Unable to allocate a worker's MercaHash scratchpad; reduce workers or free memory");
    } catch (const std::exception& e) {
        Fail(std::string{"Mining worker failed: "} + e.what());
    } catch (...) {
        Fail("Unexpected mining worker failure");
    }
    if (active) --m_active;
}
} // namespace mining
