Native Solo Mining (M7.3)
========================

Open the Mining tab (Alt+5), select CPU, GPU, or CPU + GPU, choose the CPU
worker count, and press Start Mining. Mining never starts when the application
or a wallet opens. Software fees, developer fees and automatic donations are 0%.
The native application supports Solo Mining. Standard `getblocktemplate`,
`submitblock`, and `getmininginfo` RPCs remain available to third-party miners
and independent pool operators.

All wallet pages share one session. While it runs, the status identifies its
wallet; stop it before starting for another wallet. The reward destination is
an owned, spendable native PQ receiving address. A previously marked mining
address is preferred, followed by an existing eligible receiving address. If
neither exists, the wallet creates one with label `Mercatura mining reward`.
Repeated sessions, including after label edits, reuse eligible addresses.

The destination is selectable and has Copy address and Show in address book
buttons. The latter filters and selects the address in the wallet's receiving
address book; the Receive tab's payment-request history is a separate view.
Privacy mode hides the address and disables both actions. Creating a destination
uses the normal wallet unlock flow only when necessary. The wallet relocks
following preparation. Locked wallets can reuse existing eligible destinations.
Workers receive no wallet private material. Coinbase maturity is unchanged.

Statistics
----------

The prominent rate is the combined CPU + GPU **10-second average**. The session
statistics row also shows the **60-second average**, session-average H/s and
session hashes. CPU and device rows provide less prominent live rates.
At startup the windows use the actual available session duration. Counters and
steady-clock timestamps determine the rates; irregular rate samples are never
averaged equally. Fractional window boundaries are interpolated between actual
counter observations. The sampler keeps roughly two observations per second,
outside hashing workers, and UI polling continues during blocking provider calls.

Session Runtime starts with the accepted Start request, includes preparation,
waiting for work and cleanup, and freezes when all workers finish after Stop or
failure. It updates at one-second resolution using a monotonic clock. Sessions
longer than 24 hours use `N d HH:MM:SS`. A new session resets runtime, hashes and
rates. Pauses remain part of elapsed time and reduce the measured rates.
Stopped live rates are zero; trailing-window and session averages remain visible.
Counter resets are handled without unsigned underflow or counting prior sessions.

CPU attempts and complete GPU batches count as completed hashes, including
completed work discarded during cancellation. Aborted/incomplete GPU batches
do not count. Solutions counts target hits; submitted counts submission attempts;
accepted counts solutions observed as the active tip after submission.
Processing success alone does not establish active-chain acceptance. The tooltip
reports processing failures and obsolete/stale candidates. Normal wallet
accounting reports immature and spendable rewards.

Worker budget
-------------

CPU workers are always selected manually in Qt, from 1 to the supported maximum.
The last selection is saved in Qt settings and restored with clamping when the
page reopens. All usable logical CPUs are allowed when memory permits; there is
no reserved-core deduction. On a 12-thread CPU, the CPU limit is 12. The controller
uses the same detector and rechecks limits at Start; changed resources can still
make Start fail safely.

Available concurrency uses hardware information, Linux CPU affinity/cgroup v2
quotas, Windows processor groups or macOS logical CPU information. Available
memory uses Linux MemAvailable and cgroup v2 ancestor limits, Windows available
physical memory or macOS free/inactive pages. Linux cgroup v1 and Windows job
object limits are not currently detected.

Each CPU worker is budgeted at 160 MiB (128 MiB scratchpad plus overhead). The
memory reserve is the greater of 512 MiB or one quarter of available memory.
Unknown memory permits at most one worker; known insufficient memory permits
zero. GPU candidate verifiers also consume a host-memory context per device. Combined
mode clamps CPU selection to the same remaining host-memory budget used by the
controller, without overwriting the saved manual preference during clamping.
Allocation/thread failures stop safely. Logical CPU counts do not predict the
optimal hashrate; choose fewer workers if throughput or responsiveness improves.
Automatic GPU discovery and automatic GPU context budgeting remain available.

Architecture and validation
---------------------------

`MiningController` owns a coordinator and persistent workers. Stop requests
cancellation without joining on Qt. The coordinator joins every started worker,
including after partial startup/failure, before releasing the provider. Shutdown
joins the coordinator before node destruction. Wallet unload uses a per-session
cancellation token and defers model destruction until cleanup completes.

`SoloWorkProvider` retains exact templates, coinbases, targets and cancellation
tokens. It uses Core's subsidy plus selected transaction fees and required witness
commitments. Session extranonces create distinct merkle roots. The controller
allocates disjoint nonce ranges without 32-bit wraparound. Solutions use normal
Core submission and blockchain validation. Mainnet/testnet wait for IBD and
connected headers; isolated regtest needs no peers. Jobs expire after 30 seconds.
MercaHash, difficulty, monetary policy, PQ authorization and validation are unchanged.

See [M7.3 build and validation](mining/M7.3-SOLO-REFINEMENT.md), the
[manual qualification plan](mining/M7.3-MANUAL-MINING-TEST-PLAN.md), and
[GPU backend documentation](native-gpu-mining.md).
