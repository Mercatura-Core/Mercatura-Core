# MercaHash revision V2

V1 remains historical consensus: 134,217,728 scratchpad bytes, 2,097,152
64-byte lines, 131,072 mixing steps, one binding pass over 32,768 4096-byte
blocks. Its domains and output bytes are unchanged.

V2 uses 16,777,216 bytes, 262,144 64-byte lines (mask 0x3FFFF), 16,384
mixing steps, and one binding pass over 4096 4096-byte blocks (mask 0xFFF).
Both use alternating half-round fill, 16 unsigned 64-bit state lanes,
secondary touches every 16 steps, cross-lane mixing every 64 steps, SHA3
checkpoints every 1024 steps, and 256 final sampling reads. Arithmetic wraps
modulo 2^64. All external word encodings are little endian. Canonical header
serialization remains exactly 80 bytes; block identity remains SHA256d.

V2 domains are the exact ASCII sequences `MercaHash-v2/state/0`,
`MercaHash-v2/state/1`, `MercaHash-v2/checkpoint`, and `MercaHash-v2/final`,
without terminating NULs. Production HashV1/HashV2 freeze geometry, domains,
fill and workload independently of the development-only WorkParams API.

## Permanent vectors

`src/test/mercahash_v2_tests.cpp` fixes raw output bytes, without uint256
presentation reversal. Inputs are 80 zero bytes, byte[i]=i, 80 bytes of 0xFF,
and byte[i]=(37*i+11) mod 256. Three more vectors use the last pattern with
bytes 76..79 replaced by little-endian nonces 1, 2 and 3. Scratchpads have no
initial contents requirement and are fully filled on each invocation.

The first four V1 vectors remain unchanged. The staged device arithmetic
uses a separate SHA3 construction and separate integer/line implementation
for independent parity checks; CPU emulation is not physical GPU testing.

## Height selection and activation

`Consensus::Params::nMercaHashV2Height` is network-specific: `nullopt` means
V1-only, zero means V2 from genesis, and a positive height means a V1-to-V2
transition. The schedules are:

| Network | Genesis and mining schedule |
| --- | --- |
| Mainnet | V2 at height 0 and every subsequent height; no historical V1 period |
| Public testnet | Existing V1 genesis and V1 history; V2 remains unscheduled pending separate approval |
| Signet | Existing V1 behavior; no V2 decision or activation |
| Regtest default / `-mercahashv2height=-1` | Existing V1 genesis and V1-only mining |
| Regtest `-mercahashv2height=0` | Distinct V2-valid genesis and V2-only mining |
| Regtest `-mercahashv2height=H`, H > 0 | Existing V1 genesis; V1 below H and V2 from H onward |

Only isolated regtest accepts the CLI override. Using it on any other network
fails startup, so it cannot disable mainnet V2 or activate public-testnet V2.
Reuse the same option on restarts and reindex. Different genesis fixtures
require separate isolated datadirs; changing schedules is not a migration of
existing blockchain data. Public-testnet and signet genesis bytes are unchanged.

### Provisional mainnet genesis

**Mainnet launch genesis and launch difficulty are not finalized.** The existing
development header happens to satisfy V2 and is retained without mining a new
mainnet genesis. Its nTime, nNonce, nBits, block hash and Merkle root are all
provisional development values. Assertions and test vectors identify this fixture,
not final launch commitments. `HasProvisionalGenesis()` marks it explicitly, and
mainnet startup displays a warning. Final genesis mining and launch parameter
selection require a separate decision after implementation and optimization.

Bootstrap validates V2-from-genesis fixtures with height 0 explicitly. Mainnet
native jobs derive height 1 from genesis and select V2 for CPU and GPU workers.
Unknown height fails closed rather than authorizing a V1 fallback. The offline
grinder defaults to the network's genesis version and rejects V1 on V2-only
networks; transition schedules require explicit offline version selection.
The standalone `mercatura-genesis` development helper defaults to V2, accepts
`--pow-version 1` for historical V1 fixtures, and
marks its output as provisional. It does not select or finalize launch values.

The V2-only regtest fixture uses nonce 5, block ID
`f7086cc3933445280f0e03607dc8b1199a010111f161f11a0c86e0a6f76a9ffe`,
and V2 PoW hash
`65ae7d866f23a2dafe3fc406a024f9342673fe04ac7083bce28abc3b90460d02`.
Other regtest schedules retain their original genesis and vectors.

At H-1 the mandatory algorithm is V1, and at H and every later height it is
V2. This uses the candidate's parent on its own branch. It never uses the
active tip, timestamps, header versions or peer height claims.

Connected header batches derive height from a TREE-validated parent and
check every linkage before hashing. Presync and redownload continuations
use the locally tracked cursor descended from their known chain start.
HeadersSyncState's commitments, DGW checks, buffering and work thresholds
remain intact. When activation is configured, unconnected headers cause an
ancestry request and return before claimed-work accounting, caching,
presync processing or admission. Getheaders remains rate limited. Unknown
height never authorizes PoW acceptance. Exact indexed TREE headers can reuse
their historical PoW; prechecks alone create no validity cache entries.

Block acceptance, connection, VerifyDB/startup verification and template
validation supply the established parent/index height. The context-free
CheckBlock API fails closed without height when activation is configured.
Its memory-only successful-check cache is version-tagged. Reindex processes
ancestors before descendants, resolving each candidate's own height. RPC
mining also derives height from its candidate's known parent.

DGWv3, its 24-block lookback and clamps, the 150-second target, powLimit,
compact targets, minimum-difficulty shutdown and chainwork arithmetic are
unchanged. V1 targets in the lookback remain valid inputs. If effective
throughput rises by a factor R, initial expected spacing is approximately
150/R seconds until DGW adapts over its rolling window. Realized timing is
stochastic and subject to the existing clamps. This revision neither resets
difficulty nor introduces a minimum-difficulty exception. A separate change
would require measured evidence and explicit consensus coordination.

## Native mining and resource accounting

SoloWorkProvider resolves the assembled template's actual parent in the local
block index and stores its height and PoW version in the immutable job. A tip
replacement revokes the job. On a version transition, the controller cancels
old work, drains and joins workers, releases their resources, recalculates
budgets, and starts a fresh pool. The same process applies to a reorganization
back to V1. Nonce leases and cumulative statistics retain their existing
semantics; stale candidates cannot submit.

A CPU worker or authoritative GPU candidate verifier reserves scratchpad bytes
plus 32 MiB. V1 therefore reserves 160 MiB and V2 48 MiB. The overhead remains
conservative for thread stacks and allocator/session state; it is not a tuning
claim. CPU affinity/cgroup constraints and manual worker selection remain in
force. Automatic selection retains the existing half-logical-CPU heuristic
within the current memory bound. No new production worker ceiling is imposed.
Qt displays a version-aware preview; the actual provider/controller selection
is authoritative and independently checked.

GPU budgeting uses the selected scratchpad size and retains the runtime and
per-context overhead reserves. V1 retains its 256-context bound and original
256-nonce scheduling. V2 assembles those atomic leases into larger batches,
retaining unused lease tails, with resource-based resident capacity and bounded
execution windows. CUDA budgets from freshly queried free VRAM. V2 OpenCL uses
AMD's free-memory query when available, otherwise the conservative
quarter-of-total-memory policy; V1 retains the latter policy. Single-allocation
and address limits apply. Total OpenCL memory is never described as measured
free VRAM. A backend must report the job's exact version before its
version-specific permanent-vector startup check can pass. Every candidate is
compared byte-for-byte against the authoritative CPU hash before submission.
See [Phase G1](mercahash-v2-g1.md) for capacity, scheduling and hardware validation.

## Device implementations

CUDA compiles the existing specialized device source into distinct V1/V2
kernel instances. The single-lane fill, 32-lane binding reduction, cooperative
write barriers, staged mixing and stream-ordering optimizations are retained.
The frozen geometry/domains and matching host allocation/loop bounds select the
version. V1 chunk sizes and synchronization cadence are unchanged. G1 bounds
V2 launches using hardware execution windows and shorter mixing stages, with
cancellation checks between synchronized launches. These are responsiveness
bounds, not performance tuning. Cancellation drains queued work before buffers
can be reused or released. Bounded OOM retries, reserve revalidation and fatal
driver/kernel error propagation remain intact.

OpenCL compiles the portable source with an explicit `MH_VERSION=1` or `2`;
unsupported values fail compilation. Its existing 8-lane cooperative layout
and barriers are retained. CMake embeds the same source without altering the
existing MSVC-safe string splitting. Device hashing is fixed for the lifetime
of a backend; changing a job's version recreates and self-tests the backend.

`mercatura-gpu-check DEVICE --version 1` and `--version 2` independently run
permanent vectors, deterministic random headers, partial/full batches and CPU
parity. `--contexts N`, `--repetitions N`, and the optional 80-byte-header file
remain supported. `mining-bench [workers] [seconds] [cycles] [version]` supports
both algorithms with synthetic work and no node/wallet access.
