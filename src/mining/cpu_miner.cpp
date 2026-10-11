// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.

#include <mining/cpu_miner.h>

#include <pow.h>
#include <streams.h>
#include <util/threadnames.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <limits>
#include <set>
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
    uint64_t begin{cursor.load()};
    while (begin < NONCE_SPACE) {
        const uint64_t end{std::min(begin + NONCE_CHUNK, NONCE_SPACE)};
        if (cursor.compare_exchange_weak(begin, end)) return NonceRange{begin, end};
    }
    return {};
}

struct MiningController::Work {
    explicit Work(MiningJob work) : job{std::move(work)}, expires{job.deadline.value_or(std::chrono::steady_clock::now() + JOB_LIFETIME)} {}
    const MiningJob job;
    const std::chrono::steady_clock::time_point expires;
    std::atomic<uint64_t> next_nonce{0};
    std::atomic<bool> cancelled{false};
    std::atomic<bool> solved{false};
};

MiningController::MiningController(HashFactory factory, LimitDetector limits, RangeAllocator ranges, DeviceDetector devices, BackendFactory backends)
    : m_hash_factory{factory ? std::move(factory) : HashFactory{[] {
          auto context{std::make_shared<PoWHashContext>()};
          return [context](const CBlockHeader& header) { return context->GetHash(header); };
      }}},
      m_detect_limits{limits ? std::move(limits) : LimitDetector{DetectWorkerLimits}},
      m_allocate_range{ranges ? std::move(ranges) : RangeAllocator{AllocateNonceRange}},
      m_detect_devices{devices ? std::move(devices) : DeviceDetector{DiscoverGpuDevices}},
      m_create_backend{backends ? std::move(backends) : BackendFactory{CreateGpuBackend}}
{
}
MiningController::~MiningController() { RequestStop(); Wait(); }

bool MiningController::Start(ProviderFactory factory, unsigned int workers, GpuConfig hardware)
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
    m_cpu_hashes = 0;
    m_active = 0;
    m_ready_workers = 0;
    {
        std::lock_guard lock{m_mutex};
        m_stats = {};
        m_stats.state = MiningState::STARTING;
        m_stats.status = "Preparing mining";
        m_solutions.clear();
    }
    try {
        m_coordinator = std::thread{[this, factory = std::move(factory), workers, hardware = std::move(hardware)] { Run(factory, workers, hardware); }};
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
    stats.cpu_hashes = m_cpu_hashes.load();
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

void MiningController::Run(ProviderFactory factory, unsigned int requested, GpuConfig hardware)
{
    std::vector<std::thread> workers;
    try {
        util::ThreadRename("mca-mining");
        const auto limits{m_detect_limits()};
        const unsigned int count{hardware.cpu ? (requested == 0 ? limits.recommended : requested) : 0};
        if (hardware.cpu && count == 0) throw std::runtime_error{"Insufficient available memory for a mining worker"};
        if (count > limits.maximum) throw std::runtime_error{"Requested worker count exceeds the current safe CPU or memory limit"};
        if (!hardware.cpu && !hardware.gpu) throw std::runtime_error{"Select CPU or GPU mining hardware"};
        if (hardware.yield_ms > 1000) throw std::runtime_error{"GPU yield interval exceeds 1000 ms"};
        std::vector<GpuDevice> selected;
        if (hardware.gpu) {
            const auto discovery{m_detect_devices()};
            const std::set<std::string> ids(hardware.devices.begin(), hardware.devices.end());
            if (ids.size() != hardware.devices.size()) throw std::runtime_error{"Duplicate GPU selection"};
            for (const auto& id : ids) {
                if (std::none_of(discovery.devices.begin(), discovery.devices.end(), [&](const auto& device) { return device.id == id && device.supported; }))
                    throw std::runtime_error{"Selected GPU unavailable or unsupported: " + id};
            }
            for (const auto& device : discovery.devices) if (device.supported && (ids.empty() || ids.contains(device.id))) selected.push_back(device);
            if (selected.empty()) throw std::runtime_error{discovery.errors.empty() ? "No compatible GPU detected" : discovery.errors.front()};
        }
        // Every GPU verifier owns a separate authoritative 128 MiB CPU context.
        // Include those contexts in the host memory budget even in GPU-only mode.
        if (limits.available_memory) {
            const auto host_limits{CalculateWorkerLimits(std::numeric_limits<unsigned int>::max(), limits.available_memory)};
            if (uint64_t{count} + selected.size() > host_limits.maximum) throw std::runtime_error{"Insufficient host memory for CPU workers and GPU candidate verification"};
        }
        {
            std::lock_guard lock{m_mutex};
            for (const auto& device : selected) m_stats.devices.push_back(GpuStats{device});
        }
        m_worker_count = count + static_cast<unsigned int>(selected.size());
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
            workers.reserve(count + selected.size());
            for (unsigned int i{0}; i < count && !m_stop; ++i) workers.emplace_back([this] { Worker(); });
            for (size_t i{0}; i < selected.size() && !m_stop; ++i) workers.emplace_back([this, i, hardware] { GpuWorker(i, hardware); });
        }
        auto sampled_at{std::chrono::steady_clock::now()};
        uint64_t sampled_hashes{0}, sampled_cpu{0};
        std::vector<uint64_t> sampled_devices(selected.size(), 0);
        while (!m_stop) {
            if (m_provider->IsCancelled()) { RequestStop(); break; }
            std::shared_ptr<Work> work;
            std::optional<CBlockHeader> solution;
            {
                std::lock_guard lock{m_mutex};
                work = m_work;
                if (work && work->next_nonce < NONCE_SPACE && m_stats.state == MiningState::STARTING && m_ready_workers > 0) {
                    m_stats.state = MiningState::RUNNING;
                    m_stats.status = "Mining";
                }
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
                    m_stats.state = m_ready_workers > 0 ? MiningState::RUNNING : MiningState::STARTING;
                    m_stats.status = m_ready_workers > 0 ? "Mining" : "Preparing mining hardware";
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
                const uint64_t cpu_hashes{m_cpu_hashes.load()};
                m_stats.cpu_hashes_per_second = (cpu_hashes - sampled_cpu) / std::chrono::duration<double>(now - sampled_at).count();
                sampled_cpu = cpu_hashes;
                for (size_t i{0}; i < m_stats.devices.size(); ++i) {
                    auto& device{m_stats.devices[i]};
                    device.hashes_per_second = device.failed ? 0 : (device.hashes - sampled_devices[i]) / std::chrono::duration<double>(now - sampled_at).count();
                    sampled_devices[i] = device.hashes;
                }
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
    m_stats.cpu_hashes_per_second = 0;
    for (auto& device : m_stats.devices) { device.hashes_per_second = 0; device.memory_bytes = 0; if (!device.failed) device.status = "Stopped"; }
    if (m_stats.state != MiningState::FAILED) {
        m_stats.state = MiningState::STOPPED;
        m_stats.status = "Stopped";
    }
    m_finished = true;
}

void MiningController::Worker()
{
    bool active{false}, ready{false};
    try {
        util::ThreadRename("mca-cpu-worker");
        auto hash{m_hash_factory()}; // Exactly one independent context per worker.
        if (!hash) throw std::runtime_error{"Mining hash context is unavailable"};
        ++m_ready_workers; ready = true;
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
                    ++m_cpu_hashes;
                    if (UintToArith256(result) <= work->job.target) {
                        ReportCandidate(work, header);
                        if (!work->job.continuous) break;
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
    if (ready) --m_ready_workers;
}
void MiningController::ReportCandidate(const std::shared_ptr<Work>& work, const CBlockHeader& header)
{
    if (work->job.continuous) {
        std::unique_lock lock{m_mutex};
        ++m_stats.solutions;
        while (!m_stop && !work->cancelled && (!work->job.valid || *work->job.valid) && std::chrono::steady_clock::now() < work->expires && m_solutions.size() >= 64)
            m_cv.wait_for(lock, std::chrono::milliseconds{100});
        if (!m_stop && m_work == work && !work->cancelled && (!work->job.valid || *work->job.valid) && std::chrono::steady_clock::now() < work->expires) {
            m_solutions.push_back(header);
            m_cv.notify_all();
        }
        return;
    }
    bool expected{false};
    if (work->solved.compare_exchange_strong(expected, true)) {
        std::lock_guard lock{m_mutex};
        ++m_stats.solutions;
        if (!m_stop && m_work == work && !work->cancelled && (!work->job.valid || *work->job.valid) && std::chrono::steady_clock::now() < work->expires) {
            m_solutions.push_back(header);
            m_cv.notify_all();
        }
    }
}

void MiningController::GpuWorker(size_t index, GpuConfig hardware)
{
    bool active{false}, ready{false};
    auto status = [&](const std::string& message, bool failed = false) {
        std::lock_guard lock{m_mutex};
        m_stats.devices[index].status = message;
        m_stats.devices[index].failed = failed;
        if (failed) { m_stats.devices[index].hashes_per_second = 0; m_stats.devices[index].memory_bytes = 0; }
    };
    try {
        util::ThreadRename("mca-gpu-worker");
        GpuDevice device;
        { std::lock_guard lock{m_mutex}; device = m_stats.devices[index].device; }
        auto backend{m_create_backend(device, hardware.contexts)};
        if (!backend || backend->Contexts() == 0 || backend->Contexts() > 256) throw std::runtime_error{"Invalid GPU hashing context count"};
        {
            std::lock_guard lock{m_mutex};
            auto& stats{m_stats.devices[index]};
            stats.contexts = backend->Contexts(); stats.memory_bytes = backend->MemoryBytes();
            stats.status = "Checking MercaHash V1 reference vectors";
        }
        VerifyGpuBackend(*backend, [this] { return m_stop.load(); });
        if (m_stop) return;
        auto verify{m_hash_factory()};
        if (!verify) throw std::runtime_error{"Authoritative GPU candidate verifier is unavailable"};
        ++m_ready_workers; ready = true;
        status("Ready");
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
            ++m_active; active = true;
            const auto cancelled = [this, work] {
                return m_stop.load() || work->cancelled.load() || work->solved.load() ||
                       (work->job.valid && !work->job.valid->load()) || std::chrono::steady_clock::now() >= work->expires;
            };
            status("Mining");
            while (!cancelled()) {
                const auto range{m_allocate_range(work->next_nonce)};
                if (!range) break;
                for (uint64_t nonce{range->begin}; nonce < range->end && !cancelled();) {
                    const auto count{static_cast<size_t>(std::min<uint64_t>(backend->Contexts(), range->end - nonce))};
                    std::vector<GpuHeader> headers(count);
                    std::vector<CBlockHeader> candidates(count, work->job.header);
                    for (size_t slot{0}; slot < count; ++slot) {
                        candidates[slot].nNonce = static_cast<uint32_t>(nonce + slot);
                        DataStream stream; stream << candidates[slot];
                        if (stream.size() != headers[slot].size()) throw std::runtime_error{"Noncanonical mining header size"};
                        std::memcpy(headers[slot].data(), stream.data(), headers[slot].size());
                    }
                    const auto hashes{backend->Hash(headers, cancelled)};
                    if (cancelled()) break;
                    if (hashes.size() != count) throw std::runtime_error{"GPU returned an incomplete hashing batch"};
                    m_hashes += count;
                    { std::lock_guard lock{m_mutex}; m_stats.devices[index].hashes += count; }
                    for (size_t slot{0}; slot < count && !cancelled(); ++slot) {
                        const uint256 result{std::span<const unsigned char>{hashes[slot]}};
                        if (UintToArith256(result) > work->job.target) continue;
                        // Exact hash equality detects faults even if a wrong hash
                        // happens to satisfy the target. Never submit device output
                        // without the authoritative Core hash and target checks.
                        const auto authoritative{verify(candidates[slot])};
                        if (authoritative != result) throw std::runtime_error{"GPU candidate hash disagrees with authoritative MercaHash V1"};
                        if (UintToArith256(authoritative) <= work->job.target && !cancelled()) ReportCandidate(work, candidates[slot]);
                    }
                    nonce += count;
                    if (hardware.yield_ms && !cancelled()) {
                        std::unique_lock lock{m_mutex};
                        m_cv.wait_for(lock, std::chrono::milliseconds{hardware.yield_ms}, cancelled);
                    }
                }
            }
            --m_active; active = false;
            status("Waiting for work");
        }
    } catch (const std::exception& error) {
        status(std::string{"Device failed: "} + error.what(), true);
        if (m_worker_count.fetch_sub(1) == 1 && !m_stop) Fail(std::string{"All mining devices failed: "} + error.what());
    } catch (...) {
        status("Unexpected GPU device failure", true);
        if (m_worker_count.fetch_sub(1) == 1 && !m_stop) Fail("All mining devices failed");
    }
    if (active) --m_active;
    if (ready) --m_ready_workers;
    m_cv.notify_all();
}

} // namespace mining
