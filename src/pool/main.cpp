// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <pool/config.h>
#include <pool/transport.h>
#include <sys/stat.h>

#include <atomic>
#include <csignal>
#include <iostream>

namespace {
std::atomic<bool> stopping{false};
void Stop(int) { stopping.store(true); }
} // namespace
int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "Usage: mercatura-pool configuration.json\n";
        return 1;
    }
    // Protect the SQLite database, WAL and newly created operational artifacts.
    umask(0077);
    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGINT, Stop);
    std::signal(SIGTERM, Stop);
    try {
        const auto config = pool::Config::Load(argv[1]);
        pool::Service service{config};
        std::cout << "Mercatura Pool protocol 1; permanent 0% fee; coordinator ready\n";
        pool::RunServer(service, stopping);
        std::cout << "Coordinator stopped; accepted work is durable\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Coordinator: " << e.what() << "\n";
        return 1;
    }
}
