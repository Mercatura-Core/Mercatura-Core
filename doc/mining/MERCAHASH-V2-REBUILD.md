# MercaHash V2 rebuild

This branch starts at `724428dbe551fb6bfb8c81272ef74647ebbba839` and retains
[the accepted cloud baseline](MERCAHASH-V2-REBUILD-BASELINE.md). The original
V1 CPU implementation, `src/crypto/mercahash.cpp`, is unchanged. G1 `128ea7e`
is reference material for the approved algorithm, literal vectors, and reviewed
ancestry plumbing; its provisional mainnet activation policy is excluded.

## Consensus and isolated chains

Production activation height **X is unset**. Mainnet, testnet, testnet4 and
signet select V1 at every height. Both development flags are rejected on those
networks, including explicitly disabled values. No post-quantum transport change
is implemented or activated here.

| Regtest arguments | Required algorithm | Genesis SHA256d identity |
| --- | --- | --- |
| `-mercahashv2height=-1` (default) | V1 throughout | `8e2308efb3a16b126e69444329cc0ed81bea0596e99db1032ccd750e7028f685` |
| `-mercahashv2height=0` | V2 from height 0 | `f7086cc3933445280f0e03607dc8b1199a010111f161f11a0c86e0a6f76a9ffe` |
| `-mercahashv2height=20` | V1 below 20; V2 beginning at 20 | historical V1 genesis above |
| `-mercahashv2height=0 -regtestminingstress` | V2 throughout; harder fixed target | `9336d4e8d61fa82a08386748ac6c1020e200a3e9f0ce303253a1ea3322686802` |

Use a fresh, dedicated datadir for every configuration. The harder option requires
V2 from genesis. Its `nBits=1f0fffff` target gives approximately 4,096 attempts
per block, versus approximately two for ordinary regtest. It changes the initial
target and genesis fixture, not the MercaHash workload. Existing no-retarget
regtest consensus preserves that target for subsequent blocks. No startup mining
loop or validation bypass is involved. New genesis fixtures undergo the real
MercaHash computation and normal block checks during initialization.

The V2 easy fixture uses time `1788566400`, nonce `5`, bits `207fffff`; the harder
fixture uses the same coinbase/time, nonce `7935`, bits `1f0fffff`. Their V2 PoW
hashes (uint256 display order) are respectively:

- `65ae7d866f23a2dafe3fc406a024f9342673fe04ac7083bce28abc3b90460d02`
- `00096c8fef58c1c0c6833cfd49df8523228bb6709e90eab76c9b70bf18d78093`

Candidate height comes from the candidate's locally verified parent, or the
verified ancestry cursor during header presynchronization/redownload. Unknown
ancestry cannot select an algorithm when activation is configured. Header linkage
and block identities continue to use SHA256d. Exact TREE-valid indexed headers
can avoid repeated PoW work; prechecks do not admit unverified cache entries.
Block validation cache entries carry their checked hash version. Reorganization,
restart, reindex and VerifyDB use each block's own historical height. Chainwork,
DGWv3 and 150-second spacing retain their established definitions.

V2 has a private 16 MiB scratchpad, 262,144 64-byte lines, one binding pass over
4,096 4-KiB blocks, 16,384 mixing steps, secondary touches every 16 steps,
cross-lane mixing every 64, SHA3 checkpoints every 1,024, and 256 final reads.
All four approved `MercaHash-v2/...` domains and unsigned little-endian arithmetic
are frozen. Seven literal V2 vectors and additional V1 nonce vectors guard reuse,
independent allocations and unaligned byte buffers. Offline `bitcoin-util -powversion=1|2 grind` and `mercatura-genesis --pow-version 1|2` choose an explicit
algorithm without supplying a network-consensus override.

## Native mining ownership

Qt owns `MiningSession`, which owns `MiningController`. Start publishes a session
and launches the coordinator; wallet destination preparation, block assembly and
submission run on that coordinator. `SoloWorkProvider` derives the immutable job's
height and version from its actual template parent. Each CPU worker owns one
private authoritative `PoWHashContext`; it releases the old scratchpad capacity
when its version changes. V1 workers budget 160 MiB including overhead, V2 48 MiB.
CPU/cgroup/affinity/memory limits remain authoritative; there is no fixed CPU
worker ceiling.

Ordinary accepted blocks retain the worker pool. An actual algorithm change
cancels the old job, advances the worker epoch, joins all old workers on the
coordinator, rechecks resources, and constructs the replacement pool. This also
handles a reorganization from V2 back into V1.

RequestStop only publishes cancellation, updates short-lived controller state,
and wakes waiters. A joined background helper calls the provider's thread-safe
Interrupt method. The coordinator retains provider ownership until this helper
and every worker have joined, then destroys it. Qt never takes a provider mutex
or calls provider code during Stop. `Wait` remains the explicit shutdown barrier;
normal application shutdown requests node interruption before joining mining.
No detached mining threads are used.

The controller mutex protects only work pointers, queues and statistics. Provider
virtual methods and worker joins run outside it. Neither hashing nor Core chain
locks are acquired by the statistics snapshot. Rate sampling uses completed
counters at observation time, independently of coordinator/provider progress.
Qt's idle version preview uses the client model's cached height; a preview never
replaces the provider's authoritative version selection.

## Validation and release gates

The validation report accompanying the bundle records exact commands, durations,
counts and logs. Tests include real Qt native mining in all three schedules,
repeated Stop, displayed statistics and event-loop timing, actual application
Close with a disk wallet, and reopening that same chain/wallet. Six-worker logic
can run with deterministic lightweight hash seams on constrained hosts; physical
six-worker qualification is a separate result.

Physical RTX 3050 execution, target-machine CPU scaling, mixed hardware Qt
responsiveness, device parity, cancellation and resource recovery remain
**DEFERRED — NOT TESTED** until executed on the specified hardware. Host arithmetic
models, fake runtimes and source builds do not satisfy that release gate.

Selecting X, coordinating transport rollout, public-testnet activation and final
release qualification require a later explicit decision. An unset X does not
prevent V2 development or isolated native wallet/hardware tests.
