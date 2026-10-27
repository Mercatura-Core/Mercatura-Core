// Copyright (c) 2022-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NODE_CHAINSTATEMANAGER_ARGS_H
#define BITCOIN_NODE_CHAINSTATEMANAGER_ARGS_H

#include <util/result.h>
#include <validation.h>

#include <cstdint>

class ArgsManager;

/** -par default (number of script-checking threads, 0 = auto) */
static constexpr int DEFAULT_SCRIPTCHECK_THREADS{0};

namespace node {
/** Script workers excluding the calling validation thread. Explicit positive
 * -par values remain independent of automatic CPU budgeting. */
int ScriptCheckWorkerCount(int64_t requested, unsigned int effective_cpus);
[[nodiscard]] util::Result<void> ApplyArgsManOptions(const ArgsManager& args, ChainstateManager::Options& opts);
} // namespace node

#endif // BITCOIN_NODE_CHAINSTATEMANAGER_ARGS_H
