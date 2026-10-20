// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <algorithm>
#include <charconv>
#include <crypto/mercahash.h>
#include <fstream>
#include <iostream>
#include <mining/gpu_backend.h>
#include <stdexcept>

namespace {
unsigned Number(const std::string& text)
{
    unsigned result{0};
    const auto [end, error]{std::from_chars(text.data(), text.data() + text.size(), result)};
    if (error != std::errc{} || end != text.data() + text.size())
        throw std::runtime_error{"Invalid numeric argument: " + text};
    return result;
}

// Same deterministic generator as the supplied independent verification harness.
uint64_t NextRandom(uint64_t& state)
{
    state += 0x9E3779B97F4A7C15ULL;
    uint64_t value{state};
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31);
}

class DifferentialCheck {
    mining::GpuBackend& m_backend;
    const unsigned m_repetitions;
    std::vector<unsigned char> m_scratch{std::vector<unsigned char>(mercahash::ScratchpadBytes(m_backend.PoWVersion()))};
    uint64_t m_comparisons{0};

public:
    DifferentialCheck(mining::GpuBackend& backend, unsigned repetitions) : m_backend{backend}, m_repetitions{repetitions} {}
    void Batch(std::span<const mining::GpuHeader> headers)
    {
        std::vector<mining::GpuHash> expected(headers.size());
        for (size_t slot{0}; slot < headers.size(); ++slot) {
            if (m_backend.PoWVersion() == mercahash::Version::V2) mercahash::HashV2(headers[slot], m_scratch, expected[slot]);
            else mercahash::HashV1(headers[slot], m_scratch, expected[slot]);
        }
        for (unsigned pass{0}; pass < m_repetitions; ++pass) {
            const auto actual{m_backend.Hash(headers, [] { return false; })};
            if (actual != expected)
                throw std::runtime_error{"GPU batch differs from authoritative Core MercaHash (including size/order)"};
            m_comparisons += headers.size();
        }
    }
    uint64_t Comparisons() const { return m_comparisons; }
};
} // namespace

int main(int argc, char** argv)
{
    try {
        unsigned requested{0}, repetitions{3}, version{1};
        std::string headers_file;
        for (int arg{2}; arg < argc; ++arg) {
            const std::string option{argv[arg]};
            if (option == "--contexts" || option == "--repetitions" || option == "--version") {
                if (++arg == argc) throw std::runtime_error{"Missing value for " + option};
                const auto value{Number(argv[arg])};
                if (option == "--contexts") requested = value;
                else if (option == "--version") version = value;
                else repetitions = value;
            } else if (!option.starts_with('-') && headers_file.empty()) {
                headers_file = option;
            } else {
                throw std::runtime_error{"Usage: mercatura-gpu-check [--list | DEVICE_ID [HEADERS_FILE] [--contexts N] [--repetitions N] [--version 1|2]]"};
            }
        }
        if (version != 1 && version != 2) throw std::runtime_error{"Version must be 1 or 2"};
        if (requested > 256 || repetitions == 0 || repetitions > 100)
            throw std::runtime_error{"Contexts must be 0..256 and repetitions 1..100"};
        const auto discovery{mining::DiscoverGpuDevices()};
        for (const auto& error : discovery.errors) std::cout << error << '\n';
        for (const auto& device : discovery.devices)
            std::cout << device.id << " " << device.name << " " << device.capability << " "
                      << (device.supported ? "supported" : device.error) << '\n';
        if (argc == 1 || std::string{argv[1]} == "--list")
            return std::none_of(discovery.devices.begin(), discovery.devices.end(), [](const auto& device) { return device.supported; }) ? 77 : 0;
        const auto device{std::find_if(discovery.devices.begin(), discovery.devices.end(),
                                     [&](const auto& entry) { return entry.id == argv[1]; })};
        if (device == discovery.devices.end()) throw std::runtime_error{"Requested GPU was not detected"};
        auto selected{*device};
        selected.pow_version = version == 2 ? mercahash::Version::V2 : mercahash::Version::V1;
        auto backend{mining::CreateGpuBackend(selected, requested)};
        mining::VerifyGpuBackend(*backend, [] { return false; });
        const unsigned contexts{backend->Contexts()};
        std::cout << "Permanent MercaHash V" << version << " vectors passed on " << device->id
                  << " (" << contexts << " safe contexts)\n";
        DifferentialCheck check{*backend, repetitions};
        uint64_t seed{0x4D65726361747572ULL};
        // Adapt the original six verification batches to the safe production
        // budget, including a full batch and partial batches whenever possible.
        for (unsigned size : {1U, 7U, 16U, 31U, 32U, contexts}) {
            std::vector<mining::GpuHeader> headers(std::min(size, contexts));
            for (auto& header : headers) {
                for (size_t byte{0}; byte < header.size(); byte += 8) {
                    const auto value{NextRandom(seed)};
                    for (unsigned offset{0}; offset < 8; ++offset)
                        header[byte + offset] = value >> (8 * offset);
                }
            }
            check.Batch(headers);
        }
        if (!headers_file.empty()) {
            std::ifstream input{headers_file};
            if (!input) throw std::runtime_error{"Cannot read headers file"};
            std::vector<mining::GpuHeader> batch;
            std::string line;
            size_t tested{0};
            while (std::getline(input, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty() || line.starts_with('#')) continue;
                if (line.size() != 160)
                    throw std::runtime_error{"Every header must contain exactly 160 hexadecimal characters"};
                mining::GpuHeader header{};
                auto digit = [](char c) {
                    if (c >= '0' && c <= '9') return c - '0';
                    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                    throw std::runtime_error{"Invalid hexadecimal header"};
                };
                for (size_t byte{0}; byte < header.size(); ++byte)
                    header[byte] = digit(line[byte * 2]) * 16 + digit(line[byte * 2 + 1]);
                batch.push_back(header);
                ++tested;
                if (batch.size() == contexts) { check.Batch(batch); batch.clear(); }
            }
            if (!input.eof()) throw std::runtime_error{"Header file read failed"};
            if (!tested) throw std::runtime_error{"Headers file contains no test inputs"};
            if (!batch.empty()) check.Batch(batch);
        }
        std::cout << check.Comparisons() << " independent CPU/GPU comparisons passed across full/partial batches and "
                  << repetitions << " repetitions\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
