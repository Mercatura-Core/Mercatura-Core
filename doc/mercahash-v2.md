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

`Consensus::Params::nMercaHashV2Height` is optional: `nullopt` means disabled.
No public network schedules V2. Only isolated regtest accepts
`-mercahashv2height=H` (H >= 1); `-1` disables it. Using the option on any
other network fails startup. Reuse the same option on restarts and reindex;
use a fresh isolated datadir when changing the simulated consensus schedule.
Genesis and network identity are unchanged.

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

GPU budgeting uses the selected scratchpad size, retains the existing runtime
and per-context overhead reserves, and retains the 256-nonce lease bound.
CUDA budgets from freshly queried free VRAM; OpenCL retains its conservative
quarter-of-total-memory and single-allocation limits. Total OpenCL memory is
never described as measured free VRAM. A backend must report the job's exact
version before its version-specific permanent-vector startup check can pass.
Every candidate is compared byte-for-byte against the authoritative CPU hash
before submission.

## Device implementations

CUDA compiles the existing specialized device source into distinct V1/V2
kernel instances. The single-lane fill, 32-lane binding reduction, cooperative
write barriers, staged mixing and stream-ordering optimizations are retained.
Only frozen geometry/domains and the matching host allocation/loop bounds
change. Existing chunk sizes and synchronization cadence are retained for
correctness; they are not claimed optimal for V2. Cancellation drains queued
work before buffers can be reused or released. Bounded OOM retries, reserve
revalidation and fatal driver/kernel error propagation remain intact.

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
