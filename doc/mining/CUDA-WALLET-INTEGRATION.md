# CUDA wallet integration — Issue #4

This ports the supplied winning NVIDIA implementation into the existing native
wallet miner. There is no separate miner product, blockchain, fee, automatic
mining start, or GPU clock/power/fan control. Qt selectors, Solo, CPU candidate
verification, MercaHash V1 and ML-DSA wallet/transaction rules remain intact.

The final feature update adds bounded Automatic allocation fallback and actual
per-device context reporting. Only explicit device-buffer memory exhaustion or
loss of CUDA's required reserve permits retry; Manual never reduces its selected
count. Each probe releases all mining buffers and rechecks the unchanged memory
policy. Driver errors during retry cleanup abort fallback; final teardown remains
non-throwing and attempts any failed releases again. Handled CUDA allocation OOM
state is consumed before retry, so it cannot poison the first hash launch; a
different pending runtime error remains fatal. A binary search is bounded to 20 attempts at the unchanged 256-context
scheduler limit, including revalidation when available memory changes. Cancellation
and shutdown abort retries. Initialized streams/programs are reused; no mining
starts until the retained allocation passes the existing independent verification.
OpenCL retries only explicit device-buffer allocation failures and continues to
use conservative total-memory capability limits rather than claiming measured free
VRAM. The Qt tab shows actual active counts and both estimate and final count when
Automatic adjusts capacity.

Focused regressions cover every allocatable threshold from 0 through 256, cleanup
between failed and successful probes, fresh budgets, cancellation/shutdown predicates,
exact Manual counts, non-memory errors, fake-runtime CUDA allocation/reserve faults,
and independent multi-GPU statistics and Qt formatting. Physical NVIDIA/AMD memory
pressure and shutdown qualification remain hardware requirements.

**Feature scope is frozen after this update.** The next stage is hardware
validation, performance benchmarking, stability testing and bug fixes only. No
release, tag, merge or publication is authorized by this handoff.

## Source provenance and scope

Base: `631886dd5159614b628ced3fe4fa1e289ce19451` (`v0.1` when audited).
Work is isolated on `feature/cuda-wallet-miner-optimization`.
The developer supplied `mercatura-gpu-benchmark-sources.tar.gz`:

| Original source | SHA-256 |
| --- | --- |
| Archive | `8b6f5d347d3a23afe61646fd9f8d24832f78e120d933e3019abd299276f95fea` |
| `cuda_backend_mixgroup16.cu` | `7edc3525bae084e1eed9a9b029d1fa312d32b9d964234ad6e39b90ba4713c735` |
| `mercahash_gpu_specialized.h` | `cfb44d142683cead89c7ad85176b8cec8c1ae29c5b120b8640c0b130ea385993` |

`src/mining/mercahash_gpu_specialized.h` preserves the original arithmetic
**byte-for-byte**; only its three include-guard lines are renamed to
`BITCOIN_MINING_MERCAHASH_GPU_SPECIALIZED_H` for repository lint. The
backend retains the winning launch geometry and schedule: single-thread fill,
16,384-line fill chunks, fill/binding synchronization every 32 kernels, mixing
synchronization every 16 kernels, and default CUDA caching. The backend port
changes only its include path, removes benchmark timing/console output and its
unused phase dispatcher, describes production scheduling, and adds an exception
scope guard. The guard drains pending transfers/kernels before the host output
vector is destroyed, including launch/transfer/sync/callback failures.

The host regression test isolates the two implementations in separate namespaces
and uniquely renames its emulated kernel entrypoints to avoid collisions with
the real CUDA host object. The imported header has a distinct include guard.
After normalizing those three guard lines back to the original name, its bytes
match the original header checksum above. The ported file SHA-256 is
`bbcb18a44f6a0d3fd67c1a72d69f12426842582e1fd8e6627e1963bd71b7334f`.

The original `mercahash_gpu.h`, both backends' hash arithmetic, consensus/PoW/PQ
rules and Solo reward construction are unchanged. The benchmark's
`gpu_backend_budget_512.cpp` was **not** ported. Automatic now maximizes each
CUDA device's independently measured free-memory budget, with no 16-context cap
or half-VRAM reserve. It retains max(512 MiB, 5% of measured free memory), plus
64 MiB runtime allowance and 4 KiB per context beyond each 128 MiB scratchpad.
Manual reduces the count for memory conservation or efficiency tuning. CUDA
queries free memory immediately before allocation and verifies the reserve
afterward. AMD uses conservative quarter-global-memory/single-allocation limits
because portable OpenCL free VRAM is unknown; total memory is never treated as
measured free VRAM. The 256-context nonce-lease bound remains explicit, including
a UI notice when it limits capacity. See [memory policy and scheduler review](../native-gpu-mining.md).

## Benchmark reference versus wallet evidence

The developer reported 54.789 H/s for 180 seconds on a stock RTX 3050 6GB,
using 36 experimental contexts and 128 MiB per context, with 369/369 independent
CPU/GPU comparisons. The supplied `verify_cpu_gpu_36.cpp` uses batch sizes
1, 7, 16, 31, 32 and 36 (123 headers), repeated three times. These results were
not reproduced in the cloud and do not promise integrated-wallet throughput
with the integrated wallet's current automatic/manual memory policies.

## Permanent regression coverage

- `cuda_binding_tests`: executes the actual imported header through a host warp
  model. Compares single-lane fill across 16,384-line boundaries and 32-lane
  binding/shuffle results against the unchanged portable baseline, including
  two private scratchpads, guards, first/last blocks, 16-block ARX transitions,
  adjacent-block publication and the 64-block kernel boundary. This is a host
  model, not a CUDA memory-model or device-parity result.
- `test_cuda_backend_host` (Linux, CUDA build): links the actual production CUDA
  host object to fake runtime symbols in a separate test executable. Checks
  exact launch ranges, one-thread fill/32-thread binding and mix, grouped sync,
  full/partial inputs, cancellation at every callback with subsequent reuse,
  launch/synchronization/transfer/callback failures, partial-allocation cleanup
  and destruction. No GPU driver or device instructions execute in this test.
- `gpu_miner_tests`: retains simulated CPU/GPU scheduling, stale results, device
  failures and Stop/Start tests; covers maximum-safe automatic budgets, manual
  reductions, exact reserve boundaries, larger cards and OpenCL total-only limits.
  Mixed-size GPU workers prove independent automatic counts and non-overlapping
  full/partial nonce batches. Qt tests cover per-device automatic previews,
  manual limits, refresh/clamping, scheduler notices and small-headroom warnings.
  CUDA fake-runtime tests cover fresh queries after stream initialization, stale
  memory, post-allocation reserve loss and allocation cleanup.
- Wallet `mining_tests_solo`: adds a GPU-controller test with a CPU-reference
  backend test double. Real native Solo work, independent candidate rehashing,
  PQ reward ownership and actual regtest block validation/acceptance run through
  the GPU worker path, while hardware hashing remains explicitly simulated.
- Existing native/Qt tests cover CPU and combined-mode scheduling, cancelled
  unlock, wallet unload, shutdown, real competing tips and PQ reward spending.
- `mercatura-gpu-check` now generates six varied full/partial batches using the
  supplied deterministic generator. Each output is independently checked with
  Core `HashV1` across three repetitions by default. Optional file headers are
  likewise batched/repeated. Context requests retain safe production budgeting.

## Cloud results

The cloud uses Debian 13, GCC 14.2, Clang 19.1.7, CUDA 12.8.93, Qt 6.8.2
and CMake 4.4.4. Build tools/SDKs/development dependencies are isolated in a
workspace prefix. The initial clean Qt/OpenCL build and CPU-only rebuild use
`-j4`. The final combined CUDA/OpenCL Qt rebuild also uses `-j4`.

NVCC 12.8 with the cloud's glibc 2.41 fails in the SDK/system math headers
(`sinpi/cospi/sinpif/cospif` exception specifications), before project compilation.
No system or SDK headers were patched and no unsupported-compiler override was
used. Clang 19 compiles the real host/device object. For this compiler use the
supported numeric `CMAKE_CUDA_ARCHITECTURES=75`: CMake's separate `75-real` and
`75-virtual` entries leave a sticky Clang no-PTX flag. `cuobjdump` confirms both
the cubin and PTX with numeric `75`. The NVCC configuration for Ubuntu CI remains
unchanged, with its explicit real/virtual entries.

Executed in the cloud:

| Check | Result |
| --- | --- |
| Clean Qt + AMD OpenCL build, all configured targets, `-j4` | Passed |
| CPU-only Qt rebuild, `-j4`; `cpu_miner_tests` | Passed; all 16 cases |
| Combined CUDA + OpenCL Qt build/rebuild, all targets, `-j4` | Passed with Clang 19/CUDA 12.8 |
| CUDA image inspection with `cuobjdump` | Both `sm_75` cubin and PTX present with numeric architecture `75` |
| Native/Qt CTest selection | 16/16 suites passed, 102.89 s |
| Focused CUDA tests after include-guard correction | 2/2 passed |
| Fake-runtime host cancellation coverage | All 801 polling points plus restart; launch/sync/transfer/callback/allocation faults and cleanup passed |
| Functional regtest selection | 9/9 passed; 168 s wall time excluding cache setup |
| Native Solo proof/independent node acceptance | 102 production CPU-controller blocks, mature native PQ reward spend and fee accounting validated by `mercatura_solo_native.py` |
| GPU-worker native acceptance | Passed with explicitly CPU-reference-backed test double; actual Core PoW/PQ/block validation, no GPU execution |
| AMD device-source compiler checks | OpenCL 1.2 `-Werror` syntax and SPIR64 LLVM generation passed |
| File, include and include-guard lint; diff whitespace checks | Passed |
| GPU device listing | Exit 77: no NVIDIA/AMD hardware; parity/performance not executed |
| NVCC/GCC on cloud Debian/glibc | Failed in SDK/system headers as described above; not a project compile success |

The native selection includes `cpu_miner_tests`, `gpu_miner_tests`,
`mercahash_gpu_tests`, `mercahash_gpu_sync_tests`, `cuda_binding_tests`,
`worker_limits_tests`, `mining_statistics_tests`, `mining_tests_wallet`,
`mining_tests_solo`, `pow_tests`, `crypto_tests`, `mercatura_emission_tests`,
`mercatura_fixedpoint_tests`, `mercatura_controller_tests`, `test_mining-qt`
and `test_cuda_backend_host`.

The functional selection is `mercatura_mining.py`, `mercatura_mercahash_pow.py`,
`mercatura_solo_native.py`, `mercatura_coinbase_template.py`,
`mercatura_pq_send_receive.py`, `mercatura_pq_invalid_auth.py`,
`mercatura_pq_address_enforcement.py`, `mercatura_wallet_encryption.py` and
`mercatura_chain_reorg.py`. These use fresh isolated regtest data and test wallets;
no existing node/wallet/chain data or public network was used.

Compilation, host models and device-free skips must never be represented as
physical NVIDIA or AMD validation. NVCC on a supported OS and Windows builds
still need CI or platform execution; a successful Clang build does not claim
those results. Remote CI status is separate from these local results.

## RTX 3050 handoff (must execute separately)

Use a supported CUDA toolkit/host compiler, stock clocks, a fresh regtest datadir
and test-only PQ wallet. Keep all public networking disabled; any peer used for
acceptance checks must be an isolated local regtest peer. Do not use an existing
wallet, chain datadir or running node. Build a fresh directory with `-j4`:

```sh
cmake -S . -B work/build-cuda-qt -DBUILD_GUI=ON -DENABLE_IPC=OFF \
  -DWITH_CUDA_MINING=ON -DWITH_OPENCL_MINING=ON
cmake --build work/build-cuda-qt -j4
work/build-cuda-qt/bin/mercatura-gpu-check --list
work/build-cuda-qt/bin/mercatura-gpu-check cuda:0 test/data/mercahash-gpu-headers.txt
work/build-cuda-qt/bin/mercatura-gpu-check cuda:0 --contexts 1 --repetitions 3
```

Repeat at automatic and several manually selected **safe** counts. Check full,
partial and repeated batches. Exit 77 on a device-free listing is a skip.
An explicit `--contexts 36` reduces the automatic count, matching Qt Manual.
Run it only when the real free-memory budget permits it; never
bypass the reserve/allocation checks to force the benchmark configuration.

Exercise actual Qt GPU-only and CPU+GPU mining: observe accepted blocks on the
active chain and an isolated peer, match coinbase rewards to the wallet's owned
native PQ destination, verify zero software fees, and validate mature reward
spending. Test asynchronous Stop during queued fill/bind/mix, repeated Start,
competing tips/job replacement, stale output discard, allocation pressure,
GPU/driver errors, buffer cleanup/reinitialization, wallet unload and node
shutdown. The fake-runtime suite does not replace these hardware checks.

Measure at least 180 seconds of integrated-wallet mining plus longer stress.
Record actual contexts, available VRAM, driver/toolkit/build, rates, UI/Stop
latency, errors and memory recovery. Report integrated results separately from
the original 36-context benchmark. Windows CUDA builds and physical AMD
qualification remain separate gates; no AMD hardware validation is claimed.

[Mining Preview proposal](MINING-PREVIEW-PROPOSAL.md) describes a possible future
pre-release. No release/tag/publication is authorized by this integration.
