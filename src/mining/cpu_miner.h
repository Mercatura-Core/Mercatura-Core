// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.

#ifndef BITCOIN_MINING_CPU_MINER_H
#define BITCOIN_MINING_CPU_MINER_H

#include <arith_uint256.h>
#include <primitives/block.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace mining {

inline constexpr uint64_t NONCE_SPACE{uint64_t{1} << 32};
struct NonceRange { uint64_t begin; uint64_t end; };
std::optional<NonceRange> AllocateNonceRange(std::atomic<uint64_t>& cursor);

struct WorkerLimits {
    unsigned int logical_cpus{1};
    std::optional<uint64_t> available_memory;
    unsigned int maximum{0};
    unsigned int recommended{0};
};
// A 128 MiB private scratchpad plus 32 MiB for stacks and other worker overhead.
inline constexpr uint64_t WORKER_MEMORY_BYTES{160ULL * 1024 * 1024};
WorkerLimits CalculateWorkerLimits(unsigned int cpus, std::optional<uint64_t> memory);
WorkerLimits DetectWorkerLimits();

// Immutable work: public header and optional public coinbase only, never keys.
struct MiningJob {
    CBlockHeader header;
    arith_uint256 target;
    CTransactionRef coinbase;
    // Pool shares do not retire the job or reset its nonce cursor.
    bool continuous{false};
    // A transport can revoke work while the coordinator awaits a receipt.
    std::shared_ptr<std::atomic<bool>> valid{};
    std::optional<std::chrono::steady_clock::time_point> deadline{};
};

struct SubmissionResult {
    bool processed{false};
    bool active_chain{false};
    std::string message;
};

// Methods other than Interrupt are called only by the controller's coordinator.
// Interrupt must be thread safe and must unblock a pending GetJob.
class WorkProvider {
public:
    virtual ~WorkProvider() = default;
    virtual std::optional<MiningJob> GetJob() = 0;
    virtual bool IsCurrent(const MiningJob& job) = 0;
    virtual SubmissionResult Submit(const MiningJob& job, const CBlockHeader& solution) = 0;
    virtual void Interrupt() = 0;
    virtual bool IsCancelled() const { return false; }
    virtual std::string Destination() const { return {}; }
    virtual std::string WaitingStatus() const { return "Waiting for synchronization and a connected chain tip"; }
};

enum class MiningState { STOPPED, STARTING, RUNNING, STOPPING, FAILED };
struct MiningStats {
    MiningState state{MiningState::STOPPED};
    bool busy{false};
    uint64_t hashes{0};
    double hashes_per_second{0};
    unsigned int active_workers{0};
    uint64_t solutions{0};
    uint64_t submitted{0};
    uint64_t accepted{0};
    std::string status{"Stopped"};
    std::string destination;
};

class MiningController {
public:
    using ProviderFactory = std::function<std::unique_ptr<WorkProvider>()>;
    // Test seam: production always constructs one PoWHashContext per worker.
    using HashFunction = std::function<uint256(const CBlockHeader&)>;
    using HashFactory = std::function<HashFunction()>;
    using LimitDetector = std::function<WorkerLimits()>;
    // Boundary-test seam; production always uses AllocateNonceRange.
    using RangeAllocator = std::function<std::optional<NonceRange>(std::atomic<uint64_t>&)>;
    explicit MiningController(HashFactory hash_factory = {}, LimitDetector limits = {}, RangeAllocator ranges = {});
    ~MiningController();
    MiningController(const MiningController&) = delete;
    MiningController& operator=(const MiningController&) = delete;

    // Start/Wait are serialized by the owner. 0 requests automatic selection.
    // All allocation, wallet preparation, block assembly, hashing and submission
    // happen off the caller's thread. Start never automatically runs at launch.
    bool Start(ProviderFactory provider, unsigned int workers = 0);
    void RequestStop();
    void Wait(); // Lifecycle barrier before node destruction; not a UI Stop action.
    MiningStats GetStats() const;

private:
    struct Work;
    void Run(ProviderFactory factory, unsigned int requested);
    void Worker();
    void Fail(const std::string& error);
    void CancelWork();

    const HashFactory m_hash_factory;
    const LimitDetector m_detect_limits;
    const RangeAllocator m_allocate_range;
    std::thread m_coordinator;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_finished{true};
    std::atomic<uint64_t> m_hashes{0};
    std::atomic<unsigned int> m_active{0};
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::shared_ptr<Work> m_work;
    std::deque<CBlockHeader> m_solutions;
    MiningStats m_stats;
    std::mutex m_provider_mutex;
    std::unique_ptr<WorkProvider> m_provider;
};

} // namespace mining
#endif // BITCOIN_MINING_CPU_MINER_H
