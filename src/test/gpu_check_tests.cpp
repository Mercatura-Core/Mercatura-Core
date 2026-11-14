// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.

// Exercise the standalone checker's real setup and measurement loop with a
// CPU-backed rotating pool. This test provides no GPU correctness evidence.
#define main GpuCheckMain
#include <mining/gpu_check.cpp>
#undef main

namespace {
unsigned benchmark_batches{0}, execution_window{5};
class RotatingBackend final : public mining::GpuBackend {
    unsigned next{0}, calls{0};
public:
    unsigned Contexts() const override { return 7; }
    unsigned BatchContexts() const override { return std::min(execution_window, Contexts() - next); }
    mercahash::Version PoWVersion() const override { return mercahash::Version::V2; }
    uint64_t MemoryBytes() const override { return 0; }
    std::vector<mining::GpuHash> Hash(std::span<const mining::GpuHeader> input,
                                    const std::function<bool()>& cancelled) override
    {
        if (cancelled()) return {};
        // One startup call, then six differential batches. Their combined 38
        // headers leave next=3 and a residual BatchContexts() of four.
        ++calls;
        const bool aligning{calls == 8};
        const bool measuring{calls > 8};
        if (aligning && (input.size() != 4 || next != 3))
            throw std::runtime_error{"Measurement did not consume the correctness-test tail"};
        if (calls == 9 && next != 0)
            throw std::runtime_error{"Measurement did not start at a pool boundary"};
        if (measuring) {
            if (input.size() != execution_window) throw std::runtime_error{"Measurement used a residual execution window"};
            ++benchmark_batches;
        }
        next = (next + input.size()) % Contexts();
        std::vector<mining::GpuHash> hashes(input.size());
        if (!measuring && !aligning) {
            std::vector<unsigned char> scratch(mercahash::V2_SCRATCHPAD_BYTES);
            for (size_t i{0}; i < input.size(); ++i) mercahash::HashV2(input[i], scratch, hashes[i]);
        }
        return hashes;
    }
};
} // namespace

namespace mining {
GpuDiscovery DiscoverGpuDevices()
{
    GpuDevice device;
    device.id = "test:cpu";
    device.supported = true;
    return {{device}, {}};
}
std::unique_ptr<GpuBackend> CreateGpuBackend(const GpuDevice&, unsigned, const std::function<bool()>&)
{
    return std::make_unique<RotatingBackend>();
}
void VerifyGpuBackend(GpuBackend& backend, const std::function<bool()>& cancelled)
{
    // Deliberately consume contexts during startup verification as well.
    backend.Hash(std::vector<GpuHeader>(2), cancelled);
}
} // namespace mining

int main()
{
    const char* args[]{"gpu-check-test", "test:cpu", "--version", "2", "--repetitions", "1", "--benchmark-seconds", "1"};
    std::vector<std::string> storage(std::begin(args), std::end(args));
    std::vector<char*> argv;
    for (auto& arg : storage) argv.push_back(arg.data());
    // Cover a hardware window smaller than the pool and a whole-pool window.
    for (const auto window : {5U, 7U}) {
        execution_window = window;
        benchmark_batches = 0;
        const auto result{GpuCheckMain(argv.size(), argv.data())};
        if (result || !benchmark_batches) return 1;
    }
    std::cout << "Full benchmark window preserved after correctness checks (CPU test double)\n";
    return 0;
}
