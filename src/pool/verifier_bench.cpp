// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
// Repeatable bounded native hash load; no chain validation or miner credit.
#include <pool/job.h>
#include <pool/number.h>
#include <pool/verifier.h>
#include <streams.h>
#include <sys/resource.h>
#include <util/strencodings.h>

#include <chrono>
#include <deque>
#include <iostream>

int main(int argc, char** argv)
{
    try {
        const bool samples = argc == 4 && std::string{argv[1]} == "--samples";
        if (!samples && argc != 4) throw std::invalid_argument("Usage: pool-verifier-bench workers hashes queue | --samples header count");
        const auto workers = samples ? 1 : pool::Number::Parse(argv[1]).Uint64();
        const auto hashes = pool::Number::Parse(argv[samples ? 3 : 2]).Uint64();
        const auto capacity = samples ? 1 : pool::Number::Parse(argv[3]).Uint64();
        if (workers == 0 || workers > 64 || hashes == 0 || hashes > 100000 || capacity == 0 || capacity > 4096 || (samples && hashes > 256)) throw std::invalid_argument("bounded benchmark arguments required");
        CBlockHeader header;
        if (samples) {
            const std::string hex{argv[2]};
            if (hex.size() != 160 || !IsHex(hex)) throw std::invalid_argument("canonical header required");
            auto bytes = ParseHex(hex);
            SpanReader{bytes} >> header;
        }
        pool::Verifier verifier{static_cast<size_t>(workers), static_cast<size_t>(capacity), static_cast<size_t>(workers * 160)};
        const auto start = std::chrono::steady_clock::now();
        UniValue results{UniValue::VARR};
        std::deque<std::future<pool::Verification>> pending;
        uint64_t submitted = 0, completed = 0, refusals = 0;
        auto finish = [&] {
            auto result = pending.front().get();
            pending.pop_front();
            if (result.cancelled || !result.error.empty()) throw std::runtime_error("authoritative native verification failed");
            if (samples) {
                UniValue sample{UniValue::VOBJ};
                sample.pushKV("nonce", completed);
                sample.pushKV("hash", result.hash.GetHex());
                results.push_back(std::move(sample));
            }
            ++completed;
        };
        while (completed < hashes) {
            while (submitted < hashes) {
                header.nNonce = static_cast<uint32_t>(submitted);
                auto task = verifier.Submit(header);
                if (!task) {
                    ++refusals;
                    break;
                }
                pending.push_back(std::move(*task));
                ++submitted;
            }
            if (pending.empty()) throw std::runtime_error("verifier stopped unexpectedly");
            finish();
        }
        verifier.Stop();
        const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (samples) {
            std::cout << results.write() << '\n';
        } else {
            rusage usage{};
            if (getrusage(RUSAGE_SELF, &usage) != 0) throw std::runtime_error("resource measurement unavailable");
            UniValue output{UniValue::VOBJ};
            output.pushKV("workers", workers);
            output.pushKV("queue", capacity);
            output.pushKV("hashes", completed);
            output.pushKV("seconds", seconds);
            output.pushKV("verifications_per_second", completed / seconds);
            output.pushKV("mean_hash_us", verifier.microseconds.load() / completed);
            output.pushKV("queue_full_probes", refusals);
            output.pushKV("scratchpad_mib", workers * 128);
#ifdef __APPLE__
            output.pushKV("peak_rss_bytes", static_cast<uint64_t>(usage.ru_maxrss));
#else
            output.pushKV("peak_rss_bytes", static_cast<uint64_t>(usage.ru_maxrss) * 1024);
#endif
            output.pushKV("cpu_seconds", usage.ru_utime.tv_sec + usage.ru_stime.tv_sec + (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1000000.0);
            std::cout << output.write() << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
