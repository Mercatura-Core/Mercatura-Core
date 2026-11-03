# MercaWeave v0.1 — WO07B automated simulation

Started 2026-11-03, America/Los_Angeles. Starting research HEAD
`e014a10efa17fa0a8ac053650cbede9960da055f`. Both worktrees verified clean;
original worktree remains on work at `4b88b619fb1b08c7f30112a38eb471880b2a6af4`.
No production source, activation, chainparams, existing datadir or network is changed.

## Production integration gap register (recorded before implementation)

| Gap | Missing call path | Current substitute | Covered / unavailable tests | Security implication and next work |
|---|---|---|---|---|
| 1 — grouped-v3 chainstate | Chainstate::ConnectBlock/DisconnectBlock -> actual complete group context -> CScriptCheck -> persistent undo/notifications/mempool reconciliation | WO04 owned synthetic View/Engine and WO07 guarded actual CheckInputScripts/CScriptCheck/UpdateCoins/ApplyTxInUndo components | Complete immutable proofs, real Script dispatch, cold/warm cache bypass, synthetic branches; full grouped P2P, block acceptance, persistent undo and wallet reconciliation unavailable | Unknown witness permissiveness is not ownership. Require a separate consistently compiled isolated full-node integration, caller/context plumbing, cache audit, invalid-block rollback, persistent undo/restart and differential multi-node tests before WO08 release claims. Affected sources: validation.cpp/.h, blockstorage, txmempool, validationinterface, script/interpreter. |
| 2 — grouped-v3 CWallet | CWallet protected ownership seed -> genuine WalletBatch records -> group derivation/issuance -> native signing/finalizer -> commit/notifications/rescan | WO05 research Wallet/Funding/GroupPSBT, WO06 Qt bridge/planner, WO07 genuine SQLite backend research records | Native encryption helpers, grouped signing/PSBT, atomic SQLite records/crashes/watch metadata; ordinary grouped wallet migration, native coin locks, rescan, balances and grouped Qt process lifecycle unavailable | No normal v3 custody or automatic broadcast is authorized. Separate integration must preserve one protected seed, authenticated schema/migration, locked lifecycle, rescan/issued ranges, actual CWallet PSBT and transactional coin reservations, then qualify reorg/restart/corruption and async Qt ownership. Affected sources: wallet/wallet.cpp, walletdb, spend, scriptpubkeyman, psbt and Qt WalletModel. |

These gaps remain open throughout WO07B. Research v3 validity is never described
as real-node consensus or propagation. No WO08 work is initiated.

## Preflight and intended bounded stages

Read the WO01–WO07 design reports and existing evidence. Reproduce the current
32-test standalone suites in Release, checked-STL Debug and ASan/UBSan.
Actual host: Debian/GCC x86_64, 3 CPUs, 10,207,480 KiB RAM, approximately
3.6 GiB free disk. Reuse existing ordinary node binaries and extracted SDK;
no rebuild/toolchain installation is planned initially. Limit build jobs to four
and native campaign concurrency to the host's memory budget.

Implement a Python package using BitcoinTestFramework/TestNode and actual wallet
RPC signing. Funding will create confirmed fanout inventories, then independent
transaction epochs. Keep off-chain exchange accounting distinct from deposit,
sweep, withdrawal and consolidation transactions. Persist seeded manifests,
per-transaction measurements, independent expected/observed accounting and
bounded checkpoints outside disposable node datadirs. Start with a small smoke
campaign, then 100/500/1000 stages; support larger requested counts and optional
extended modes without claiming unexecuted workloads. Exercise network outages,
partitions, conflicting branches, restart, native wallet/PSBT and bounded soak.
Use existing isolated v3 executables plus a genuine signed research workload
adapter for comparison; never put v3 output scripts into a node-process campaign.
