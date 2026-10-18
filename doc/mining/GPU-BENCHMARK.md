# Standalone CUDA MercaHash V1 benchmark

`mercatura-gpu-bench` is a development and performance qualification tool. It is
not a production miner: it hashes deterministic synthetic 80-byte headers and
never constructs or submits blocks. It does not initialize Qt, wallets, chain
state, network services, node configuration, or other running processes. It can
run independently of a Mercatura installation or blockchain synchronization.

## Download and run on an RTX 3050

Open the repository's **Actions → Mercatura standalone CUDA benchmark** workflow,
select a successful run for `gpu-benchmark` (or its pull request), and download
**mercatura-gpu-bench-windows-x64** from Artifacts. GitHub may require sign-in.
Artifacts appear only after the relevant build, software tests, and packaging
checks succeed; a queued or failed run has no qualified executable artifact.
Extract the entire archive and open PowerShell in `Mercatura-GPU-Benchmark`.

```powershell
.\mercatura-gpu-bench.exe --help
.\mercatura-gpu-bench.exe --list
.\mercatura-gpu-bench.exe --device cuda:0 --contexts 16 --duration 60 --warmup 5 --json > .\benchmark-results\rtx3050-c16.json
.\mercatura-gpu-bench.exe --device cuda:0 --contexts 20 --duration 60 --warmup 5 --json > .\benchmark-results\rtx3050-c20.json
.\mercatura-gpu-bench.exe --device cuda:0 --contexts 16 --duration 60 --warmup 5 --stage-timing --json > .\benchmark-results\rtx3050-c16-timed.json
```

Only request counts admitted by `--list`. A nominal 6GB card is not guaranteed to
admit 20 contexts under Windows. Progress appears on stderr; `--json` writes one
JSON document to stdout, including on failure. Check `$LASTEXITCODE`: 0 means
success, 1 means an argument, allocation, verification or execution failure, and
77 means `--list` found no usable NVIDIA device. No throughput is claimed after
an error. There is no separate persistent configuration or results-file writer;
redirection creates files only at the path you choose.

A supported NVIDIA driver is required. The distributed binary targets compute
capability 7.5 and later, includes native 8.6 code for the RTX 3050, and embeds
forward-compatible PTX. The CUDA Toolkit, Mercatura wallet and node are not
required on the benchmark machine. Driver 617.42 and the reported Palit RTX 3050
measurements still require qualification on the actual machine.

## Build and architecture

Sources:

- `src/bench/mercahash_gpu_bench.cpp`: standalone entry point, discovery and reports.
- `src/bench/mercahash_gpu_bench_support.cpp` and `.h`: parsing, deterministic
  inputs, CPU parity, measured batch loop and JSON helpers.
- `src/mining/cuda_backend.cu`: existing production backend with optional events.
- `src/mining/cuda_diagnostics.h`: benchmark-only diagnostics and factory.
- `src/mining/gpu_backend.cpp`: unchanged memory policy and permanent GPU vectors.
- `src/crypto/mercahash.cpp`: authoritative CPU V1 implementation.
- `src/test/gpu_bench_tests.cpp`: non-hardware tests.

With tests enabled, `gpu-bench-core-tests` reuses the existing consensus-vector,
GPU arithmetic, synchronization and mock-backend suite sources with normal Core
fixtures. It selects the required cases without compiling unrelated test sources.
The full `test_bitcoin` target is unchanged. MSVC 14.40 currently rejects existing
constant-expression initializers in unrelated `pow_tests.cpp` cases, so the
benchmark workflow therefore uses this focused runner.

The benchmark target links only the CUDA backend, shared backend support, crypto
and UniValue. It does not link `bitcoin_node`, Qt or wallet libraries. Production
construction supplies no diagnostics object and creates no timing events.
`mercahash_gpu.h`, launch sizes, chunk boundaries, synchronization points,
consensus arithmetic, mining controller and wallet behavior are unchanged.

`BUILD_GPU_BENCH` defaults OFF and requires `WITH_CUDA_MINING=ON`. Existing
presets and release configurations are unchanged. Tests also build a small
`gpu-bench-tests` executable in CPU-only configurations without requiring CUDA.

Linux x86-64, Ubuntu 24.04 example (supported CUDA 12.8 host compiler):

```sh
cmake -S . -B work/gpu-bench -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DWITH_CUDA_MINING=ON -DBUILD_GPU_BENCH=ON \
  -DCMAKE_CUDA_ARCHITECTURES='75-real;75-virtual;86-real' \
  -DBUILD_GUI=OFF -DENABLE_WALLET=OFF -DENABLE_IPC=OFF -DWITH_ZMQ=OFF
cmake --build work/gpu-bench --parallel 4 --target mercatura-gpu-bench gpu-bench-tests gpu-bench-core-tests
work/gpu-bench/bin/gpu-bench-tests
work/gpu-bench/bin/gpu-bench-core-tests --run_test=crypto_tests/mercahash_v1_vectors
work/gpu-bench/bin/gpu-bench-core-tests --run_test=mercahash_gpu_tests,mercahash_gpu_sync_tests,gpu_miner_tests
```

Select `CUDAToolkit_ROOT` and `CMAKE_CUDA_COMPILER` explicitly if needed. Set
`BUILD_TESTS=OFF` to build only the benchmark. CPU-only configurations must leave
`BUILD_GPU_BENCH=OFF`; requesting it without CUDA produces a configuration error.

For Windows x64, use a CUDA-supported MSVC toolset, CMake and Ninja. The workflow
selects MSVC 14.40, controlled CUDA 12.8.1 redistributables and the existing vcpkg
manifest's pinned baseline. In a matching x64 developer PowerShell, with
`VCPKG_ROOT` and `CUDA_PATH` configured:

```powershell
cmake -S . -B work/gpu-bench -G Ninja -DCMAKE_BUILD_TYPE=Release `
  "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static -DVCPKG_MANIFEST_NO_DEFAULT_FEATURES=ON `
  -DVCPKG_MANIFEST_FEATURES=tests `
  "-DCUDAToolkit_ROOT=$env:CUDA_PATH" "-DCMAKE_CUDA_COMPILER=$env:CUDA_PATH/bin/nvcc.exe" `
  '-DCMAKE_CUDA_ARCHITECTURES=75-real;75-virtual;86-real' `
  -DWITH_CUDA_MINING=ON -DBUILD_GPU_BENCH=ON `
  -DBUILD_GUI=OFF -DENABLE_WALLET=OFF -DENABLE_IPC=OFF -DWITH_ZMQ=OFF
cmake --build work/gpu-bench --parallel 4 --target mercatura-gpu-bench gpu-bench-tests gpu-bench-core-tests
ctest --test-dir work/gpu-bench --output-on-failure -R '^gpu-bench-'
```

Do not use `--allow-unsupported-compiler`. CUDA 12.8 with Debian 13 glibc 2.41 can
fail in toolkit/system headers; choose a supported combination instead of
patching system headers. A local CUDA 13 build is not proof of the CUDA 12.8
Windows build. macOS and non-CUDA vendors are outside this utility's scope.

## CLI and memory policy

| Option | Meaning / default |
| --- | --- |
| `--list` | Discovery only; combine with `--json` if desired |
| `--device cuda:N` | CUDA device identifier; default `cuda:0` |
| `--contexts N` | 0 automatic, otherwise explicit 1–256; default 0 |
| `--duration N` | Measurement seconds, 1–86400; default 60 |
| `--warmup N` | Warmup seconds, 0–3600; default 5 |
| `--stage-timing` | Enable timing events; default disabled |
| `--json` | Structured stdout instead of text |
| `--help` | Usage without device initialization |

Unknown, duplicate, malformed and out-of-range arguments fail. `--list` rejects
measurement options. Counts are never silently clamped. Discovery reports a
memory-derived manual limit; the backend checks fresh free memory again before
allocation and reports its allocation-time limit on success. Another application
can consume VRAM between checks, so admission does not guarantee allocation.

The production policy leaves at least 512 MiB and half of reported free VRAM
unused, budgets 128 MiB plus 4096 bytes per context, honors allocation limits, and
caps manual counts at 256. Automatic selection remains capped at 16. The benchmark
queries this policy rather than introducing its own budget. No reserve is relaxed.

| Contexts | Scratchpads |
| --- | --- |
| 1 | 128 MiB |
| 4 | 512 MiB |
| 8 | 1 GiB |
| 12 | 1.5 GiB |
| 16 | 2 GiB |
| 18 | 2.25 GiB |
| 20 | 2.5 GiB |
| 22 | 2.75 GiB |

These are proposed experiments. With exactly 6 GiB *free*, the current formula
admits at most 23 contexts. Reported device capacity is not reported free memory.
Contexts are independent nonce scratchpads within one stream, not independent
CUDA driver contexts. The launch remains one 32-thread block per input.

## Correctness and timing methodology

Every run first allocates a backend, executes all production permanent vectors
across its slots, and compares two full batches of distinct inputs and a short
batch against authoritative CPU `HashV1`. For a one-context backend the short
batch also has one input. Full batches use different inputs and reuse scratchpads.
Each slot is compared exactly, including slots with no target check. Any mismatch
or incomplete result fails before sustained measurement. CPU parity needs an
additional 128 MiB host scratchpad and may take significant startup time.

Inputs use a documented fixed byte pattern and explicit little-endian 64-bit
sequence in bytes 0–7, with its low 32 bits also in nonce bytes 76–79. All 80 bytes
are hashed; these are synthetic headers, not valid candidate blocks. Buffers are
reused and header generation requires no RNG, disk access or node serialization.
The sequence advances across warmup and measured batches; it fails on exhaustion.

Device setup, allocation, frozen vectors, CPU parity and configurable warmup are
excluded from sustained throughput. Warmup completes whole batches. Measurement
starts on the first measured batch and stops after the first batch that reaches
or exceeds the duration. **The final batch completes, all its hashes count, and
all its elapsed time counts.** Duration is a minimum, not a hard cancellation
limit. Zero warmup is allowed. Every measured batch must return all expected
hashes or the run fails. Counts never derive from launch counts or assumed speed.

`hashes_per_second = completed_hashes / actual elapsed_seconds`, using a monotonic
host clock. Elapsed includes generation, allocation inside the backend, driver
submission, synchronization, kernel execution, transfers and loop bookkeeping.
Batch latency covers the complete backend `Hash` call; it excludes header
preparation. Setup and CPU verification times are reported separately. No CPU
verification runs inside the sustained loop, so success qualifies the startup
corpus rather than every later measured hash.

With `--stage-timing`, CUDA events bracket each existing kernel or copy in its
existing stream. Per-kernel event durations are summed into initialization,
fill, binding, mixing and finalization totals; upload and download are separate.
The upload/init pair shares the existing first stream wait. No extra host waits,
chunk fusion, stream concurrency or changed launch boundaries are introduced.
There remain 1,154 kernel launches and 1,155 explicit stream synchronizations per
full batch. Warmup and verification timing counters are cleared before measurement.

Event recording and collection add overhead and can perturb scheduling. Always
retain a separate run without `--stage-timing` as the throughput baseline.
Disabled timings are JSON `null`, not zero. `host_synchronize_wait_ms` measures
host time inside the existing synchronize calls; it **overlaps GPU execution**
and is not additive with device timings. Pure host synchronization/driver overhead
cannot be isolated from these waits: `host_synchronization_overhead_ms` remains
`null`; use a host/device profiler timeline. CUDA events, particularly around
pageable host transfers, are stream intervals rather than pure DMA bandwidth.

## Output and profiling

JSON schema version 1 reports source revision, configured CUDA architectures,
driver/runtime API versions, device and memory information, requested/actual
contexts, setup/verification/warmup times, completed counts and wall-clock rates,
batch latency, optional device phase totals, and static kernel resources.
`cuda_execution_errors: 0` accompanies successful results only. Exceptions produce
`{"schema_version":1,"status":"error","error":"..."}` and a nonzero exit.
No successful performance result is emitted for a failed run. `source_revision`
uses Core's build-time Git revision generator: a 12-digit commit with a possible
`-dirty` suffix, an exact clean tag, or `unknown` without Git metadata. It refreshes
on incremental builds instead of retaining the CMake configure-time revision.

`registers_per_thread`, `local_bytes_per_thread`, static shared bytes and maximum
active 32-thread blocks per SM come from CUDA resource/occupancy APIs. Local bytes
include compiler-managed storage and do not measure dynamic spill traffic.
Occupancy is a theoretical per-SM resource limit, not achieved occupancy or a
claim that a small grid fills the GPU. Init and combined-stage kernel values can
differ; the combined kernel's resources cover all of its branches.

Useful commands with NVIDIA tools installed:

```sh
nsys profile --trace=cuda,osrt -o benchmark-launches ./mercatura-gpu-bench --contexts 16 --duration 10 --warmup 0
ncu --set full --kernel-name mh_stage --launch-count 3 ./mercatura-gpu-bench --contexts 16 --duration 1 --warmup 0
compute-sanitizer --tool memcheck ./mercatura-gpu-bench --contexts 4 --duration 1 --warmup 0
```

Profilers also capture mandatory self-tests. Select launches/ranges from the
measurement interval when investigating steady state; full profiling of all
startup launches is expensive. Use compiler `-Xptxas=-v` output and SASS/Nsight for
spills, achieved occupancy, stalls and scheduling. Profiled runs are diagnostics,
not comparable unprofiled throughput baselines.

Record commit and clean/dirty state, artifact/run URL, compiler/toolkit, configured
architectures, Windows build, exact board, driver package version, power policy,
clocks, temperature, free VRAM, other GPU activity, contexts, warmup and duration.
The JSON driver API version is not the NVIDIA package version (such as 617.42).
Run at least three repeats at each admitted count, alternating baseline and timed
runs under comparable conditions. Input generation is included in wall time; use
Nsight to verify it remains negligible. No RTX 3090 Ti performance is assumed.

## CI, packages and remaining device qualification

`.github/workflows/mercatura-gpu-benchmark.yml` builds Windows x64 and Linux x86-64,
runs benchmark software tests and relevant existing MercaHash/backend tests, and
uploads successful packages with README, results directory and license notices.
The Linux ZIP may need `chmod +x Mercatura-GPU-Benchmark/mercatura-gpu-bench`
after extraction because Actions artifacts do not preserve executable permissions.
CUDA runtime is statically linked. Windows uses the static MSVC runtime and
checks imported DLLs; Linux records and checks required OS C/C++ runtime libraries.
No GPU driver is redistributed. Dependencies/toolkits are build-time only.

The installer uses controlled NVIDIA CUDA 12.8.1 archives, verifies each against
NVIDIA's release-manifest SHA256, and retains component licenses. Windows reuses
the existing vcpkg baseline and static triplet. Existing release/wallet workflows
are unchanged. The workflow runs the focused original Core suites plus
benchmark-specific tests on both platforms; it does not claim to build or run every Core test. Hosted
runners have no assumed NVIDIA device: discovery exit 77
is an explicit hardware skip, never a successful device parity/performance test.

Before interpreting RTX 3050 results, run baseline and instrumented parity on the
actual board, context sweeps, repeated scratchpad reuse, sanitizer checks,
sustained thermal/power runs and allocation pressure. A benchmark invocation has
no cooperative Stop command; normal duration completion drains the final batch
and releases resources. Ctrl+C/forced process termination is not a tested miner
lifecycle path. This utility does not qualify wallet Stop/Start or mining-job
cancellation and does not change either behavior.
