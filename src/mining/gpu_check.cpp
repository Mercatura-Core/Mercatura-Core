// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <algorithm>
#include <crypto/mercahash.h>
#include <fstream>
#include <iostream>
#include <mining/gpu_backend.h>
#include <stdexcept>
int main(int argc, char **argv)
{
    try {
        const auto discovery{mining::DiscoverGpuDevices()};
        for (const auto &error : discovery.errors)
            std::cout << error << '\n';
        for (const auto &device : discovery.devices)
            std::cout << device.id << " " << device.name << " " << device.capability << " "
                      << (device.supported ? "supported" : device.error) << '\n';
        if (argc == 1 || std::string{argv[1]} == "--list")
            return std::none_of(discovery.devices.begin(), discovery.devices.end(), [](const auto &device) { return device.supported; }) ? 77 : 0;
        if (argc > 3)
            throw std::runtime_error{"Usage: mercatura-gpu-check [--list | DEVICE_ID [HEADERS_FILE]]"};
        const auto device{std::find_if(discovery.devices.begin(), discovery.devices.end(),
                                       [&](const auto &entry) { return entry.id == argv[1]; })};
        if (device == discovery.devices.end())
            throw std::runtime_error{"Requested GPU was not detected"};
        auto backend{mining::CreateGpuBackend(*device, 0)};
        mining::VerifyGpuBackend(*backend, [] { return false; });
        std::cout << "Permanent MercaHash V1 vectors passed on " << device->id << '\n';
        if (argc == 3) {
            std::ifstream input{argv[2]};
            if (!input)
                throw std::runtime_error{"Cannot read headers file"};
            std::vector<unsigned char> scratch(mercahash::SCRATCHPAD_BYTES);
            std::string line;
            size_t tested{0};
            while (std::getline(input, line)) {
                if (line.empty() || line.starts_with('#'))
                    continue;
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (line.size() != 160)
                    throw std::runtime_error{"Every header must contain exactly 160 hexadecimal characters"};
                mining::GpuHeader header{};
                auto digit = [](char c) {
                    if (c >= '0' && c <= '9')
                        return c - '0';
                    if (c >= 'a' && c <= 'f')
                        return c - 'a' + 10;
                    if (c >= 'A' && c <= 'F')
                        return c - 'A' + 10;
                    throw std::runtime_error{"Invalid hexadecimal header"};
                };
                for (size_t i{0}; i < header.size(); ++i)
                    header[i] = digit(line[i * 2]) * 16 + digit(line[i * 2 + 1]);
                mining::GpuHash expected{};
                mercahash::HashV1(header, scratch, expected);
                const auto actual{backend->Hash(std::span<const mining::GpuHeader>{&header, 1}, [] { return false; })};
                if (actual.size() != 1 || actual.front() != expected)
                    throw std::runtime_error{"GPU hash differs from authoritative Core MercaHash V1"};
                ++tested;
            }
            if (!input.eof())
                throw std::runtime_error{"Header file read failed"};
            if (!tested)
                throw std::runtime_error{"Headers file contains no test inputs"};
            std::cout << tested << " additional headers matched authoritative Core hashes\n";
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
