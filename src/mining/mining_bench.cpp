// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.

#include <mining/cpu_miner.h>
#include <univalue.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <thread>
#ifndef WIN32
#include <sys/resource.h>
#endif

namespace {
std::optional<uint64_t> CurrentRss()
{
#ifdef __linux__
    std::ifstream status{"/proc/self/status"};
    std::string line;
    while (std::getline(status, line)) {
        std::istringstream fields{line};
        std::string key;
        uint64_t kib{0};
        if (fields >> key >> kib && key == "VmRSS:" && kib <= std::numeric_limits<uint64_t>::max() / 1024) return kib * 1024;
    }
#endif
    return {};
}

class BenchmarkWork final : public mining::WorkProvider
{
    std::atomic<bool> stopped{false};
    unsigned int serial{0};
    const std::shared_ptr<std::atomic<unsigned int>> jobs;

public:
    explicit BenchmarkWork(std::shared_ptr<std::atomic<unsigned int>> count) : jobs{std::move(count)} {}
    std::optional<mining::MiningJob> GetJob() override
    {
        if (stopped) return {};
        mining::MiningJob job;
        job.header.nVersion = ++serial;
        job.target = arith_uint256{1};
        job.deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
        ++*jobs;
        return job;
    }
    bool IsCurrent(const mining::MiningJob&) override { return !stopped; }
    mining::SubmissionResult Submit(const mining::MiningJob&, const CBlockHeader&) override { return {}; }
    void Interrupt() override { stopped = true; }
};
} // namespace

int main(int argc, char** argv)
{
    try {
        if (argc > 4) throw std::runtime_error{"usage: mining-bench [workers=0] [seconds=10] [cycles=1]"};
        const auto number = [](std::string_view input) {
            unsigned long value{0};
            const auto [end, error]{std::from_chars(input.data(), input.data() + input.size(), value)};
            if (error != std::errc{} || end != input.data() + input.size()) throw std::runtime_error{"expected an unsigned decimal integer"};
            return value;
        };
        const unsigned long workers{argc > 1 ? number(argv[1]) : 0};
        const unsigned long seconds{argc > 2 ? number(argv[2]) : 10};
        const unsigned long cycles{argc > 3 ? number(argv[3]) : 1};
        if (!seconds || seconds > 60 || !cycles || cycles > 10) throw std::runtime_error{"duration is 1..60 seconds; cycles are 1..10"};
        const auto detected{mining::DetectWorkerLimits()};
        auto budget{detected};
        // Bound short, isolated experiments to at most 64 usable logical CPUs.
        // This executable never runs a node/wallet, never installs, and never
        // changes production worker selection. Memory bounds remain intact.
        const auto memory{mining::CalculateWorkerLimits(std::numeric_limits<unsigned int>::max(), detected.available_memory)};
        budget.maximum = std::min({detected.logical_cpus, memory.maximum, 64U});
        const unsigned int count{workers ? static_cast<unsigned int>(std::min<unsigned long>(workers, std::numeric_limits<unsigned int>::max())) : budget.recommended};
        if (workers > budget.maximum || !count) throw std::runtime_error{"worker count exceeds the bounded benchmark CPU/memory budget"};
        mining::MiningController controller{{}, [budget] { return budget; }};
        UniValue samples{UniValue::VARR};
        for (unsigned int cycle{0}; cycle < cycles; ++cycle) {
            const auto rss_before{CurrentRss()};
            auto jobs{std::make_shared<std::atomic<unsigned int>>(0)};
            const auto begin{std::chrono::steady_clock::now()};
            const auto cpu_begin{std::clock()};
            if (!controller.Start([jobs] { return std::make_unique<BenchmarkWork>(jobs); }, count)) throw std::runtime_error{"benchmark Start failed"};
            const auto deadline{begin + std::chrono::seconds{seconds}};
            while (std::chrono::steady_clock::now() < deadline && controller.GetStats().busy)
                std::this_thread::sleep_for(std::chrono::milliseconds{10});
            const auto stopping{std::chrono::steady_clock::now()};
            controller.RequestStop();
            const auto requested{std::chrono::steady_clock::now()};
            controller.Wait();
            const auto finished{std::chrono::steady_clock::now()};
            const auto stats{controller.GetStats()};
            if (stats.state == mining::MiningState::FAILED) throw std::runtime_error{stats.status};
            UniValue sample{UniValue::VOBJ};
            const double elapsed{std::chrono::duration<double>(finished - begin).count()};
            sample.pushKV("seconds", elapsed);
            sample.pushKV("hashes", stats.hashes);
            sample.pushKV("hashes_per_second", stats.hashes / elapsed);
            sample.pushKV("cpu_seconds", double(std::clock() - cpu_begin) / CLOCKS_PER_SEC);
            sample.pushKV("stop_request_ms", std::chrono::duration<double, std::milli>(requested - stopping).count());
            sample.pushKV("stop_join_ms", std::chrono::duration<double, std::milli>(finished - stopping).count());
            sample.pushKV("jobs", jobs->load());
            sample.pushKV("active_after_stop", stats.active_workers);
            if (rss_before) sample.pushKV("rss_before_bytes", *rss_before);
            if (const auto rss_after{CurrentRss()}) sample.pushKV("rss_after_stop_bytes", *rss_after);
            samples.push_back(sample);
        }
        UniValue output{UniValue::VOBJ};
        output.pushKV("mode", "real CPU controller hashing with synthetic public work; no node or wallet");
        output.pushKV("workers", count);
        output.pushKV("production_recommended", detected.recommended);
        output.pushKV("production_maximum", detected.maximum);
        output.pushKV("responsiveness_reserve_overridden", count > detected.maximum);
        output.pushKV("scratchpad_bytes", count * uint64_t{128} * 1024 * 1024);
#ifndef WIN32
        rusage usage{};
        if (getrusage(RUSAGE_SELF, &usage) == 0) {
#ifdef __APPLE__
            output.pushKV("peak_rss_bytes", usage.ru_maxrss);
#else
            output.pushKV("peak_rss_bytes", usage.ru_maxrss * int64_t{1024});
#endif
        }
#endif
        output.pushKV("samples", samples);
        std::cout << output.write(2) << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
