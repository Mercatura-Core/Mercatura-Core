// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_POOL_TRANSPORT_H
#define BITCOIN_POOL_TRANSPORT_H
#include <pool/service.h>

#include <atomic>
namespace pool {
size_t TlsWriteLength(size_t& pending, size_t available);
void RunServer(Service& service, const std::atomic<bool>& stop);
} // namespace pool
#endif // BITCOIN_POOL_TRANSPORT_H
