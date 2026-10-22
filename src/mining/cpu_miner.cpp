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
static_assert(MAX_GPU_CONTEXTS == NONCE_CHUNK, "GPU capacity changes require a nonce scheduling review");
constexpr auto JOB_LIFETIME{std::chrono::seconds{30}};
uint64_t TotalHashes(const MiningStats& stats)
{
    uint64_t hashes{stats.cpu_hashes};
    for (const auto& device : stats.devices) hashes += device.hashes;
    return hashes;
}
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

std::vector<uint32_t> V2NonceBatch::Next(std::atomic<uint64_t>& cursor, unsigned int maximum,
    const std::function<std::optional<NonceRange>(std::atomic<uint64_t>&)>& allocate,
    const std::function<bool()>& cancelled)
{
    if (!maximum) throw std::runtime_error{"Empty V2 GPU execution capacity"};
    std::vector<uint32_t> nonces;
    while (nonces.size() < maximum && !cancelled()) {
        if (!m_pending || m_pending->begin == m_pending->end) m_pending = allocate(cursor);
        if (!m_pending) break;
        if (m_pending->begin >= m_pending->end || m_pending->end > NONCE_SPACE)
            throw std::runtime_error{"Invalid V2 nonce lease"};
        const auto count{std::min<uint64_t>(maximum - nonces.size(), m_pending->end - m_pending->begin)};
        for (uint64_t i{0}; i < count; ++i) nonces.push_back(static_cast<uint32_t>(m_pending->begin++));
    }
    return cancelled() ? std::vector<uint32_t>{} : nonces;
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
    : m_hash_factory{std::move(factory)},
      m_detect_limits{limits ? std::move(limits) : LimitDetector{DetectWorkerLimits}},
      m_allocate_range{ranges ? std::move(ranges) : RangeAllocator{AllocateNonceRange}},
      m_detect_devices{devices ? std::move(devices) : DeviceDetector{DiscoverGpuDevices}},
      m_create_backend{backends ? std::move(backends) : BackendFactory{[this](const GpuDevice& device, unsigned int contexts) {
          return CreateGpuBackend(device, contexts, [this] { return m_stop.load(); });
      }}}
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
    {
        std::lock_guard lock{m_mutex};
        // Publish the new session and its counters as one coherent snapshot.
        m_stop = false;
        m_finished = false;
        m_cpu_hashes = 0;
        m_active = 0;
        m_ready_workers = 0;
        m_stats = {};
        m_statistics.Reset(std::chrono::steady_clock::now());
        m_cpu_statistics.Reset(std::chrono::steady_clock::now());
        m_device_statistics.clear();
        m_stats.state = MiningState::STARTING;
        m_stats.status = "Preparing mining";
        m_solutions.clear();
    }
    try {
        m_coordinator = std::thread{[this, factory = std::move(factory), workers, hardware = std::move(hardware)] { Run(factory, workers, hardware); }};
    } catch (const std::exception& e) {
        Fail(e.what());
        {
            std::lock_guard lock{m_mutex};
            m_statistics.Finish(std::chrono::steady_clock::now(), TotalHashes(m_stats));
        }
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
    // Provider interruption and teardown belong to background threads. Stop
    // must not wait for a wallet/chain/provider lock on the caller (Qt) thread.
}
void MiningController::Wait() { if (m_coordinator.joinable()) m_coordinator.join(); }

MiningStats MiningController::GetStats() const
{
    std::lock_guard lock{m_mutex};
    MiningStats stats{m_stats};
    stats.cpu_hashes = m_cpu_hashes.load();
    const auto now{std::chrono::steady_clock::now()};
    const auto measured{m_statistics.Observe(now, TotalHashes(stats))};
    stats.cpu_hashes_per_second = m_cpu_statistics.Observe(now, stats.cpu_hashes).live;
    for (size_t i{0}; i < stats.devices.size(); ++i)
        stats.devices[i].hashes_per_second = stats.devices[i].failed ? 0 : m_device_statistics[i].Observe(now, stats.devices[i].hashes).live;
    stats.busy = !m_finished.load();
    stats.hashes = measured.hashes;
    stats.hashes_per_second = measured.live;
    stats.hashes_per_second_10s = measured.average_10s;
    stats.hashes_per_second_60s = measured.average_60s;
    stats.hashes_per_second_session = measured.average_session;
    stats.session_elapsed = measured.elapsed;
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
    m_stats.stale += m_solutions.size();
    m_solutions.clear();
    m_cv.notify_all();
}

void MiningController::Run(ProviderFactory factory, unsigned int requested, GpuConfig hardware)
{
    std::vector<std::thread> workers;
    std::thread interrupter;
    try {
        util::ThreadRename("mca-mining");
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
        {
            std::lock_guard lock{m_mutex};
            for (const auto& device : selected) {
                m_stats.devices.push_back(GpuStats{device});
                m_device_statistics.emplace_back();
                m_device_statistics.back().Reset(std::chrono::steady_clock::now());
            }
        }
        m_worker_count = 0;
        std::optional<mercahash::Version> running_version;
        auto provider{factory()};
        if (!provider) throw std::runtime_error{"Mining provider is unavailable"};
        const auto destination{provider->Destination()};
        { std::lock_guard lock{m_mutex}; m_stats.destination = destination; }
        m_provider = std::move(provider);
        // Dedicated interruption avoids blocking RequestStop on provider code.
        // The coordinator keeps the provider alive until this helper joins.
        interrupter = std::thread{[this] {
            {
                std::unique_lock lock{m_mutex};
                m_cv.wait(lock, [this] { return m_stop.load(); });
            }
            try { m_provider->Interrupt(); }
            catch (const std::exception& e) { Fail(std::string{"Mining interruption failed: "} + e.what()); }
            catch (...) { Fail("Mining interruption failed"); }
        }};
        auto start_workers = [&](mercahash::Version version) {
            // Ordinary template turnover retains the pool. Only an algorithm
            // change joins it and rechecks budgets, including a reorg to V1.
            CancelWork();
            uint64_t epoch{0};
            {
                // Epoch is part of the condition-variable wait predicate.
                // Its mutex prevents notification between the check and wait.
                std::lock_guard lock{m_mutex};
                epoch = ++m_worker_epoch;
            }
            m_cv.notify_all();
            for (auto& worker : workers) worker.join();
            workers.clear();
            const auto detected{m_detect_limits()};
            const auto limits{version == mercahash::Version::V1 ? detected :
                CalculateWorkerLimits(detected.logical_cpus, detected.available_memory, version)};
            const unsigned int count{hardware.cpu ? (requested ? requested : limits.recommended) : 0};
            if (hardware.cpu && count == 0) throw std::runtime_error{"Insufficient available memory for a mining worker"};
            if (count > limits.maximum) throw std::runtime_error{"Requested worker count exceeds the current safe CPU or memory limit"};
            const auto host{CalculateWorkerLimits(std::numeric_limits<unsigned int>::max(), limits.available_memory, version)};
            if (selected.size() > host.maximum || count > MaximumCpuWorkers(limits, selected.size(), version))
                throw std::runtime_error{"Insufficient host memory for CPU workers and GPU candidate verification"};
            {
                std::lock_guard lock{m_mutex};
                for (auto& stats : m_stats.devices) {
                    stats.failed = false;
                    stats.contexts = stats.estimated_contexts = 0;
                    stats.memory_bytes = 0;
                    stats.batch_contexts = stats.active_contexts = 0;
                    stats.budget = {};
                    stats.device.pow_version = version;
                    stats.status = "Preparing device";
                }
            }
            m_worker_count = count + static_cast<unsigned int>(selected.size());
            workers.reserve(count + selected.size());
            for (unsigned int i{0}; i < count && !m_stop; ++i) workers.emplace_back([this, epoch] { Worker(epoch); });
            for (size_t i{0}; i < selected.size() && !m_stop; ++i) workers.emplace_back([this, i, hardware, epoch, version] { GpuWorker(i, hardware, epoch, version); });
            running_version = version;
        };
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
            // All ordinary provider operations run on this coordinator.
            // Only the joined interruption helper calls Interrupt concurrently.
            if (work && (!m_provider->IsCurrent(work->job) || now >= work->expires)) {
                if (solution) {
                    std::lock_guard lock{m_mutex};
                    ++m_stats.stale;
                }
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
                    else if (result.stale)
                        ++m_stats.stale;
                    else if (!result.processed)
                        ++m_stats.rejected;
                    if (m_stats.state != MiningState::FAILED) m_stats.status = result.message;
                }
                CancelWork();
                work.reset();
            }
            bool exhausted{false};
            if (work && work->next_nonce >= NONCE_SPACE && m_active == 0) {
                std::lock_guard lock{m_mutex};
                // A pending solution belongs to this job. Submit it before
                // installing a new job and its independent nonce cursor.
                exhausted = m_solutions.empty();
            }
            if (!work || exhausted) {
                auto job{m_provider->GetJob()};
                if (m_stop) break;
                const auto waiting{job ? std::string{} : m_provider->WaitingStatus()};
                if (job) {
                    (void)mercahash::ScratchpadBytes(job->pow_version);
                    if (job->height < 0) throw std::runtime_error{"Mining job has an invalid height"};
                    if (!running_version || *running_version != job->pow_version) start_workers(job->pow_version);
                }
                std::lock_guard lock{m_mutex};
                if (m_stop) break;
                if (job) {
                    if (job->target == arith_uint256{}) throw std::runtime_error{"Mining job has an invalid zero target"};
                    m_stats.pow_version = job->pow_version;
                    m_stats.job_height = job->height;
                    m_work = std::make_shared<Work>(std::move(*job));
                    m_stats.state = m_ready_workers > 0 ? MiningState::RUNNING : MiningState::STARTING;
                    m_stats.status = m_ready_workers > 0 ? "Mining" : "Preparing mining hardware";
                    m_cv.notify_all();
                } else {
                    m_stats.status = waiting;
                    m_stats.state = MiningState::STARTING;
                }
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
    if (interrupter.joinable()) interrupter.join();
    m_provider.reset();
    std::lock_guard lock{m_mutex};
    m_stats.cpu_hashes = m_cpu_hashes.load();
    const auto finished_at{std::chrono::steady_clock::now()};
    m_statistics.Finish(finished_at, TotalHashes(m_stats));
    m_cpu_statistics.Finish(finished_at, m_stats.cpu_hashes);
    for (size_t i{0}; i < m_stats.devices.size(); ++i) m_device_statistics[i].Finish(finished_at, m_stats.devices[i].hashes);
    m_stats.hashes_per_second = 0;
    m_stats.cpu_hashes_per_second = 0;
    for (auto& device : m_stats.devices) { device.active_contexts = 0; device.hashes_per_second = 0; device.memory_bytes = 0; if (!device.failed) device.status = "Stopped"; }
    if (m_stats.state != MiningState::FAILED) {
        m_stats.state = MiningState::STOPPED;
        m_stats.status = "Stopped";
    }
    m_finished = true;
}

void MiningController::Worker(uint64_t epoch)
{
    bool active{false}, ready{false};
    try {
        util::ThreadRename("mca-cpu-worker");
        PoWHashContext context; // Exactly one private authoritative context.
        auto hash{m_hash_factory ? m_hash_factory() : HashFunction{}};
        if (m_hash_factory && !hash) throw std::runtime_error{"Mining hash context is unavailable"};
        ++m_ready_workers; ready = true;
        std::shared_ptr<Work> previous;
        while (!m_stop && epoch == m_worker_epoch) {
            std::shared_ptr<Work> work;
            {
                std::unique_lock lock{m_mutex};
                m_cv.wait(lock, [this, &previous, epoch] { return m_stop.load() || epoch != m_worker_epoch || (m_work && m_work != previous); });
                if (m_stop || epoch != m_worker_epoch) break;
                work = m_work;
            }
            previous = work;
            ++m_active;
            active = true;
            CBlockHeader header{work->job.header};
            while (!m_stop && epoch == m_worker_epoch && !work->cancelled && !work->solved && (!work->job.valid || *work->job.valid) && std::chrono::steady_clock::now() < work->expires) {
                const auto range{m_allocate_range(work->next_nonce)};
                if (!range) break;
                for (uint64_t nonce{range->begin}; nonce < range->end; ++nonce) {
                    // Do not alter MercaHash for cancellation; finish the current
                    // attempt, then check stop, replacement and expiry.
                    if (m_stop || epoch != m_worker_epoch || work->cancelled || work->solved || (work->job.valid && !*work->job.valid) || std::chrono::steady_clock::now() >= work->expires) break;
                    header.nNonce = static_cast<uint32_t>(nonce);
                    const auto result{hash ? hash(header) : context.GetHash(header, work->job.pow_version)};
                    ++m_cpu_hashes;
                    if (UintToArith256(result) <= work->job.target) {
                        ReportCandidate(work, header);
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
    if (ready) --m_ready_workers;
}
void MiningController::ReportCandidate(const std::shared_ptr<Work>& work, const CBlockHeader& header)
{
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

void MiningController::GpuWorker(size_t index, GpuConfig hardware, uint64_t epoch, mercahash::Version version)
{
    bool active{false}, ready{false};
    auto status = [&](const std::string& message, bool failed = false) {
        std::lock_guard lock{m_mutex};
        m_stats.devices[index].status = message;
        m_stats.devices[index].failed = failed;
        if (failed) { m_stats.devices[index].hashes_per_second = 0; m_stats.devices[index].memory_bytes = 0; m_stats.devices[index].active_contexts = 0; }
    };
    try {
        util::ThreadRename("mca-gpu-worker");
        GpuDevice device;
        { std::lock_guard lock{m_mutex}; device = m_stats.devices[index].device; }
        device.pow_version = version;
        auto backend{m_create_backend(device, hardware.contexts)};
        if (!backend || backend->Contexts() == 0 || (version == mercahash::Version::V1 && backend->Contexts() > MAX_GPU_CONTEXTS)) throw std::runtime_error{"Invalid GPU hashing context count"};
        if (!backend->BatchContexts() || backend->BatchContexts() > backend->Contexts()) throw std::runtime_error{"Invalid GPU execution capacity"};
        if (backend->PoWVersion() != version) throw std::runtime_error{"GPU backend selected the wrong MercaHash version"};
        {
            std::lock_guard lock{m_mutex};
            auto& stats{m_stats.devices[index]};
            stats.contexts = backend->Contexts(); stats.memory_bytes = backend->MemoryBytes();
            stats.estimated_contexts = backend->EstimatedContexts(); stats.automatic = hardware.contexts == 0;
            stats.batch_contexts = backend->BatchContexts(); stats.budget = backend->Budget();
            stats.status = "Checking MercaHash reference vectors";
        }
        VerifyGpuBackend(*backend, [this, epoch] { return m_stop || epoch != m_worker_epoch; });
        if (m_stop || epoch != m_worker_epoch) return;
        PoWHashContext verifier;
        auto verify{m_hash_factory ? m_hash_factory() : HashFunction{}};
        if (m_hash_factory && !verify) throw std::runtime_error{"Authoritative GPU candidate verifier is unavailable"};
        ++m_ready_workers; ready = true;
        status("Ready");
        std::shared_ptr<Work> previous;
        while (!m_stop && epoch == m_worker_epoch) {
            std::shared_ptr<Work> work;
            {
                std::unique_lock lock{m_mutex};
                m_cv.wait(lock, [this, &previous, epoch] { return m_stop.load() || epoch != m_worker_epoch || (m_work && m_work != previous); });
                if (m_stop || epoch != m_worker_epoch) break;
                work = m_work;
            }
            previous = work;
            ++m_active; active = true;
            const auto cancelled = [this, work, epoch] {
                return m_stop.load() || epoch != m_worker_epoch || work->cancelled.load() || work->solved.load() ||
                       (work->job.valid && !work->job.valid->load()) || std::chrono::steady_clock::now() >= work->expires;
            };
            status("Mining");
            if (version == mercahash::Version::V2) {
                V2NonceBatch leases;
                while (!cancelled()) {
                    const auto nonces{leases.Next(work->next_nonce, backend->BatchContexts(), m_allocate_range, cancelled)};
                    if (nonces.empty()) break;
                    std::vector<GpuHeader> headers(nonces.size());
                    std::vector<CBlockHeader> candidates(nonces.size(), work->job.header);
                    for (size_t slot{0}; slot < nonces.size(); ++slot) {
                        candidates[slot].nNonce = nonces[slot];
                        DataStream stream; stream << candidates[slot];
                        if (stream.size() != headers[slot].size()) throw std::runtime_error{"Noncanonical mining header size"};
                        std::memcpy(headers[slot].data(), stream.data(), headers[slot].size());
                    }
                    { std::lock_guard lock{m_mutex}; m_stats.devices[index].active_contexts = nonces.size(); ++m_stats.devices[index].batches_started; }
                    const auto hashes{backend->Hash(headers, cancelled)};
                    { std::lock_guard lock{m_mutex}; m_stats.devices[index].active_contexts = 0; }
                    if (hashes.size() != nonces.size()) {
                        if (cancelled()) { std::lock_guard lock{m_mutex}; ++m_stats.devices[index].batches_cancelled; break; }
                        throw std::runtime_error{"GPU returned an incomplete hashing batch"};
                    }
                    { std::lock_guard lock{m_mutex}; m_stats.devices[index].hashes += nonces.size(); ++m_stats.devices[index].batches_completed; }
                    if (cancelled()) break;
                    for (size_t slot{0}; slot < nonces.size() && !cancelled(); ++slot) {
                        const uint256 result{std::span<const unsigned char>{hashes[slot]}};
                        if (UintToArith256(result) > work->job.target) continue;
                        const auto authoritative{verify ? verify(candidates[slot]) : verifier.GetHash(candidates[slot], work->job.pow_version)};
                        if (authoritative != result) throw std::runtime_error{"GPU candidate hash disagrees with authoritative MercaHash"};
                        if (UintToArith256(authoritative) <= work->job.target && !cancelled()) ReportCandidate(work, candidates[slot]);
                    }
                    if (hardware.yield_ms && !cancelled()) {
                        std::unique_lock lock{m_mutex};
                        m_cv.wait_for(lock, std::chrono::milliseconds{hardware.yield_ms}, cancelled);
                    }
                }
            } else
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
                    { std::lock_guard lock{m_mutex}; m_stats.devices[index].active_contexts = count; ++m_stats.devices[index].batches_started; }
                    const auto hashes{backend->Hash(headers, cancelled)};
                    { std::lock_guard lock{m_mutex}; m_stats.devices[index].active_contexts = 0; }
                    if (hashes.size() != count) {
                        if (cancelled()) { std::lock_guard lock{m_mutex}; ++m_stats.devices[index].batches_cancelled; break; }
                        throw std::runtime_error{"GPU returned an incomplete hashing batch"};
                    }
                    // Completed GPU work counts even if a tip change or Stop
                    // arrived during the batch. Obsolete candidates never submit.
                    { std::lock_guard lock{m_mutex}; m_stats.devices[index].hashes += count; ++m_stats.devices[index].batches_completed; }
                    if (cancelled()) break;
                    for (size_t slot{0}; slot < count && !cancelled(); ++slot) {
                        const uint256 result{std::span<const unsigned char>{hashes[slot]}};
                        if (UintToArith256(result) > work->job.target) continue;
                        // Exact hash equality detects faults even if a wrong hash
                        // happens to satisfy the target. Never submit device output
                        // without the authoritative Core hash and target checks.
                        const auto authoritative{verify ? verify(candidates[slot]) : verifier.GetHash(candidates[slot], work->job.pow_version)};
                        if (authoritative != result) throw std::runtime_error{"GPU candidate hash disagrees with authoritative MercaHash"};
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
