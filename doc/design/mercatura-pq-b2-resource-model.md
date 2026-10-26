# B2 resource model and measurement boundaries

This inventory is based on the B1 checkpoint plus the B2 CI factory/path fixes.
It is a source audit and measurement plan; numerical execution results belong
in the final B2 evidence/report, not in this inventory before tests execute.

## Production policy

| Resource | Implemented bound / behavior |
| --- | --- |
| Pending PQ endpoints | 64 per `Context`; lease begins at accepted transport construction, including silent inbound sockets |
| Known source | 4 pending, burst 4, refill 1 token/second |
| Global pacing | Burst 32, refill 8 tokens/second |
| Anonymous source | 16 pending, additionally subject to global pacing; no separate identity privilege |
| Source map | At most 256 entries; unused entries older than 120 seconds are pruned during eligible admission |
| Source grouping | IPv4 exact address; IPv6 /64 prefix; anonymous/proxied/unknown source uses anonymous accounting |
| Crypto pool | Two owned/joined workers, each reserving 1 MiB stack on POSIX |
| Queue | At most 32 shared `Work` entries; one outstanding job per transport |
| Deadlines | Preface 5s, record 30s, overall pending 120s, queued wait 2s, executing job 5s |
| Pending-state target | 64 KiB PQ-owned pending state per endpoint; excludes node/socket/stack/allocator overhead |
| Established data | Independent packet limits and ordinary Core message/send/receive budgets; the pending 64 KiB target is not an established-session or whole-process RSS limit |

These limits and token semantics are unchanged. Pinned and persistent identities
receive the same resource policy as ephemeral/unpinned peers. A shared NAT
address is an accounting group, not a unique user. Finite fairness tests cannot
establish availability against unlimited sources or bandwidth.

## Ownership and allocation stages

- **Before admission:** the socket, ordinary `CNode`/network structures, a
  `PQTransport` object and source string already exist. Shared context/pin
  references, inline counters/time points and empty vectors are initialized.
  Source grouping/pin lookup may allocate small strings. Kernel socket buffers,
  Core connection structures and these attempted-construction costs are not
  bounded by counting admitted PQ heap alone.
- **During admission:** the mutex-protected bounded source map may add an entry;
  the shared admission/control block and its source string are allocated before
  accounting is charged. Returning a lease releases pending accounting but does
  not refund pacing tokens. Source metadata can remain until expiry.
- **After admission:** a transport receives a bounded record, creates a shared
  `Work`, and transfers unique handshake ownership to a worker. Records and
  responses are public wire bytes. The queue holds shared job references, not
  additional copies of each handshake/private key. Worker mutation is exclusive
  until acquire-observed terminal completion.
- **Crypto state:** an ephemeral identity is generated only by an admitted job;
  persistent identities are immutable shared context owners. A handshake owns
  its KEM private state, shared secrets and derived-key RAII objects. Native
  crypto scratch and worker stack pages are separate from requested C++ heap.
  See the [B1 ownership map](mercatura-pq-b1-secret-ownership.md).
- **Completion:** key installation moves traffic keys into unique directional
  ciphers and releases pending admission. Established connections still own
  ciphers, message buffers and ordinary network state. Malformed data or failed
  allocation closes the affected transport rather than reusing partial cipher
  state. Status-copy allocation failure exposes no assurance.
- **Cancellation/Stop:** transport teardown marks jobs cancelled and drops its
  owner. Active native calls finish under worker ownership; failed/cancelled
  jobs discard owned secrets/leases before publishing completion. `Stop` removes
  and clears queued jobs, joins both workers, and serializes repeated callers.
  Caller-retained successful results/persistent contexts may intentionally retain
  their owned objects until those owners are destroyed.

The maximum handshake record is 4409 bytes, with fixed preface framing. The
existing conservative static-size assertion accounts for several simultaneous
record allocations, but it is not an allocator/RSS proof. B2's requested-heap
windows include the controlled probe containers as well as exercised C++
allocations; they do not count native `malloc`, allocator metadata, committed
stack pages, kernel buffers or every possible Core allocation path.

## Measurement layers

| Layer | Instrument / interpretation |
| --- | --- |
| Requested C++ heap | Dedicated excluded `pq_resource_probe` new/delete interposer; records allocation count, peak and retained bytes per tagged window; bounded churn mode enforces its explicit requested-byte budget |
| Logical sizes | `sizeof`/fixed record limits and source ownership; recorded separately from allocations and resident memory |
| Resident/physical attribution | Linux `/proc/<pid>/status` RSS/HWM plus optional `smaps_rollup` RSS/PSS/private pages; sampled at an explicit 1..10 second interval |
| Virtual reservations | VmSize and pthread-reported stack reservations; never interpreted as fully resident RAM |
| CPU | Process user/system ticks, process CPU clock for the probe, and cgroup CPU usage/quota metadata; cloud-host results do not predict the owner's Ryzen 5 4600G |
| Threads/descriptors/sockets | `/proc` process counts and fd-link classification, plus before/after teardown and actual process exit; probe self-scans exclude the observer's descriptor |
| Worker/queue/quota state | Public test-only resource snapshots and controlled B1 barriers/private test access; ordinary real-node peer health does not expose every internal queue transition |
| End-to-end latency | Paced connect/version/pong samples and healthy RPC batches; sample count, median/max and p95 only with at least 20 samples |
| Cleanup | Retained requested bytes, live accounting, joined workers, sanitizer results and clean process shutdown; allocator arena/RSS caching alone is not labelled a leak |

## Bounded harnesses

`pq_resource_probe --churn=SESSIONS:CYCLES` accepts 1..8 sessions and 1..64
cycles, two crypto workers, at most 16 pending endpoints, a calculated finite
watchdog (at most 207s), a 512 MiB RSS check and an explicit requested-C++-heap
allocation ceiling. Repeated source identities are paced at least 1.1 seconds
and global admission at no more than its configured refill rate. It exchanges
300 packets in each direction per established session, crossing a rekey boundary,
then verifies ownership/accounting/descriptor/thread cleanup.

The explicitly selected `pq_lifecycle_tests_capacity` cases fill the real
two-worker/32-job/64-admission bounds with barriers, deliver a healthy packet
while workers are occupied, cancel/stop and verify terminal cleanup. A second
case covers source/NAT/anonymous caps, actual 1.1s source refill, bounded map
size and expiry. Table filling/expiry use test-only timestamp aging without
altering tokens or bounds; they are not a claim that 121 seconds elapsed.
These opt-in cases are disabled in an ordinary short B1 runner invocation.

`pq-node-benchmark.py --endurance-seconds=N` extends the existing diagnostic
instead of introducing a second monitoring service. It accepts 60..10800 seconds,
2..4 nodes, 1..10 second samples, sixteen maximum Core connections per node (only the bounded ring is created) and a default 3 GiB
aggregate RSS watchdog. It binds P2P to loopback and uses fresh regtest datadirs.
Startup/verification have a finite additional watchdog allowance. CPU is bounded
by the configured host cgroup and documented worker choices. It separates idle,
warm-up, steady, restart, isolation and final-validation phases; checks real PQ
transactions/blocks, fresh reconnects, planned restarts and normal most-work
recovery; and stops its own children on failure. Raw samples remain local.

Native CPU mining uses the existing `qualify-mercahash.py` process-group watchdog
with an explicit one-worker/finite-duration CPU configuration and PQ-required
peer. RPC-generated endurance blocks are never represented as native mining.
Any concurrently run fixtures must declare their combined process/memory/CPU
budgets. No GPU, public-network, broad malformed-wire or B3 campaign is implied.
