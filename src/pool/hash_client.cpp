// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
// Native integration-test worker. This is not a production miner or M4 client.
#include <pool/config.h>
#include <pool/job.h>
#include <pool/verifier.h>
#include <streams.h>
#include <util/strencodings.h>

#include <fstream>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc == 5 && std::string{argv[1]} == "--coinbase") {
        try {
            std::ifstream file{argv[2]};
            if (!file) throw std::invalid_argument("template file unavailable");
            std::string text{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
            UniValue t;
            if (!t.read(text)) throw std::invalid_argument("invalid template");
            const auto count = pool::Number::Parse(argv[3]).Uint64();
            if (!count || count > 1000) throw std::invalid_argument("recipient count invalid");
            pool::Window w;
            for (uint64_t i = 1; i <= count; ++i) {
                std::vector<unsigned char> key(32, 0);
                for (unsigned b = 0; b < 8; ++b)
                    key[b] = static_cast<unsigned char>(i >> (b * 8));
                CScript script;
                script << OP_2 << key;
                w.weights[HexStr(script)] = pool::Number{1};
            }
            w.total = pool::Number{count};
            pool::ParseTarget(argv[4]);
            auto job = pool::BuildJob(t, w, static_cast<int64_t>(count), pool::Number{count}, "regtest", argv[4], 1, 1, std::string(64, 'f'), pool::Now(), 120);
            UniValue out{UniValue::VOBJ};
            out.pushKV("block", pool::BlockHex(job.block));
            out.pushKV("manifest", job.manifest);
            std::cout << out.write() << "\n";
            return 0;
        } catch (const std::exception& e) {
            std::cerr << e.what() << "\n";
            return 1;
        }
    }
    if (argc != 5) {
        std::cerr << "Usage: pool-hash-test-client header share_target network_target ordinary|network\n";
        return 1;
    }
    try {
        std::string hex = argv[1], mode = argv[4];
        if (hex.size() != 160 || !IsHex(hex) || (mode != "ordinary" && mode != "network")) throw std::invalid_argument("invalid worker arguments");
        auto bytes = ParseHex(hex);
        CBlockHeader header;
        SpanReader{bytes} >> header;
        auto share = pool::ParseTarget(argv[2]), network = pool::ParseTarget(argv[3]);
        pool::Verifier verifier{1, 1, 160};
        for (uint64_t nonce = 0; nonce < (uint64_t{1} << 32); ++nonce) {
            header.nNonce = static_cast<uint32_t>(nonce);
            auto task = verifier.Submit(header);
            auto result = task->get();
            if (!result.error.empty() || result.cancelled) throw std::runtime_error("native hash failed");
            auto hash = UintToArith256(result.hash);
            if (hash <= share && (mode == "ordinary" ? hash > network : hash <= network)) {
                UniValue o{UniValue::VOBJ};
                o.pushKV("nonce", header.nNonce);
                o.pushKV("pow_hash", result.hash.GetHex());
                std::cout << o.write() << "\n";
                return 0;
            }
        }
        throw std::runtime_error("nonce space exhausted");
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
