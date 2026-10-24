# Native V2 Qt notification responsiveness

The Ryzen 5 4600G/WSL2 failure requires physical requalification. The cloud
reproduced a blocking wallet-notification defect, but its ordinary 30-second
baseline did **not** reproduce the reported 11,303 ms heartbeat gap. A passing
cloud run or GitHub CI does not establish that the original physical failure is
resolved.

The owner subsequently supplied successful Ryzen/RTX 3050 GPU-only and combined
harder-regtest qualification results, CUDA parity results and standalone CPU
benchmarks. Those are pre-fix physical results. This repair changes no GPU
kernel, context selection or CPU/GPU scheduling policy, and does not make eight
workers or 192 contexts a universal default. Post-fix physical checks remain
required.

## Diagnosed wallet-notification defect

Core delivers wallet transaction notifications while holding `cs_wallet`.
`TransactionTableModel` previously posted a separate Qt event for each change.
Its GUI callback synchronously called `getWalletTx()`, which waited for that
same wallet lock. A notification could therefore make the GUI wait for Core
wallet processing, even while the native miner continued accepting blocks.

A controlled regression uses a genuine mined V2 transaction and the real wallet
notification signal. A listener connected after the Qt handlers holds the wallet
lock for two seconds. On the old implementation, the GUI heartbeat gap reached
2,005 ms and Stop dispatch was delayed 1,900 ms. With the fix, the same controlled
case measured an 11 ms heartbeat gap and 2 ms Stop dispatch delay. This is a
controlled contention result, not a reproduction on the user's hardware.

The transaction model now retains notifications in order and posts at most one
Qt drain/continuation. Each drain handles at most 32 changes or four milliseconds
of work, yielding between batches. `tryGetWalletTx()` reports a busy wallet
without waiting; the front notification stays pending and is retried after
20 ms. A missing transaction is distinct from a busy wallet and retains the
existing warning behavior. No transaction change is dropped or coalesced away.

The queue mutex is released before wallet reads and model signals. Callback
state is shared independently of the Qt model lifetime; destruction disconnects
handlers and clears the guarded model pointer. Qt cancels continuations bound
to a destroyed model. Loading/rescan notifications use this same ordered queue.

Wallet balance polling, block-tip publication and mining statistics retain their
existing behavior. Their callbacks have opt-in timing scopes for diagnosis.
No hashing, difficulty, worker allocation, GPU backend, activation or transaction
authorization code was changed.

## Measurements and regression coverage

### Independent status-refresh lock defect

`tryGetTxStatus()` tried `cs_wallet` but then synchronously called
`chain.findBlock()` under `cs_main`. This violated its nonblocking contract.
The resulting block time and separate height were not used by
`TransactionRecord::updateStatus()`.

A separate controlled test mines a real V2 wallet transaction, leaves the wallet
available, and holds `cs_main` on another thread for two seconds. The GUI status
callback and heartbeat both stalled for 2,000 ms before the correction. After
removing the unused lookup, the callback took 0 ms and the worst heartbeat gap
was 40 ms. The test continues pumping Qt events while that other thread holds
the chain lock; its final join does not manufacture the measured gap.

Status now comes entirely from the wallet snapshot, including its processed tip
hash. The model keys its status cache to that actual snapshot, so it retries when
wallet processing trails the Core tip. Confirmation counts and reward maturity
are checked by the rapid-wallet regression. Transaction amounts, authorization,
and consensus are unchanged.

The notification, wallet-model, client-model, wallet-view, transaction-record
and wallet-interface source files are byte-for-byte identical between pre-V2
baseline `724428dbe551fb6bfb8c81272ef74647ebbba839` and the failed hardware revision
`dcf4b3a7e5f693610e2c4d2f7f6e3d27bcb7a77a`. Both blocking paths predate V2.
The native session's Qt publication behavior and provider block-submission path
were also retained through V2 integration; version selection and worker resource
lifecycle were extended. Faster V2 hashing exposes much higher block turnover
at the same easy regtest target. The controlled tests establish the two lock
defects, but do not prove which one dominated the historical Ryzen stall.

### Real mining controls

Cloud measurements used real MercaHash V2, easy V2-from-genesis regtest, one CPU
worker, a fresh native PQ disk wallet and Qt 6.8.2/GCC 14.2. The physical target
uses Qt 6.2.4/GCC 12 and must be retested separately.

| Cloud run | Main-phase accepted blocks | Worst GUI heartbeat gap |
| --- | ---: | ---: |
| Unchanged baseline, 30 seconds | 502 | 166 ms |
| Instrumented baseline, 30 seconds | 481 | 258 ms |
| Instrumented fix, 30 seconds | 519 | 107 ms |
| Final fix after both corrections, 30 seconds | 498 | 85 ms |
| V1 easy control after both corrections, 30 seconds | 64 | 135 ms |
| V2 harder control after both corrections, 30 seconds | 1 | 21 ms |

The V1 peer reached height 70. The harder V2 peer reached height one; both passed
independent `verifychain(4, 0)` and normal Stop/restart/Close checks. The harder
run performed 1,850 CPU hashes and finished in 34 seconds. It used the tool's
existing 600-second total deadline to accommodate stochastic harder-target
search, while retaining the same heartbeat and lifecycle limits. The primary
easy test retains its 240-second total watchdog. Harder difficulty is an
additional control, never a substitute for the rapid-block test.

The 519-block run executed 1,024 CPU hashes, stopped normally, and closed with all
mining resources destroyed. An independent peer synchronized to height 554 and
passed `verifychain(4, 0)`. These individual runs show no observed throughput
reduction; they are not a statistical hardware benchmark.

The final easy-target run executed 1,011 CPU hashes, stopped in 16 ms, completed
Close in 57 ms, and synchronized an independent verifying peer to height 534.
The final 18-target regression set passed in 195 seconds. Its Qt suite reported
23 passed and three existing six-worker resource skips on this two-CPU cloud
host. The final rapid-wallet runs verified 592 rows after Stop, reopened at
height 636 with 638 rows including a self-payment, and later verified 1,208 rows
at height 1,206. Confirmations and maturity matched the wallet/chain snapshot;
heartbeat gaps were 77 and 120 ms. Real GPU-only and combined discovery returned
`DEFERRED — NOT TESTED` because no supported physical device exists in the cloud.

Permanent tests:

- `MiningTests::walletNotificationContention` checks the controlled wallet-lock
  case, Stop with pending notifications, worker termination, 1,000 repeated
  updates, delete/new ordering, and exact final transaction rows.
- `MiningTests::transactionStatusChainContention` independently checks that a
  GUI status lookup and heartbeat keep running while another thread owns the
  chain lock and the wallet remains available.
- `test_mining_app-qt-rapid-wallet` starts the actual application twice against
  the same isolated disk wallet. Each run mines for at least 30 seconds and
  accepts at least 300 genuine V2 blocks. It checks complete transaction entry
  sets, uniqueness, amounts, hash/output ordering and the history view's sort
  order. It reconstructs owned unspent outputs from raw wallet transactions and
  compares mature/immature amounts with both Core and cached Qt balances.
- A genuine PQ self-payment checks both sent and received entries sharing a
  transaction/output index. Mining confirms that payment; reopening checks its
  persisted history and balances.
- Only after the ordinary mining phase, the test uses the existing rescan
  presentation queue to create deterministic Stop and Close backlogs. It drains
  and verifies the Stop backlog, then closes with a second nonempty backlog.
  The next application run verifies the persisted wallet before mining again.
  Core notifications remain enabled and all blocks/transactions are genuine.

An initial two-run check accepted 616 and 597 main-phase blocks. It verified
620 rows after the first Stop, reopened at height 653 with 655 rows including
the self-payment, and later verified 1,255 rows at height 1,253. Heartbeat gaps
were 120 and 102 ms; Stop left zero active workers; Close destroyed the mining
session. Existing heartbeat and lifecycle deadlines were not increased.

## Timing diagnostics

Set `MERCATURA_QT_TIMING=1` before starting the process. `QtNotifyTiming` lines
report cumulative call counts, total microseconds and maximum callback time,
aggregated per thread at most once per second. A first callback exceeding
50 ms is also reported immediately after it returns. Relevant names include
`transaction_lookup`, `transaction_update`, `transaction_index`,
`transaction_queue`, `transaction_notification_batch`, `wallet_balance_poll`,
`gui_block_tip`, `client_tip_notification`, `mining_page_status` and
`mining_statistics_poll`. Transaction queue latency is separate from callback
execution time. The application's JSON statistics also report pending wallet
notifications and the worst 10 ms GUI heartbeat interval.

Timing is disabled by default. It does not change the miner's rate or insert
delays into hashing, block submission or wallet processing.

## Ryzen/WSL retest

Use the existing WSL worktree and build directory. Preserve the original failed
directory `/home/gwestcot/projects/mercatura-v2-native-mining-regtest/mercahash-v2-cpu-mmskngrk`.
Keep production activation X unset. Fetch and fast-forward the development
branch, then rebuild the existing GCC 12/Qt 6.2.4/CUDA/OpenCL configuration:

```sh
cd "$HOME/projects/mercatura-v2-native-mining"
git status --short
git pull --ff-only project feature/mercahash-v2-native-mining
git rev-parse HEAD
MERCA_BUILD_DIR="$HOME/projects/mercatura-v2-native-mining-build"
cmake --build "$MERCA_BUILD_DIR" --parallel 6
```

First repeat the original workload with the original WSL display backend,
one CPU worker, easy V2 genesis, 30 seconds and the unchanged 240-second watchdog.
The tool always creates fresh isolated wallet/peer directories:

```sh
MERCA_QT_RESULTS=/home/gwestcot/projects/mercatura-v2-native-mining-regtest/fixed-qt-results
MERCATURA_QT_TIMING=1 python3 contrib/devtools/qualify-mercahash.py \
  --build-dir "$MERCA_BUILD_DIR" --mode cpu --schedule v2 --workers 1 \
  --seconds 30 --deadline 240 --output-parent "$MERCA_QT_RESULTS"
```

Run CPU-only harder difficulty as a separate control:

```sh
MERCATURA_QT_TIMING=1 python3 contrib/devtools/qualify-mercahash.py \
  --build-dir "$MERCA_BUILD_DIR" --mode cpu --schedule v2 --harder --workers 1 \
  --seconds 90 --deadline 600 --output-parent "$MERCA_QT_RESULTS"
```

Then exercise the permanent full-app history/backlog/reopen regression and the
existing native Qt mining lifecycle suite:

```sh
MERCATURA_QT_TIMING=1 ctest --test-dir "$MERCA_BUILD_DIR" \
  --output-on-failure -V \
  -R '^test_mining_app-qt-rapid-wallet$|^test_mining-qt$'
```

For retained wallet data, independent peer verification and JSON correctness
records from the extended case, run the qualification tool again with its test
mode enabled:

```sh
MERCATURA_QT_TIMING=1 MERCATURA_TEST_RAPID_WALLET=1 \
  python3 contrib/devtools/qualify-mercahash.py \
  --build-dir "$MERCA_BUILD_DIR" --mode cpu --schedule v2 --workers 1 \
  --seconds 30 --deadline 240 --output-parent "$MERCA_QT_RESULTS"
```

Require a successful summary, genuine accepted blocks, zero rejected solutions,
the existing sub-second GUI heartbeat/Stop/Close-handler checks, joined workers,
correct wallet/history records and independent peer verification. Retain each
generated directory, `qt.log`, `qt-statistics.jsonl`, `summary.json` and watchdog
diagnostics on failure. Compare callback time with notification queue latency;
a futex wait alone does not identify the responsible mutex.

After the easy CPU test succeeds, repeat adjacent physical GPU/combined checks
using the owner's previously qualified settings, without changing defaults:

```sh
MERCATURA_QT_TIMING=1 python3 contrib/devtools/qualify-mercahash.py \
  --build-dir "$MERCA_BUILD_DIR" --mode gpu --schedule v2 --harder \
  --seconds 90 --deadline 600 --output-parent "$MERCA_QT_RESULTS"
MERCATURA_QT_TIMING=1 python3 contrib/devtools/qualify-mercahash.py \
  --build-dir "$MERCA_BUILD_DIR" --mode combined --schedule v2 --harder \
  --workers 8 --contexts 192 --seconds 90 --deadline 600 \
  --output-parent "$MERCA_QT_RESULTS"
```

Use the same selected CUDA device as the successful physical runs. The cloud
has no physical GPU, so host arithmetic, binding and scheduling tests cannot
replace these physical checks. The standalone CUDA benchmark/live-mining rate
discrepancy is outside this responsiveness repair.
