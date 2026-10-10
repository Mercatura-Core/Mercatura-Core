// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
// Test-only native M2/M4 driver. No standalone mining product is installed.
#include <chainparams.h>
#include <key_io.h>
#include <qt/poolclient.h>
#include <util/chaintype.h>
#include <util/string.h>
#include <util/translation.h>

#include <QCoreApplication>
#include <QThread>

#include <iostream>

const TranslateFn G_TRANSLATION_FUN{nullptr};

int main(int argc, char** argv)
{
    QCoreApplication app{argc, argv};
    if (argc != 7 && argc != 8) return 2; // optional lost-ACK Stop/Start mode
    try {
        SelectParams(ChainType::REGTEST);
        auto destination{DecodeDestination(argv[4])};
        if (!std::holds_alternative<WitnessV2MercaturaPQ>(destination)) return 2;
        const auto port{std::stoul(argv[2])};
        if (port == 0 || port > 65535) return 2;
        auto state{std::make_shared<mining::PoolState>()};
        mining::MiningController controller;
        const mining::PoolEndpoint endpoint{argv[1], static_cast<uint16_t>(port), argv[3]};
        const auto start{std::chrono::steady_clock::now()};
        const auto factory{[&] {
            return std::make_unique<mining::PoolWorkProvider>(endpoint, "regtest", Params().GenesisBlock().GetHash().GetHex(), argv[4], GetScriptForDestination(destination), Params().GetConsensus(), state);
        }};
        if (!controller.Start(factory, std::stoul(argv[5]))) return 3;
        bool restarted{false};
        while (controller.GetStats().busy && std::chrono::steady_clock::now() - start < std::chrono::seconds{std::stoul(argv[6])}) {
            if (argc == 8 && !restarted && controller.GetStats().submitted > 0 && !state->Snapshot().connected) {
                controller.RequestStop();
                controller.Wait();
                if (!controller.Start(factory, std::stoul(argv[5]))) return 3;
                restarted = true;
            }
            QCoreApplication::processEvents();
            QThread::msleep(20);
        }
        controller.RequestStop();
        controller.Wait();
        const auto stats{controller.GetStats()};
        const auto pool{state->Snapshot()};
        UniValue output{UniValue::VOBJ};
        output.pushKV("hashes", stats.hashes);
        output.pushKV("accepted", pool.accepted);
        output.pushKV("rejected", pool.rejected);
        output.pushKV("stale", pool.stale);
        output.pushKV("connections", pool.reconnects);
        output.pushKV("active_workers", stats.active_workers);
        output.pushKV("failed", stats.state == mining::MiningState::FAILED);
        output.pushKV("restarted", restarted);
        // Provider error strings are fixed, sanitized local messages.
        output.pushKV("status", stats.status);
        std::cout << output.write() << '\n';
        return stats.state == mining::MiningState::FAILED ? 1 : 0;
    } catch (...) {
        std::cerr << "Native pool test setup failed\n";
        return 2;
    }
}
