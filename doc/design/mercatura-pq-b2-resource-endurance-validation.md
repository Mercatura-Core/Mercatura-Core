# Round B2 resource, endurance and recovery validation

CI recovery and the bounded B2 qualification ladder completed successfully for
the configurations below. The machine-readable receipt is
`mercatura-pq-b2-evidence.json`. The automatic-validation GUI failure and extended
run's latency/sampling outliers remain explicit limitations, alongside the
separate cryptographic, platform and deployment gates.

## Repository and qualification boundary

The B2 branch is `hardening/pq-p2p-b2-ci-resilience`, based on B1
`10107ecdf837b16bf74f046b71dbae9df1f82f7a`. B1's qualified production/test
snapshot is `760df129c6d78b7855c0bc774379ae692e47e9bc`; the original integration
branch remains at `1380d473de01049404af2af99a81063f19789153`. Original branches
and history are preserved. The proposed Round A branch was absent at inspection.
Only `project` is used for publication.

CI recovery is recorded separately in [the recovery ledger](mercatura-pq-b2-ci-recovery.md).
The recovery source is `c0cd6f3f0091d2e8c6e119cf7b07e3ef1661525e`; its GitHub
gate completed successfully before sustained B2 execution. The final B2 tooling
and test snapshot is `61d7009edc1950a8294ac0133349fd7e75bf2fef`, independently
confirmed green in GitHub run `40311265804`. Final reports are a documentation-only
descendant; the publication receipt and Git history identify that exact SHA.
No crypto formulas, wire framing, transcript, consensus rules, canonical
fixtures, native-vendor sources or production resource bounds change in B2.
Resource additions are opt-in developer tools and tests. No public peers,
GPU qualification, B3 campaign or production deployment are included.

## Host and measurement model

The managed Linux host reports an Intel Xeon Platinum 8370C, GCC 14.2,
a two-CPU cgroup quota (`200000 100000`) and an 8 GiB memory ceiling.
Results do not predict performance on the owner's Ryzen 5 4600G.
Normal builds use the existing Release configuration. Instrumented builds and
compile contention are identified separately; timing from those runs is not a
normal-build performance baseline. Log timestamps are UTC.

The [resource model](mercatura-pq-b2-resource-model.md) records allocation
ownership, source grouping, admission, queue and worker bounds. Requested C++
heap, native scratch, allocator arenas, resident pages, virtual reservations,
stacks, socket buffers and whole-node cache growth are distinct measurements.
The new probe records heap retained after each connection cycle with its context
still alive, then verifies final context teardown. Whole-node samples include
wallet/chain activity and are not PQ-only memory measurements.

## Instrumentation and safety

The excluded `pq_resource_probe --churn=SESSIONS:CYCLES` accepts 1..8 sessions
and 1..64 cycles, uses two workers, enforces a calculated finite deadline,
512 MiB RSS check and a requested-C++-allocation budget. It exchanges 300
packets in both directions per session, crossing the 224-packet rekey boundary.
It checks authenticated sessions, fresh identity/session identifiers, pending
accounting, per-cycle retained allocations, thread/descriptor baselines and
final joined workers. Small instrumented ping throughput is reported separately
from bulk transport throughput.

`pq-node-benchmark.py --endurance-seconds=N` accepts 60..10800 seconds,
2..4 loopback regtest nodes and 1..10 second samples. The default aggregate RSS
budget is 3 GiB; each node has a 256-descriptor/96-thread guard, sixteen Core
connection slots and unchanged two-worker/32-queue/64-pending PQ limits.
Only the bounded ring topology is created. Its workload is valid wallet
transactions and RPC-generated blocks, paced reconnects, planned restarts,
temporary isolation and normal most-work recovery. It checks transport health,
matching session IDs/tips, full level-4 verification of all blocks and clean
child shutdown. RPC block generation is not native CPU mining.

The one-MiB aggregate-budget negative control stopped its own children as
expected. Development failures caused by duplicate binding, zero available
inbound slots and a payment below the minimum amount were harness errors, not
production defects. All failed development logs are retained locally. The
latest shutdown receipt explicitly checks every child process has exited.

An attempted 120-second three-node run was rejected by automatic approval
review while native CI was pending; it did not execute. The recorded response
was to stop that action and retain the CI gate. Static review, builds and short
regressions continued. Sustained workloads require a successful actual CI receipt.

## Capacity and fairness

Two opt-in cases in `pq_lifecycle_tests_capacity` passed locally: 846 assertions
in the ordinary run and 843 each in ASan/UBSan/LSan and TSan. The polling helper's
assertion count varies with completed iterations. Leak detection remained
enabled, with no suppressions. These cases are disabled in the ordinary short
B1 suite and explicitly selected for capacity qualification.
They use the excluded Linux-only fault/barrier runners. Native Windows/macOS
CI qualifies the ordinary suites and functional lifecycle paths; it does not
execute these additional Linux-specific capacity cases.

The fairness case exercises a shared-source/NAT group at four pending
admissions, rejection of its next admission, admission from an independent
source, sixteen anonymous admissions plus rejection, no token refund on lease
release, and actual 1.1-second source-token refill. It fills the bounded source
table to 256 entries and verifies overflow rejection. Test-only timestamp aging
exercises expiry while retaining a live charged source: the table reduces to
two entries and pending accounting returns to zero. This simulates the expiry
clock and is not a claim that 121 wall-clock seconds elapsed. Existing IPv4
exact-address and IPv6 /64 grouping remain unchanged. Persistence and pins do
not confer admission privilege.

The capacity case holds two actual workers before crypto calculation, fills
all 32 queue slots, rejects overflow, and fills 64 admission leases. An already
established healthy connection still carries a packet while workers are held.
Cancellation and terminal Stop complete all 34 jobs with no retained handshake,
response or admission owner; pending accounting returns to zero. Queue removal
timing is measured at first join, after the queued jobs are cleared. Worker
stack reservation is 2 MiB in total on this POSIX host. These finite tests do
not establish availability against unbounded sources or bandwidth.

## Execution status

| Qualification | Status |
| --- | --- |
| Short two-node harness development | Passed: 60.814s workload, three transactions/blocks, height 104, full validation and clean shutdown |
| Repeated bounded churn at increasing concurrency | Passed at 1/2/4/8 sessions, eight cycles each |
| Three-node persistent recovery development | Passed: 120.894s workload, restart, reconnects, most-work recovery, level-4/all verification, clean shutdown |
| Four-node ephemeral recovery development | Passed: 123.903s workload, restart, reconnects, most-work recovery, level-4/all verification, clean shutdown |
| Several-minute smoke | Passed: 300.149s workload, 12 payments/blocks, two node restarts, full verification and clean shutdown |
| Thirty-minute intermediate run | Passed: 1800.167s workload, three persistent-identity nodes, 66 payments/blocks, five restarts, most-work recovery |
| Several-hour extended run | Passed: 10800.572s workload, four ephemeral-identity nodes, 365 payments/blocks, 17 restarts, most-work recovery |
| Sustained native V2 CPU mining with PQ and restart | Passed controlled 120s mining phase, payment, mining-time reconnect, Stop/restart, full verification |
| Full-node ASan/UBSan including native C crypto | Passed bounded two-node smoke with leak checking enabled |

The 120-second recovery development runs overlapped sanitizer compilation.
Their latency/CPU figures are correctness evidence under contention, not the
normal-build performance baseline. The churn ladder below ran after compilation
and sanitizer execution finished, before the native-mining run started.

## Churn baseline

| Concurrent sessions | Cycles / completed handshakes | Peak requested C++ bytes | Context-alive bytes after every cycle | Median / p95 handshake ms |
| --- | --- | --- | --- | --- |
| 1 | 8 / 8 | 39,515 | 1,496 | 4.421 / insufficient samples |
| 2 | 8 / 16 | 74,561 | 1,688 | 5.510 / insufficient samples |
| 4 | 8 / 32 | 137,074 | 2,072 | 10.274 / 16.277 |
| 8 | 8 / 64 | 259,907 | 2,840 | 15.808 / 20.488 |

All four runs retained zero requested C++ heap after context teardown and
returned to their initial single-thread/descriptor baselines. Context-alive
retention was exactly constant across all eight cycles at each level, consistent
with the bounded source table rather than retained session state. Each of the
120 sessions delivered 300 packets in each direction: 72,000 deliveries in
total. Actual peak pending endpoints were 2/4/8/16 and peak queued jobs 1/2/4/8.
All configured budgets remained enforced. These are measured controlled windows,
not proof of the worst possible pending state across every node path.

The historical Stage A 2.93ms handshake and 164 MiB/s bulk figures use different
measurement conditions. This instrumented threaded adapter exchanges 8-byte
pings and includes allocation interposition. Those historical figures cannot
support a quantitative regression claim against this baseline.

## Normal-node smoke baseline

The two-node smoke command took 348.396s in total, including wallet maturity,
the 300.149s workload, full verification and shutdown. It ended at height 113,
with twelve confirmed relayed payments/blocks, two planned node restarts, no
logged PQ rejections and no watchdog failures. Every child exited cleanly.

| Sampled process metric | Node 0 | Node 1 |
| --- | --- | --- |
| Idle disconnected median RSS, MiB | 66.94 | 67.14 |
| Idle connected median RSS, MiB | 67.73 | 67.92 |
| Peak RSS, MiB | 119.86 | 120.31 |
| Peak threads / descriptors | 12 / 28 | 12 / 28 |
| Observed CPU seconds | 26.08 | 21.95 |
| Average CPU, percent of one core | 7.51 | 6.32 |

CPU averages include warm-up and final verification; they are not steady-only
mining utilization. With one connected endpoint per process, the observed idle
RSS increments were 804 and 796 KiB. These include ordinary network buffers,
crypto initialization and allocator behavior, not just PQ-owned session bytes.
Requested allocation windows and logical object sizes remain separate evidence.

Twelve socket connection samples had a median 267.843ms and maximum 340.195ms;
there are too few samples for a reported p95. The 230 healthy RPC batches had
a median 2.903ms and maximum 544.531ms. After establishment, the sampled pending
count was zero. Steady node-0 quarterly median RSS was
122188/122288/122396/122400 KiB. Node 1 was deliberately restarted twice;
its later process loaded the retained chain/wallet with a higher cache baseline.
Per-PID steady windows in the JSON distinguish restart/cache effects from
retained sessions. All process/RSS/PSS/descriptor metrics are sampled at 1s;
brief transients may fall between samples.
Connection samples include RPC, TCP, version/pong readiness and the framework's
50ms polling interval; they are not pure PQ cryptographic handshake times.

## Thirty-minute persistent-node run

The three-node command completed in 1864.175s. Its workload ran for 1800.167s,
followed by full level-4/all-block verification in 9.674/7.683/8.425s. The final
height was 169, with 66 confirmed relayed payments/blocks, 56 paced reconnects,
five planned restarts and one temporary isolation/most-work recovery. All nodes
retained their configured persistent fingerprints across reconnects/restarts;
they remained explicitly unpinned and did not report trusted identity status.
No PQ rejection or resource watchdog failure was logged. All children exited
cleanly.

Across 1833 sampled rows, peak aggregate RSS was 355876 KiB (347.54 MiB) and
peak aggregate PSS was 326136 KiB (318.49 MiB). Sampled thread peaks were
12/12/13 and descriptor peaks 29/29/29. Node-0 steady quarterly median RSS rose
123372/123500/123712/123924 KiB; node 1 rose
90436/90672/90948/91176 KiB. This small whole-node increase accompanies the
growing chain and wallet history and is not attributed to PQ allocations.
The restarted node's individual steady process windows increased only 24..44
KiB between their first/last quarterly medians; its new process cache baseline
is kept distinct from an accumulating session owner. Requested PQ allocations
returned to zero in the separate churn probe. These finite observations do not
prove multiday memory stability.

The 71 socket-ready connection samples had median/p95/max
268.592/307.730/510.255ms. Among 1310 health batches, median/max RPC-batch time
was 4.815/1153.902ms. Six established endpoints remained authenticated and
encrypted; sampled pending endpoints after establishment were zero. Process
CPU totals were 61.86/49.33/42.21s, respectively 3.32/2.65/2.27 percent of one
core averaged across complete observed intervals, including startup and final
validation.

## Three-hour ephemeral-node run

The four-node run started at 2026-10-26 13:13:42 UTC and completed cleanly at
16:16:01 UTC, before the later workspace restart. Its actual workload lasted
10800.571599s. The complete command took 10938.690557s including startup,
wallet maturity, final verification and shutdown; its exit code was zero.
The final height was 468, with 365 confirmed relayed payments/blocks, 334 paced
reconnects, seventeen planned node restarts and one temporary isolation followed
by ordinary most-work recovery. Eight PQ endpoints remained authenticated and
encrypted during health checks. Reconnects used fresh session IDs and ephemeral
identities. No logged PQ rejection or resource watchdog failure occurred.

All four nodes selected the same final tip. Full level-4/all-block verification
took 22.764/22.723/21.730/22.938s. Clean shutdown and the exit of every owned
child were explicitly confirmed before the completed receipt was written.

| Sampled metric | Result |
| --- | --- |
| Process sample rows | 10400 |
| Peak aggregate RSS / PSS | 468892 / 425272 KiB (457.90 / 415.30 MiB) |
| Peak threads per node | 12 / 12 / 12 / 13 |
| Peak descriptors per node | 29 / 29 / 29 / 29 |
| Steady first/last descriptors per process | 28 / 28 |
| Process CPU seconds | 434.56 / 324.40 / 267.74 / 262.55 |
| Average CPU, percent of one core | 3.97 / 2.97 / 2.45 / 2.40 |
| Socket-ready connection samples | 374 |
| Connection median / p95 / max | 271.821 / 440.031 / 12824.404ms |
| Healthy RPC batches | 7119 |
| RPC-batch median / max | 6.856 / 4672.441ms |

The three continuously running nodes' first-to-last steady quarterly median
RSS increases were 2388/3800/280 KiB. The sender/receiver wallets and chain
history grew across 365 payments; the relay-only node changed much less.
The restarted node had eighteen distinct processes; their within-process
quarterly median increases were only 12..40 KiB. All steady process windows
retained their initial descriptor/socket/thread counts. Together with the
separate zero-retained-allocation churn results and sanitizer checks, these
observations show no accumulating PQ session owner in the measured windows.
They do not prove a worst-case whole-node allocation bound or multiday stability.

Tail latency and sampling continuity require qualification. Seven of 374
connection observations exceeded one second; the maximum was 12.824s.
The sampler's nominal interval was one second, but a late gap reached 29.206s,
with several shorter gaps. Cgroup counters showed throttling around those
intervals; they do not conclusively establish the cause of every delay.
The nodes recovered, completed the workload and passed full chain verification.
These shared-host results establish no hard latency guarantee. Raw sample times,
per-process windows, connection observations and retained log hashes remain
available for review; a dedicated-host latency baseline is a separate follow-up.

## Native-mining investigation

The native qualifier now optionally requires a payment funded solely by its
native-mined coinbases, confirmation of the exact transaction on its independent
PQ peer, and a fresh matching PQ session after disconnect while the native miner
remains busy. Its original default mode remains available. The B2 invocation
sets one mining worker, a 3 GiB sampled aggregate RSS budget and a 600-second
total watchdog. Process sampling is every 0.5s during the mining phase; startup
and post-mining restart/verification remain covered by the total watchdog and
host cgroup limits, not that sampling interval.

The first 120-second V2 run failed the existing one-second GUI heartbeat
criterion: 1278ms. It mined 614 accepted blocks before Stop, with zero rejected
blocks, confirmed the payment on the peer, and established a fresh PQ session
while mining remained busy (5.793s including automatic reconnect/polling).
Stop completed in 180ms and Close in 296ms; all children exited. Peak sampled
aggregate RSS was 258,992 KiB. This run is a failure, not a passing mining gate.
The largest gap appeared at approximately 110s, well after payment/reconnect;
it is not attributed to transaction signing without evidence.

Logs show automatic validation created two additional script-check threads per
node on this two-CPU-quota host. A controlled retry uses ordinary `-par=1` on
both nodes, creating zero additional script-check threads. This changes the
fixture's validation concurrency, not validation rules or the GUI assertion.
The controlled retry passed in 218.753s total, with 120s requested native mining
before Stop. Qt recorded 920 accepted blocks before Stop and zero rejected
blocks; the short restart phase produced additional blocks, and both retained
chains converged at height 936 with matching exact tips and full level-4/all
verification. Stop completed in 33ms and Close in 71ms. The worst heartbeat gap
was 576ms, below the unchanged one-second assertion. Peak sampled aggregate RSS
was 258,804 KiB. Both processes exited, as did the reopened mining node.

Payment `8c4975cbde8a20f146277e358964f2064cb720becf51d856643d120c31ecd2d1`
confirmed on the independent peer in block
`3e8ee9b7742329830d2b71258d58b93b0a8be66867b976d731de2328d7baadf6`.
A fresh matching authenticated PQ session was observed 4.197s after controlled
disconnect, including automatic retry and 0.5s RPC polling, while Qt still
reported a busy miner and advancing CPU hashes. This is reconnect/recovery
latency, not a pure cryptographic handshake benchmark.

Automatic-validation GUI responsiveness over this longer workload remains a
known failed configuration. One controlled pass does not prove the cause of
the first run's scheduling gap or qualify every default/hardware combination.
Neither assertion thresholds nor production bounds were increased. These easy
regtest block counts are observed functional results, not throughput targets or
a prediction of 150-second main-network mining on the owner's hardware.

The same one-worker/`-par=1` fixture also passed the height-20 V1-to-V2
transition, native Stop/restart, PQ reconnect, exact tip agreement and full
verification at height 37. The command took 46.640s; Qt's worst heartbeat gap
was 106ms. Peak sampled aggregate RSS was 698,324 KiB, including V1 mining
state; it must not be represented as PQ handshake memory. This short transition
case does not repeat the native-funded payment requirement of the 120s V2 case.

## Sanitizer boundaries and remaining review

Focused ASan/UBSan/LSan and TSan instrument first-party PQ wrappers, protocol,
adapter, identity code and tests. They do not instrument the complete linked
native crypto implementations. An independent full-node ASan/UBSan build has
all 485 compile entries instrumented, including native ML-KEM/ML-DSA C and
first-party node code; assembly and external system libraries remain outside
that scope. Build flags are `-O1 -g1 -fsanitize=address,undefined`.

The full-node two-node smoke passed with `detect_leaks=1:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`, without suppressions, in the
explicitly approved compatible non-ptrace execution context. It executed
60.947s of ordinary PQ workload; the complete command took 292.605s including
startup/wallet maturity/full verification/shutdown. Full level-4/all-block
verification took 58.920s and 47.920s. Peak node RSS was 904.8 and 782.4 MiB;
no resource watchdog fired and every child exited cleanly. No ASan, UBSan or
LSan finding occurred. This extends runtime coverage to linked native C crypto;
it is a short instrumented smoke, not a sanitizer endurance or whole-node TSan
claim. Full-node TSan remains unqualified; focused first-party TSan passed.

Independent cryptographic/AKE review remains required. Windows persistent
identity storage remains deliberately unsupported and fail-closed; native CI
qualifies its explicit rejection and ephemeral recovery path. GPU, multi-host
network conditions, owner-hardware performance, public deployment and B3 remain
separate gates.

## Regression and resource-limit review

No production PQ resource limit or token rule was increased. The measured
1/2/4/8-session ladder remained well inside its requested-heap/RSS budgets,
capacity tests exercised the real two-worker/32-queue ceilings, and source
admission/refill/table-expiry behavior matched the existing policy. These results
provide no measured justification for raising those defaults. The 64 KiB target
is specifically PQ-owned pending state; the measured allocation windows and
static object sizes do not prove a complete worst-case whole-node bound.

A shared NAT can legitimately have more than four simultaneous pending users;
its excess attempts remain subject to ordinary source pacing. Anonymous peers
share a sixteen-pending pool. Persistent identities and pins receive no resource
privilege. These are documented availability tradeoffs, not perfect Sybil/DoS
resistance. Meaningful future admission-policy changes require separate review.

The recovery changes also passed focused first-party suites, adjacent networking,
crypto, script, wallet and V2 tests, copied-layout V1/V2/transition/B1/negative
functional paths, primitive/packet checks and the native mining regression.
Exact commands, source boundaries, exits and retained log hashes are in the JSON.
The excluded fuzzer entry point rebuilt; no new fuzz/B3 campaign is claimed.
Frozen protocol/fixture hashes and pinned vendor contents remain unchanged.

## Defects, failed attempts and corrections

| Finding or attempt | Disposition |
| --- | --- |
| Windows source-fixture lookup, macOS full-verification timeout and project lint/dependency cycle | Corrected in isolated recovery commits; both the recovery gate and final B2 source CI completed green. See the recovery ledger for reproducers, root causes and commit IDs. |
| Windows persistence test expectation | Corrected the test to assert the existing fail-closed storage boundary and exercise ephemeral recovery; POSIX retains full persistence/pin/rotation checks. No Windows storage protection was relaxed. |
| B2 harness duplicate bind, insufficient inbound slots and below-minimum payment | Corrected the fixture configuration/amount; retained failed logs. These were harness errors, not transport defects. |
| One-MiB negative RSS control | Expected watchdog failure and owned-child cleanup; it is not a successful endurance run. |
| Native automatic-validation GUI heartbeat, 1278ms | Retained failed configuration and unresolved cause. Explicit `-par=1` passed the same one-second assertion under the documented two-CPU budget; production code/assertions are unchanged. |
| Full-node/focused sanitizer findings | None in the completed, explicitly scoped runs. Uninstrumented dependencies and unexecuted whole-node TSan remain identified above. |

## Reproduction

Use a fresh, privately owned Linux regtest run directory and the repository's
normal build instructions. The qualified normal build uses GCC 14.2, Release
`-O2`, wallet/GUI enabled, CUDA disabled and OpenCL compiled without GPU runtime
qualification. `pq_resource_probe` and the Linux fault/barrier runners are
excluded developer targets and must be explicitly built. Do not rebuild or
relink the daemon during restart tests.

```sh
build=/path/to/configured-build
runs=$(mktemp -d)
cmake --build "$build" --target mercaturad mercatura-cli pq_resource_probe pq_b1_tests -j2
"$build/bin/pq_b1_tests" --run_test=pq_lifecycle_tests_capacity --log_level=message
for sessions in 1 2 4 8; do
    "$build/bin/pq_resource_probe" --churn="$sessions:8" || exit 1
done
```

Run the node ladder progressively: 300s/two nodes first, then 1800s/three nodes
with `--persistent`, inspect each completed receipt, and only then run 10800s/four
nodes. Use a distinct `--tmpdir` and port seed for each fixture. The extended
command used here is equivalent to:

```sh
python3 contrib/devtools/pq-node-benchmark.py \
    --configfile="$build/test/config.ini" --tmpdir="$runs/extended" --nocleanup \
    --portseed=957 --endurance-seconds=10800 --node-count=4 \
    --sample-seconds=1 --restart-seconds=600 --memory-budget-mib=3072
```

Native mining requires `test_mining_app-qt` and the host's Qt runtime/plugin
configuration. Its separate controlled V2 command is:

```sh
python3 contrib/devtools/qualify-mercahash.py --build-dir="$build" \
    --mode=cpu --schedule=v2 --workers=1 --seconds=120 --deadline=600 \
    --offscreen --pq --relay-transaction --memory-budget-mib=3072 \
    --validation-threads=1 --output-parent="$runs/native"
```

Retain generated evidence, inspect actual exit/status and shutdown fields, and
avoid using existing wallets/datadirs as fixtures. The same-host two-CPU quota
and eight-GiB cgroup ceiling remain part of this qualification configuration.
Sampling and RPC polling have overhead; no zero-overhead claim or precise
owner-hardware performance prediction is made.
