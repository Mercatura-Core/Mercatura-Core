# Native miner M2 integration and local qualification

Date: 2026-11-12. Repository: `Mercatura-Core/Mercatura-Core`.
Worktree: `/home/gwestcot/projects/mercatura-miner`.
Branch: `feature/native-miner-mercaweave`; starting commit:
`48a3a614b741db541d939acd50c3603d40f3ff2e`.

M2 integrates the frozen CPU and NVIDIA champions into the existing native
controller. CPU-only, NVIDIA-only and simultaneous CPU/NVIDIA mining passed real
Qt/Core qualification on the Ryzen 5 4600G / RTX 3050 under WSL2. This is local
platform qualification, not an external security audit or a claim about every
supported operating system. GitHub CI is left to the owner, as requested.

## Source identities and commits

The worktree was clean at the required start, with the expected upstream and
`gwestcot <34328160+gwestcot@users.noreply.github.com>` author/committer identity.
Only this development worktree and disposable test/build locations were written.
No history was rewritten and no other development/release worktree was modified.
All pushes use `project feature/native-miner-mercaweave`.

Implementation commits, in order:

- `aab4a966f36a17b99d952559c4d2d1eb5bb5ac77`: CPU batch integration, ownership,
  resource accounting, tests and Qt capability/resource reporting.
- `a57746a15d2218066de85a1719d4be6d3b78f499`: frozen CUDA champion and event tests.
- `e2becbdf4c1cc069708c347643080cbf003bb4aa`: preserve the qualified CPU `-O3`
  Release setting and correctly scope forced-scalar CMake source properties.
- `f8a150656eb147fb0127fedaee421950f3febe26`: native mode transitions, wallet
  unload, V2 maturity/spend/fee proofs and qualification instrumentation.

Changed production files comprise `mining/cpu_miner.{cpp,h}`,
`mining/worker_limits.cpp`, the new batch/RAII units, `mining/CMakeLists.txt`,
`mining/cuda_backend.cu`, `qt/miningpage.cpp` and the development mining benchmark.
Validation changes comprise the CPU/CUDA test files and CMake registration,
`qt/test/miningapptests.cpp`, `wallet/test/mining_tests.cpp`,
`test/functional/mercatura_solo_native.py` and the two devtools checkers.
Native mining documentation and this report/evidence complete the changed scope.

The report/evidence commit follows these code commits. Exact SHA-256 values for
changed implementation files are in
[implementation-source-identities.json](native-m2-artifacts/implementation-source-identities.json).
The final Git commit containing this document identifies the report itself.

Frozen CPU inputs:

| File in `cpu-campaign-phase2-20261111/champions/003-full-gcc` | SHA-256 |
|---|---|
| `mercahash_v2.cpp` | `95cfdcd09e437fa8b1fab4e0db55c5bf226790e8919d13ad40b4ab2a06e50b73` |
| `batch_api.h` | `c96e9b42e9adf164777f74899229eed9e2ed6af199bfc7a28bce95f7e6394b7c` |
| adapter reference `cpu_miner_batch.cpp` | `110c320fe8804444b344123c65aef877d838d2a1c4a98375132c93fecbef5918` |

Frozen CUDA inputs in `campaign-20261110/champions/005-full-resident-mining`:

| File | SHA-256 |
|---|---|
| `cuda_backend.cu` | `2a8ec9095e6f2d43b1c5186a85cda81321a13ad0c74fd54a12e43f268e8040fa` |
| `mercahash_gpu_specialized.h` | `7f72780d72f8d3a9ee7a8ef9dc8bcb2a59ead4bc7af1b8cef833fe74c8ef682c` |

All 23 entries in the CPU champion checksum manifest and all 79 entries in the
GPU snapshot manifest match. Research worktree status matches the inspection
snapshot. The specialized GPU header was already identical in this branch.
[Archive preservation evidence](native-m2-artifacts/archive-preservation.json)
records the checks and protected-source identities.

## CPU integration

`src/mining/mercahash_cpu_batch.cpp` is a separate mining-only translation unit
containing `b4-streamed-huge`. `src/crypto/mercahash_v2.cpp` and the authoritative
`mercahash::HashV2` entry point are unchanged.

The import removes the redundant standalone `HashV2` export, relocates the batch
header, rejects empty batches as well as oversized ones, reports AVX2/scalar
capability, and uses a bit-preserving `memcpy` helper where the installed MinGW
C++20 library lacks `std::bit_cast`. Arithmetic, streaming stores and their
`_mm_mfence()` before binding are retained. The
[CPU adaptation diff](native-m2-artifacts/cpu-champion-adaptations.diff) makes
these differences reviewable. The archived controller adapter was read as a
reference; it did not replace the production coordinator.

Production V2 CPU workers assemble up to four independent headers using the
existing nonce allocator and `V2NonceBatch` lease assembler. Partial leases,
noncontiguous leases, exhaustion and `UINT32_MAX` remain bounded by the shared
64-bit nonce cursor. V1 execution and the existing injected single-hash factory
are preserved. Template retention, worker generations, deadlines, cancellation,
statistics, candidate queuing and submission still use the existing controller.
Core authoritatively validates submitted CPU solutions; GPU target hits also
receive explicit authoritative equality/target checks before submission.

`CpuScratchpads` owns four separate 16 MiB allocations using standard aligned C++
allocation/deallocation through `std::pmr::memory_resource`. Two-MiB alignment
supports optional Linux huge-page advice and exceeds the 32-byte streaming-store
requirement. Failed/unavailable advice uses ordinary pages. Partial allocation
failure releases all prior allocations. Each worker has 64 MiB payload; the
96 MiB budget includes allocation/alignment, stack and other worker overhead.
Four allocations can require less than 8 MiB of extra alignment space in total;
that space is not necessarily resident. No consensus scratchpad was enlarged.

V2 GPU verification retains its ordinary 16 MiB context plus allowance, budgeted
at 48 MiB per device. V1 CPU/verifier budgets remain 160 MiB. Combined memory
limits subtract actual verifier budgets before dividing the remaining budget by
CPU worker cost. Dynamic CPU discovery, large processor counts and manual worker
selection remain intact. Qt reports 64 MiB payload, the 96 MiB worker budget and
AVX2/scalar capability without a layout redesign.

Only the mining batch unit receives `-O3` in GCC/Clang Release builds. Consensus
compilation and global ISA flags are unchanged. There is no global AVX2/Zen2
requirement. Dispatch checks CPU support; unsupported compilers/architectures use
the scalar path. The forced-scalar CMake object and standalone scalar verifier
were compiled with both disabling definitions visible in their actual commands.

## CUDA integration and ownership

The import is exactly `combo-slide2`, with production event naming/comments as
shown in the [CUDA adaptation diff](native-m2-artifacts/cuda-champion-adaptations.diff).
It retains sixteen independent hashes per packed fill block, 16,384-line chunks,
word-distributed binding, eight independent hashes per mixing block, matching
occupancy queries, two timing-disabled completion events and approximately
50-microsecond not-ready polling. The circular fixed-batch experiment is absent.

Discovery, context/stream ownership, memory reserve/headroom checks, resident and
execution capacities, bounded automatic allocation recovery, manual contexts,
V1 kernels/scheduling and the vendor abstraction remain in place. Each event is
re-recorded only after its prior marker completes. Returns/exceptions drain the
stream while caller/output buffers are still alive. Construction and teardown
handle partially initialized events/buffers. No device reset was introduced.
Device-loss cleanup remains best effort through the existing nonthrowing CUDA
teardown; destructive physical driver-reset testing was not attempted.

## Local build and correctness gates

Hardware: AMD Ryzen 5 4600G, 6 cores/12 logical processors, approximately 13 GiB
WSL memory; NVIDIA RTX 3050 6 GiB. Linux kernel
`6.6.87.2-microsoft-standard-WSL2`; GCC 11.4.0; CUDA toolkit 13.4.92;
compute/sm 8.6 including PTX; Qt 6.2.4. NVIDIA-SMI reported UMD 13.4,
615.78.05 with Windows KMD 617.42. GPU power limit remained 70 W.

The main Release build enables wallet, daemon, CLI, Qt tests and CUDA; IPC is
excluded. Three/four build jobs bounded compilation memory. Core uses its
configured `-O2` and hardening; the CPU batch unit appends `-O3`, and CUDA uses
`-O3`. Exact production/scalar and control commands are retained in the artifacts.

Passed local gates:

- Permanent V2 and historical V1 vectors; native activation/version tests.
- All 1,024 archived CPU outputs and all 1,024 full final 16 MiB scratchpads,
  against unchanged authoritative hashing. This passed normal and forced-scalar
  builds at both initial `-O2` and final `-O3`.
- Archived 64-alignment, canary, reused-buffer, overlap, partial-batch and
  pre-entry cancellation checks; permanent 1–4 width tests, empty/oversized
  rejection, injected allocation failures and failed-page-advice fallback.
- Final-source GCC address/undefined/leak sanitizer corpus: 1,024 full comparisons,
  exit 0, empty sanitizer error log. That diagnostic build uses `-O2`; production
  `-O3` has its own corpus and native regression gates.
- Twelve focused final `-O3` CTest suites, including CPU workers/resources,
  scheduling/capacity/statistics, CUDA host runtime, activation and V2 rewards.
- Wallet mining suites, V1 native solo tests, PQ transport/policy and witness
  resource tests. The full unchanged `wallet_tests` suite passed in 859.39 seconds.
- Optional OpenCL backend SDK compilation and its existing fake-runtime host test.
  No physical AMD execution is implied.
- GPU/OpenCL arithmetic, GPU synchronization, CUDA binding and all 26 Qt Mining
  cases. The final group passed all five selected suites.
- Physical V2 CUDA at 8, 16, 17, 255, 256, 257 and 286 resident contexts:
  permanent vectors, randomized/full headers, nonzero nonces, full/partial and
  repeated batches. The 286-context boundary corpus produced 798 exact CPU/GPU
  comparisons. Historical physical V1 vectors and 11 comparisons also passed.
- Physical Compute Sanitizer memcheck/synccheck: zero errors; racecheck: zero
  displayed hazards, errors or warnings at 17 contexts.
- Fake CUDA runtime: 801 V1 and 293 V2 cancellation/restart checkpoints,
  every completion-event record/query fault position, partial event creation,
  not-ready polling/cancellation, execution/transfer/allocation errors, allocation
  recovery and resident/window coverage through 8,193 fake contexts. Queued-buffer
  release and event reuse/destruction are asserted.

Initial diagnostic failures were resolved or superseded transparently: ccache
needed access outside the sandbox; sandbox tracing prevented LeakSanitizer exit;
an early Clang sanitizer launch outside the sandbox failed at startup without
diagnostics, followed by a successful GCC sanitizer qualification. The new fake
runtime needed C++ kernel-name normalization; the activated assembler fixture
needed its normal height-1 dummy extranonce. A procfs sampling race during normal
Qt exit was fixed by requiring confirmed child exit. The first 300-second broad
wallet watchdog was insufficient; unchanged tests passed with a 1,800-second
watchdog. No assertions were removed to obtain a pass.

## Real blocks, modes, statistics and lifecycle

The actual `MiningPage` controls and `MiningSession` drove the final `-O3`
sequence `CPU → combined → CPU → GPU → combined → GPU`. Every mode change used
Stop and waited for cleanup; controls remain disabled during mining. Each of the
six sessions accepted a real block. Combined sessions require completed CPU and
GPU hashes, rather than merely a GPU launch. GPU-only sessions require zero CPU
mining hashes while retaining authoritative CPU candidate verification.

A fresh PQ-authenticated peer independently synchronized and verified the
six-block chain. The mining chain reopened after Qt shutdown, reconnected through
PQ and passed chain verification at the same tip. Every audited coinbase paid
one witness-v2 PQ destination the full commanded subsidy plus ordinary fees, with
only zero-value commitment outputs. All owned children exited.
[Final mode evidence](native-m2-artifacts/transitions-o3-summary.json) includes
peer/tip proofs, resource peaks and [mode events](native-m2-artifacts/transitions-o3-events.json).

A separate physical combined run exercised Stop/restart followed by real wallet
removal while both backends hashed. The unload handler took 37 ms; ownership and
resource cleanup completed in 481 ms, active workers became zero and GPU memory
reporting cleared. Close then completed in 133 ms. This initial run preceded the final
`-O3` setting. The final-setting repeat also passed: 530 ms unload handler,
631 ms cleanup, 632 ms maximum heartbeat gap, zero active workers/buffer bytes,
and 539 ms Close completion while an independent V2 validation test also ran.
See [final unload evidence](native-m2-artifacts/wallet-unload-o3-summary.json).

The shared 102-block native reward proof runs for both V1 and V2. V2 independently
imported all 102 public blocks into a second Core node, enforced depth-100/101
maturity, spent the owned PQ coinbase through real wallet signing, confirmed the
transaction and paid its six base units of ordinary fees in block 102. Every
coinbase/merkle/witness commitment is checked. Mining software fees remain zero.
The proof passed again with the final `-O3` production binary. The full public block proof stays in disposable local evidence; the committed
summary contains only public identifiers and counts.

Statistics count completed CPU lanes, including batches made stale during
execution. A cancelled GPU call returning an incomplete batch counts no hashes,
including any already finished internal windows. Fully returned GPU batches count
before stale-candidate rejection. CPU/GPU verification and consensus validation
hashes are not CPU mining hashes. Solutions, submissions, acceptances and
stale/rejected work remain separate. Completed-hash H/s is not a measurement of
all useful current-job work; no per-hash stale-throughput figure is inferred.

## Performance protocols and results

No tuning campaign was conducted. Initial `-O2` measurements are retained with
that label; the final CPU unit was corrected to the frozen research `-O3` setting
at the owner's request.

The controlled native comparison uses public synthetic V2 work, six CPU workers,
286 manual CUDA contexts, two-second jobs, a 10 ms GPU yield and three separate
15-second trials per configuration, including startup/Stop drain. Both binaries
use ordinary pages, enforced per process with `PR_SET_THP_DISABLE` and checked
through `THP_enabled`/`AnonHugePages`; no global page policy was changed. Original
CPU control hashing and integrated batch hashing both compile at `-O3`; their
controller/Core code uses `-O2`. The original hash control is an isolated object
linked into the benchmark, not a change to production consensus. CUDA source
controls both compile at `-O3`. Original controller/CUDA sources come from the
required starting Git commit. Exact control builds, commands and results are
retained in the artifact directory.

| Native configuration | Median completed H/s | Protocol |
|---|---:|---|
| Original CPU hash, `-O3` control | 242.81 | Six workers, ordinary pages, 3 × 15 s |
| Integrated CPU, `-O3` | 319.59 | Same CPU protocol |
| Original CUDA | 455.46 | 286 contexts, 3 × 15 s |
| Integrated CUDA | 1,290.58 | Same CUDA protocol |
| Integrated CPU + CUDA | 1,568.26 | Six workers + 286 contexts, 3 × 15 s |

The CPU comparison improves approximately 31.6%; CUDA improves approximately
183.4%. Combined median components were 303.81 CPU and 1,264.45 GPU H/s. They are
observations of shared operation, not a promise that combined throughput equals
standalone rates. The original shipping `-O2` CPU control measured 194.05 H/s;
that earlier result is not substituted for the matched `-O3` control above.

For the exact archived six-worker CPU controller protocol, final native trials
used three independent 20-second runs, four contexts/worker and default 2 MiB
alignment/huge-page advice. All three showed 393,216 KiB of huge pages. Native
rates were 325.37, 340.50 and 314.83 H/s (median 325.37). A contemporaneous unchanged
frozen controller binary measured 315.87 H/s. Its protocol matches the archived
six-worker, 20-second configuration; it is one control observation, not a new
three-trial research median. Both current results lie below the historical
382.05 H/s median. Current runs received about 108–110 process CPU seconds per
20 seconds, versus about 121 in the archive. WSL/host availability, clock and
memory conditions were not independently controlled or explained. The native
range overlaps the current frozen control; exact historical throughput is not
claimed.

The final CUDA research-protocol confirmation uses the identical
`audit-align-n286.txt` corpus (SHA-256
`45c1d13bd561ccdb8377300180d8f6f6a50c0e86ea8d05d309b50e13b11898e2`), 286 contexts,
one correctness repetition and a warmed 60-second timed interval with no
controller yield/template turnover. It returned **96,096 completed hashes in
60.0054 seconds: 1,601.46 H/s**, with 5.36 ms deadline drain and a full 286-context
execution window. CPU regression tests continued in the background; no other GPU
hashing workload was started. This is one 60-second observation. It does not
replace the archived ABBA mean or establish the unverified 600-second claim.

Native GPU startup includes reference-vector checks: approximately 1.38 seconds
before the first completed batch in a representative 15-second run. Native
yield, expired jobs and cancelled batches further distinguish controller rates
from warmed uninterrupted checker rates. Their individual costs were not isolated
with additional tuning experiments. The 10 ms yield remains adjustable in the
existing Mining tab.

## Memory, timing and power observations

Six V2 CPU workers own 384 MiB of payload. Matched final CPU peak sampled RSS was
about 388.4 MiB; combined peak sampled host RSS was about 486.7 MiB. The difference
between payload/RSS includes process baseline, worker stacks and allocation/data
overhead; it is not a separate portable allocation guarantee. The 96 MiB/worker
budget remains conservative. Actual GNU aligned allocation need not make all
alignment padding resident.

At 286 contexts, CUDA buffers total 4,798,352,416 bytes: 4,576 MiB of scratchpads
plus 68,640 bytes of headers/state/results. Runtime/headroom allowances and the
existing minimum reserve remain additional to those buffers. Combined Qt
qualification peaked below its 4 GiB host RSS watchdog and released all owned
workers/contexts. Native median Stop joins were approximately 114 ms CPU and
145 ms combined in the final ordinary-page comparison. These include completion
of admitted CPU batches and GPU draining, not just kernel cancellation latency.

GPU telemetry retained a 70 W board limit. Final combined observations covered
41–54 °C and up to 67.52 W, including startup/idle samples. These are GPU-board
measurements only. WSL exposes no CPU temperature/package-energy sensor here;
CPU-package or whole-system power/efficiency is not inferred from board power or
CPU TDP. An unrelated GPU workload occupied the board at initial inspection; it
was left untouched and physical qualification began after the board became idle.

## Consensus and MercaWeave preservation

Authoritative V1/V2 hashing, PoW selection, chain parameters, consensus optionals,
block assembly, solo payout handling and wallet mining destination selection are
byte-for-byte unchanged from the required starting commit. See
[protected-source audit](native-m2-artifacts/consensus-and-payout-preservation.json).
Public MercaWeave activation heights remain unset; public testnet V2 remains
17,000. Regtest-only `-mercaweaveheight` support, DGW, genesis, block capacity,
branch-local emission/subsidy rules, PQ authorization/networking, witness-v2
ownership, ordinary fees and payout behavior were not changed.

Explicit tests cover the wallet-owned v2 default before/after isolated activation,
solo rejection of inactive/malformed v3 rewards, and assembler rejection of
inactive/malformed v3 rewards. Activated valid v3 templates remain available.
The existing three-node `mercatura_mercaweave.py` test passed delayed activation,
immediate activation, reorg boundaries, mixed ownership/PSBT signing, grouped
mining payout, encryption/restart, backup and restore.

## Remaining platform qualifications

Physical Windows/macOS execution, MSVC dispatch, physical AMD OpenCL, other NVIDIA
architectures and destructive driver/device-reset recovery were not tested.
MinGW GCC 10 cross-compilation of the new scratchpad and batch units passed, but
is not Windows execution qualification. Forced scalar execution is real local
coverage; it is not a claim to have exercised a non-AVX2 physical CPU. GPU faults
and allocation/event failures were injected in the existing fake runtime; normal
physical allocation, execution, cancellation and cleanup were separately tested.
GitHub CI and any external security review remain with the owner.

## Reproduction and retained evidence

Build with the same options used locally:

```sh
cmake -S . -B build-m2 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=/usr/bin/g++ -DCMAKE_CXX_COMPILER_ARG1= \
  -DCMAKE_CUDA_COMPILER=/usr/local/cuda-13.4/bin/nvcc \
  -DCMAKE_CUDA_ARCHITECTURES=86 -DWITH_CUDA_MINING=ON \
  -DBUILD_TESTS=ON -DBUILD_GUI=ON -DBUILD_GUI_TESTS=ON \
  -DBUILD_DAEMON=ON -DENABLE_IPC=OFF -DWITH_CCACHE=OFF
cmake --build build-m2 --target test_bitcoin test_mining-qt test_mining_app-qt \
  mercaturad bitcoin-cli mercatura-gpu-check mining-bench test_cuda_backend_host -j3
```

A clean environment can additionally select `/usr/bin/gcc` explicitly for C to
avoid inheriting this machine's ccache-as-C-compiler wrapper. GPU/localhost tests
need the managed environment's appropriate runtime access.

```sh
python3 contrib/devtools/check-mercahash-cpu-archive.py \
  --campaign /home/gwestcot/projects/mercatura-v2-codex-cpu-research/research/cpu-campaign-phase2-20261111 \
  --output work/cpu-check
# Repeat with --scalar for O3 fallback; use --sanitize --optimization=-O2
# outside tracing restrictions to reproduce the recorded sanitizer build.
python3 contrib/devtools/qualify-mercahash.py --build-dir build-m2 \
  --mode cpu --mode-sequence cpu,combined,cpu,gpu,combined,gpu --harder \
  --workers 6 --contexts 286 --device cuda:0 --seconds 2 --deadline 480 \
  --output-parent work/qualification --offscreen --pq --validation-threads 2 \
  --memory-budget-mib 4096
python3 test/functional/mercatura_solo_native.py --configfile=build-m2/test/config.ini \
  --pow-version=2 --tmpdir=/tmp/mercatura-new-v2-proof --nocleanup
```

Use new datadir paths for every functional run. Do not point these tools at an
existing wallet/node. The mode tool also supports `--mode combined --unload-wallet`.
Full raw local logs, disposable chains and public block proofs remain under
ignored `work/m2` and isolated `/tmp/mercatura-m2-*` locations. Only small public
summaries, commands, manifests and relevant diagnostic logs are committed; no
wallet files, private keys, cookies or large binaries are included.
