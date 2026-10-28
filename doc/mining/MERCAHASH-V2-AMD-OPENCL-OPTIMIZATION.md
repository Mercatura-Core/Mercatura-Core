# MercaHash V2 AMD OpenCL optimization

Status: optimized implementation and feasible software qualification complete.
Physical AMD runtime parity and hashrate remain pending physical validation.

## Source and authority

Base branch: `integration/pq-p2p-p9-p11-required-testnet`.
Latest `project` refs were fetched before branch creation. Its verified HEAD was
`4e9ccbc7c29167a185f20ef05a2625ab6b451633`, equal to the supplied checkpoint.
Feature branch: `feature/mercahash-v2-amd-opencl-optimized`, in its own worktree.
Implementation source: `bcdeb5ae20d1c48f4dbe65a646395ad9a7019fe3`.
Final documentation publication: see final handoff receipt.

Existing worktrees and checkpoint branches were preserved. Changes were pushed
only to the new `project` development branch. No merge, official tag, release,
artifact publication, activation decision, public deployment or live-node or live-chain-data
operation is part of this task. Manual acceptance and physical hardware testing
remain with the founder/volunteer operators.

## Changed source and supporting files

- `src/mining/mercahash_opencl_v2.h`: dedicated AMD V2 kernels and binding reduction.
- `src/mining/mercahash_gpu.h`: opt-in serial-store/helper declarations for the
  AMD V2 instantiation; all shared arithmetic and default V1 behavior retained.
- `src/mining/opencl_backend.cpp`: V2 program/kernel selection, capability-based
  geometry, bounded ordered enqueue windows and cleanup.
- `src/mining/CMakeLists.txt`, `src/mining/opencl_source.h.in`: embed both sources
  with bounded MSVC string-literal chunks; register the isolated host API test.
- `src/mining/gpu_backend.h`, `src/mining/gpu_check.cpp`: read-only execution
  description for the existing parity/benchmark utility. Other backends retain
  the empty default; no shared nonce/scheduler policy change.
- `src/test/mercahash_opencl_tests.cpp`, `src/test/opencl_backend_tests.cpp`,
  `src/test/CMakeLists.txt`: source arithmetic/synchronization and actual host
  backend API tests, registered through existing conventions.
- `.github/workflows/mercatura-gpu.yml`: development branch qualification,
  offline V2 compiler checks, focused suites and native MSVC OpenCL coverage.

The CPU consensus implementation, all frozen outputs, CUDA backend/specialized
header, CPU mining implementation, PQ networking, wallets, genesis, DGW/emission,
target interpretation, capacity schedule and activation rules are unchanged.

## One specialized V2 execution path

The NVIDIA design supplies the architecture: split initialization/fill/bind/mix/
final launches, a single owner for sequential fill, independent binding-line
parallelism, private scratchpads and bounded execution windows. CUDA's warp
shuffles and warp barriers were not copied into OpenCL. The NVIDIA header is
unchanged. Shared arithmetic is reused from the existing portable header, with
an explicit AMD V2 instantiation. No A/B/C implementations or tuning modes exist.

Each active context owns sixteen 64-bit state words and a private **16 MiB**
scratchpad. The selected V2 geometry and domains remain frozen: 262,144 lines,
4,096 binding blocks, 16,384 mixing steps and the original final sampling/SHA3.

Initialization, fill, mixing and finalization use one work-item per group. Fill
preserves the exact sequential feedback and line order. Its line writes no
longer require the portable cooperative-store barriers. Mixing also retains
every data-dependent primary/secondary/checkpoint operation in order, with one
state owner and ordinary serial stores. The algorithm's cross-lane state mixing
is preserved; those sixteen algorithm state lanes are not GPU work-items.

Binding uses the largest power-of-two group from 8 through 64 permitted by the
device, work-item axis and binding kernel limit. Kernel-specific capacity and
local-memory requirements are queried before allocation/mining. Unsupported
geometry fails closed. Scalar kernels use local size one. No subgroup extension,
NVIDIA warp width, Wave32/Wave64 instruction or vendor-specific shuffle is assumed.
The reduction reserves 4 KiB of word-major, lane-contiguous local storage,
including on smaller workgroups. Adjacent lanes access adjacent 64-bit words;
no particular wave width or bank arrangement is assumed.

## Binding equivalence and synchronization

Blocks retain their original sequential order. Within a block, each lane owns
lines `lane, lane + group_size, ...` below 64. The previous/reference blocks are
outside the current block: block zero wraps to the last block; later reference
indices are strictly below the current block. Therefore parallel current-line
writes cannot overwrite another line's previous/reference input.

Every lane snapshots the block head and seed before the first global-memory
barrier permits any head replacement. The bound-line formula and unsigned
64-bit wraparound are identical to the serial implementation. Each lane XORs
its independent contributions; an explicitly synchronized local-memory tree
combines every lane into the lane-zero accumulator. XOR associativity preserves the original
block accumulator exactly. Every lane applies the same accumulator/state update
and the existing 16-block ARX schedule.

The global/local barrier after line stores publishes the full block before its
successor reads it. Each reduction step has a local-memory barrier. The final
local barrier prevents a fast lane from reusing the reduction array while
another lane is still reading the reduced values. Only uniform whole-group exits precede
barriers. The final binding ARX transition and leader state publication remain
at their original boundaries.

Initialization, fill, mixing, final serialization, domains, SHA3 padding and
target interpretation reuse the original source. Final outputs are compared to
the independent authoritative `mercahash::HashV2()`, without regenerating frozen
outputs. Intermediate comparisons additionally use the known portable baseline.

## Scheduling, resources and failures

The OpenCL queue is explicitly in order. Commands snapshot kernel arguments on
enqueue under OpenCL 1.2. Each window retains the same 194 launches: one init,
64 fill chunks of 4,096 lines, 64 binding chunks of 64 blocks, 64 mixing chunks
of 256 steps, and one final. At most four kernels are outstanding before an
explicit finish; cancellation is checked around bounded enqueue windows. This
reduces mandatory per-kernel finishes while preserving algorithm dependencies.

Resident context selection, conservative VRAM budget/reserves, allocation retry
rules, 16 MiB-aligned nonoverlapping views and context rotation are preserved.
Inputs/results retain blocking transfers and output order. The view guard drains
outstanding work before releasing it on cancellation or error. Cleanup releases
all five kernels, buffers, program, queue and context, including partial startup.
Cancelled partial batches never become results. Fresh jobs initialize their own
state and overwrite the selected scratchpad before binding/mixing.

Review additionally found that cancellation immediately after enqueue must perform
a checked drain before returning: otherwise a destructor-only drain could hide
an asynchronous error. The final source checks that drain, and a permanent
fault-injection regression requires the error to propagate.

Driver calls cannot safely be forcibly interrupted. Actual kernel duration,
watchdog behavior and Stop latency remain hardware qualification items. Four
queued kernels are a static work bound, not a measured millisecond guarantee.

The existing per-device frozen self-test remains mandatory before mining. A
mismatch disables that device. Every candidate is independently recomputed on
CPU before submission. CPU/NVIDIA workers and the shared nonce allocator retain
their behavior. No unchecked CPU/device fallback or altered difficulty is added.

## Executed qualification

| Local check | Executed result |
|---|---|
| Stable final-source Linux GCC 14.2 Release build | PASS: daemon, CLI, Qt, utility, test executable and OpenCL host test |
| Clang 19 OpenCL 1.2 V1 and optimized V2 | PASS: strict syntax and SPIR64 LLVM compilation |
| Exact CMake-embedded V2 program | PASS: matches tracked headers, four literals <=8192 bytes; strict SPIR64 compilation |
| Final-source kernel/CPU/CUDA parity rerun | PASS: 10 cases, 393 assertions after the layout refinement |
| Focused native mining unit run | PASS: 81 selected cases, 68,703 assertions; other cases outside the selector skipped |
| Separate frozen Core V1 vectors | PASS: one case, 33 assertions |
| Actual OpenCL host backend CTest with fake ICD | PASS: supported/illegal geometry, scheduling, errors and cleanup |
| ASan/UBSan optimized source host model | PASS: both cases, leak checking disabled |
| TSan optimized source host model | PASS: both cases; no CPU-reference-library or GPU instrumentation claim |
| ASan/UBSan actual host API backend test | PASS including post-enqueue cancellation drain failure; leak checking disabled |
| Native Qt mining unit suite, uncontended | PASS: 23 passed, zero failed, three six-worker rows skipped for actual CPU/memory budget |
| Existing checker `--list` | Exit 77: no AMD OpenCL GPU detected; physical verification unavailable |
| Native mining/PQ functional variants | PASS: five registered variants (V2 activation, native Solo, V1/V2/height-20 PQ transport), 424.337 seconds including cache setup |
| Repository lint and whitespace | PASS on source CI; final report checks recorded with publication |

The full local mining/Qt/functional regression runs first passed at
`d6f0d5b1600536048362822d8d500e19f5dc723e`. The subsequent kernel-only local-memory
layout refinement retains those host/CPU paths unchanged. Final-source kernel
parity, sanitizers, CMake embedding/build and exact-source CI are reported
separately; prior-source CI is not substituted for final-source CI.

The Linux host has a two-CPU cgroup quota and approximately 10 GiB memory. Its
host-model/sanitizer/Qt durations are test execution evidence, not AMD kernel
measurements or results from founder hardware. Existing worktrees, builds and prior qualification artifacts
were preserved; local test data are owned disposable fixtures outside Git.

The new arithmetic tests compile the actual optimized source as C++ with explicit
work-item IDs and workgroup barriers. They compare full scratchpads and state
after initialization, fill, binding and mixing, then compare complete digests to
Core for all seven permanent header patterns plus a repeat. They exercise both
resident slots, full/partial count bounds, irregular launch splits, scratch reuse
and guard words. The binding boundary test separately exercises sizes 8/16/32/64,
block-zero wrap, ARX and launch boundaries, and the final block.

The host API test compiles the real OpenCL backend as a separate translation unit
with a fake ICD. It checks exact launch bounds/order, seven resident contexts
across bounded two-CU full/partial windows, output ordering, all supported group
sizes, per-axis limits, unsupported geometry rejection, cancellation/restart,
launch/synchronization errors, partial allocation failures and drained cleanup.
Its output echo is a test mechanism, not hashing or proof of AMD execution.

The first harness revision included a `.cpp` file and failed repository include
lint; the backend was moved to its own normal test translation unit. Its initial
global loader interposition also conflicted with sanitizer startup. Test-only
link wrapping fixes that interference. The revised harness passes ASan/UBSan;
LeakSanitizer's process-inspection step cannot run in this restricted sandbox, so
no leak-sanitizer success is claimed. Explicit fake-resource counters pass.
ThreadSanitizer checks the optimized source's host workgroup model; it does not
instrument a GPU or certify OpenCL device-memory ordering.

[GPU/software CI 40630240475](https://github.com/Mercatura-Core/Mercatura-Core/actions/runs/40630240475)
completed successfully on the exact final source. All four jobs passed:

| Platform / job | Executed qualification | Result |
|---|---|---|
| Linux native, OpenCL ON | GNU 13.3; actual backend build, fake-ICD CTest, OpenCL 1.2 syntax/SPIR64, mining/reference suites, Qt, no-device checker | PASS |
| Linux native, OpenCL OFF | GNU 13.3; ordinary CPU-only build with portable/optimized source host models and Qt mining regressions | PASS |
| Windows native MSVC OpenCL | MSVC 19.51.36260.0, VS2026 x64; actual optional OpenCL host build, selected reference/GPU scheduler suites, no-device checker | PASS |
| NVIDIA CUDA compile / host runtime double | Real CUDA source compilation plus actual backend host-object scheduling/error/cancellation regression | PASS; no physical NVIDIA execution claim |

Both Linux Qt jobs reported 23 passed, zero failed and three skipped six-worker
rows because their actual host budgets cannot support those rows. The Windows
OpenCL job is headless; it does not claim a native Windows Qt or AMD device run.
SDK discovery found OpenCL 3.0 headers/libraries, while kernel language remains
strictly OpenCL C 1.2. An SDK is not an installed working AMD device/runtime.

[Main source CI 40630240462](https://github.com/Mercatura-Core/Mercatura-Core/actions/runs/40630240462)
also completed successfully on the exact final source. All seven required jobs
plus the runner-selection job passed. Linux x86_64, native Windows MSVC and native
macOS ARM64 each passed all 187 CTest entries and all 56 registered Mercatura
functional variants. Windows UCRT, macOS Intel and macOS ARM64 cross-compilation
jobs passed; cross-compilation is distinguished from native execution and does
not certify AMD hardware support. Repository lint passed.

The later documentation-only publication does not change the qualified source.
No earlier cancelled/superseded run is substituted for these final results.

The first native Windows OpenCL configuration inherited `WITH_ZMQ=ON` from the
MSVC preset while its minimal vcpkg manifest excluded ZeroMQ. Configuration
failed looking for PkgConfig through FindZeroMQ. The job now explicitly disables
optional dependencies absent from that manifest; no hashing code was changed.

## Volunteer AMD verification

**Actual AMD device compilation/execution, runtime reference parity, H/s, kernel
duration, occupancy, bandwidth, temperature, power and hashes/watt:
NOT TESTED — PHYSICAL AMD GPU REQUIRED.** Hardware hashrate is **unknown**.
CPU emulation, offline LLVM compilation, native host CI and fake-ICD results do
not close those gates. No speedup is predicted from the NVIDIA results.

On Linux, install the repository's normal compiler/Boost/libevent/SQLite build
prerequisites and OpenCL 1.2 headers/ICD development package (for example
`ocl-icd-opencl-dev`). Install a suitable actual AMD OpenCL runtime/driver for the
device. A normal backend build does not require ROCm. From the verified feature
source, use a new build directory:

```sh
cmake -S . -B work/amd-opencl -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_GUI=OFF -DBUILD_TESTS=ON -DENABLE_IPC=OFF -DWITH_ZMQ=OFF \
  -DWITH_OPENCL_MINING=ON -DWITH_CUDA_MINING=OFF
cmake --build work/amd-opencl -j4 --target mercatura-gpu-check test_bitcoin
work/amd-opencl/bin/mercatura-gpu-check --list
work/amd-opencl/bin/mercatura-gpu-check opencl:0 --version 2
work/amd-opencl/bin/mercatura-gpu-check opencl:0 --version 2 --benchmark-seconds 30
work/amd-opencl/bin/mercatura-gpu-check opencl:0 --version 1
```

For Windows with the repository's native VS2026 preset and an installed matching
MSVC toolchain/vcpkg, open the corresponding developer PowerShell. Other compiler
presets must match their installed toolchains; CI qualification below is specific
to its recorded preset.

```powershell
if (-not $env:VCPKG_ROOT) { throw 'Set VCPKG_ROOT to your vcpkg checkout first.' }
cmake -B work/amd-opencl --preset vs2026 `
  -DVCPKG_MANIFEST_NO_DEFAULT_FEATURES=ON '-DVCPKG_MANIFEST_FEATURES=wallet;tests;gpu-amd' `
  -DBUILD_GUI=OFF -DBUILD_TESTS=ON -DENABLE_IPC=OFF `
  -DWITH_OPENCL_MINING=ON -DWITH_CUDA_MINING=OFF -DWITH_ZMQ=OFF
cmake --build work/amd-opencl --config Release --parallel 4 --target mercatura-gpu-check test_bitcoin
& work/amd-opencl/bin/Release/mercatura-gpu-check.exe --list
& work/amd-opencl/bin/Release/mercatura-gpu-check.exe opencl:0 --version 2
& work/amd-opencl/bin/Release/mercatura-gpu-check.exe opencl:0 --version 2 --benchmark-seconds 30
& work/amd-opencl/bin/Release/mercatura-gpu-check.exe opencl:0 --version 1
```

Choose the actual discovered device ID. The checker runs permanent vectors and
independent CPU/reference comparisons before its optional genuine-device timed
benchmark. It supports full/partial/repeated batches and an optional public-header
file, e.g. `test/data/mercahash-gpu-headers.txt`. `--contexts N` requests an exact
safe count; omit it for Automatic. Exit 77 from `--list` means no supported device
is available and is not a successful hardware test.

Record exact source/build flags, device/driver/OpenCL versions, reported geometry,
allocated contexts/scratchpad bytes, completed hashes, elapsed time and actual
H/s. Test multiple safe counts, repeated jobs, cancellation, sustained Stop/Close
and a fresh job after cancellation. Kernel duration/watchdog and reset behavior
need separate physical observations. Use synthetic public headers; no wallet,
private keys or live blockchain data are needed. Native Qt manual acceptance and
any later release/deployment remain separately controlled by the founder.

## Remaining risks and conclusion

**Optimized AMD OpenCL implementation complete — hardware hashrate and actual
AMD runtime parity pending physical validation.**

Final qualified source: `bcdeb5ae20d1c48f4dbe65a646395ad9a7019fe3`.
Its tree is `f3782b793742a300963456f77a770da8ddea3157`. Source CI and the focused GPU
workflow are green. Frozen reference outputs and consensus remain unchanged.
The source branch is ready for review and founder/volunteer hardware acceptance;
it is not an official release, activation or deployment.

Real AMD drivers can impose additional compiler/register/resource limitations
that an offline SPIR64 compiler and host emulation do not expose. Actual AMD
parity must pass before accepting a device for mining. Workgroup selection is
conservative and capability based; optimal occupancy/context count is unmeasured.
The custom networking/release gates inherited from P9–P11 are not changed or
closed by this mining implementation.
