Native CPU mining (M2)
======================

Open the Mining tab (Alt+5), select automatic or manual CPU workers, and press
Start Mining. Mining never starts when the application or a wallet opens. The
current mode is Solo Mining. Pool transport and coordination are outside M2.
Software fees, developer fees, and automatic donations are permanently 0%.

All wallet pages share one session. While it runs, the status identifies its
wallet; stop it before starting for another wallet. The payout is an owned,
native PQ destination recorded with the address-book label `Mercatura mining
reward`. Creating it uses the normal wallet API. If the wallet is encrypted,
the usual unlock dialog is requested only when a destination needs to be
created. It is relocked after preparation. An existing destination is reused
while locked. Renaming/removing its label can require a new destination.
Workers receive no wallet private material.

The hash counter counts completed hash attempts. Solutions counts local target
hits (including hits discarded during cancellation); submitted counts provider
submission attempts; accepted counts solutions observed as the active tip after
submission. Processing success alone does not establish active-chain acceptance.
An immediately extended block can therefore be absent from the accepted counter.
Existing wallet accounting reports coinbase maturity and spendable rewards.

Architecture
------------

`MiningController` owns a coordinator and persistent `std::thread` workers.
Workers are explicitly joined on the coordinator after cancellation, including
partial startup failures; the owner joins the coordinator before destruction.
`WorkProvider` supplies immutable public jobs, checks currency, submits solutions,
and exposes interruption/cancellation. Provider methods run on the coordinator;
`Interrupt` must be safe from another thread. A future pool provider can use the
same workers, without changing MercaHash or introducing wallet keys into jobs.

Each production worker constructs exactly one `PoWHashContext`. Its scratchpad
is allocated lazily on the first hash and reused across jobs. The engine calls
the authoritative hash path and compares its result with the provider's derived
target. An atomic 64-bit cursor allocates disjoint batches of 256 nonces over
`[0, 2^32)`. It does not wrap a 32-bit cursor.

The solo provider reconstructs the coinbase from Core's template fields,
including its actual SP-LT subsidy plus selected transaction fees, sequence,
locktime, witness reserved value, and required outputs. A random session prefix
and monotonic extranonce produce distinct merkle roots on refresh/exhaustion.
Solutions go through `BlockTemplate::submitSolution`; chain acceptance is
observed separately. Mainnet/testnet mining waits for IBD to end. Regtest does
not require peers or cooldown. Connected parents are checked every roughly
100 ms, and jobs expire after 30 seconds even if no tip change is observed.
Cancellation is checked between hashes; MercaHash itself is never modified.

Qt's shared `MiningSession` prepares the destination and provider off the GUI
thread. Core unload notifications set a session-specific cancellation token.
Stop requests cancellation without joining on Qt. The coordinator joins workers
and releases contexts/providers. WalletController removes the unloaded wallet
from navigation immediately, but defers model destruction until cleanup is
complete. Application shutdown uses a final join barrier before node destruction;
that barrier can wait for the current hash or block-processing operation.

Worker budget
-------------

Available concurrency uses hardware information, Linux CPU affinity/cgroup v2
quotas, Windows processor groups, or macOS logical CPU information. Available
memory uses Linux MemAvailable and cgroup v2 ancestor limits, Windows available
physical memory, or macOS free/inactive pages. Linux cgroup v1 and Windows job
object limits are not currently detected.

Each worker is budgeted at 160 MiB (128 MiB scratchpad plus 32 MiB overhead).
Reserve the greater of 512 MiB or one quarter of available memory, and reserve
approximately one eighth of CPU concurrency (at least one CPU when possible).
Manual selection is bounded by both remaining budgets. Automatic mode additionally
uses at most half the available concurrency. There is no fixed small worker cap.
Unknown memory permits at most one worker; known insufficient memory permits zero.
The controller rechecks budgets at Start and reports allocation/thread failures.
Logical processors are not a throughput prediction; performance autotuning is
outside M2. Operating-system allocation/overcommit behavior remains relevant.

Focused validation
------------------

Configure with wallet, GUI, and tests enabled, for example:

```sh
cmake -S . -B build-m2 -G Ninja -DBUILD_GUI=ON -DBUILD_TESTS=ON \
  -DENABLE_IPC=OFF -DMERCATURA_PUBLIC_TESTNET_RELEASE=OFF
cmake --build build-m2 --target bitcoin-qt test_bitcoin test_mining-qt \
  bitcoind bitcoin-cli -j 2
build-m2/bin/test_bitcoin --run_test=cpu_miner_tests,mining_tests_wallet,mining_tests_solo \
  --log_level=test_suite
QT_QPA_PLATFORM=minimal build-m2/bin/test_mining-qt
python3 build-m2/test/functional/test_runner.py \
  --jobs=1 mercatura_pool_coinbase.py
```

The unit tests cover concurrency/resource selection, nonce-space boundaries,
independent context use, counters, replacement/cancellation, restart, startup
failure cleanup, submission/acceptance separation, persisted/locked PQ payouts,
and actual native regtest mining. The dedicated Qt test covers startup, buttons,
shared sessions across wallets, locked reuse, unload, destruction ordering, and
shutdown. See the task's implementation report for commands actually executed and
platforms/behaviors not runtime-verified.
