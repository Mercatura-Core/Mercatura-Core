# MercaHash V2 GPU optimization: Phase G1

G1 starts at `f709bf1855b92cb7a85d5df57793cd43dbacccf4` on
`feature/mercahash-revision-v2`. It changes GPU capacity, nonce scheduling and
resource reporting. It does not change consensus, hash outputs, network
schedules, genesis parameters or wallet rules. No performance auto-tuner is
included. Capacity and optimal hashrate are separate measurements.

## Capacity and execution

V1 retains its 256-context limit, 256-nonce leases, GPU arithmetic, kernel
launch sizes and synchronization schedule. Its permanent vectors and existing
controller tests remain unchanged. The CUDA fake-runtime test retains the
original 770 launches, 31 synchronizations and 801 cancellation checkpoints.

V2 resident capacity uses 16 MiB scratchpads, a 64 MiB fixed allowance and
4 KiB per-context overhead allowance. CUDA and V2 AMD OpenCL with the optional
`cl_amd_device_attribute_query` free-memory extension reserve the greater of
512 MiB and 5% of measured free memory. Without that extension OpenCL reserves
the greater of 512 MiB and 75% of total memory; this is a conservative estimate,
not a free-memory measurement. Backends re-query immediately before allocation.
All four device buffers, the driver's largest allocation, host address/size
limits and representable context counts are checked. No V2 context constant
such as 256, 512 or 1024 limits the pool.

Automatic mode initially attempts the full safe resource budget. Recoverable
allocation failures trigger a bounded binary search with complete cleanup and
fresh budget queries. The returned allocation is revalidated. Manual counts
must fit and allocate exactly; failures are reported rather than silently
reducing the requested count. Driver, kernel, verification and cleanup errors
remain fatal. Other applications can change available memory after discovery,
so a resource estimate is not an allocation guarantee.

Resident capacity is independent of work submitted in one launch. CUDA uses
the minimum driver-reported active blocks per multiprocessor across the five
compiled V2 kernels, multiplied by the device's multiprocessor count. This is
an occupancy/resource query, not a benchmark or tuning search. Portable OpenCL
uses one context per compute unit as a conservative execution window. Grid,
work-size and existing kernel index limits further bound each window. Both
backends rotate through the whole resident pool, including its final partial
window. CUDA offsets its buffers; OpenCL uses aligned scratchpad sub-buffer
views. Neither strategy changes GPU arithmetic.

V2 fill and binding launches retain 4,096-line and 64-block stages; mixing uses
256-step stages. Each launch is synchronized and checks cancellation. A full
window executes 194 kernels. CUDA additionally synchronizes the output copy
(195 synchronizations). Cancellation/error paths drain queued work before
host input or device buffers can be reused. These bounds improve responsiveness
but do not qualify watchdog behavior on hardware; measure it on the target
driver, particularly when the GPU also drives a desktop.

## Nonce scheduler

The existing atomic allocator still leases at most 256 nonces per call to CPU
and GPU workers. A V2-only assembler collects as many nonoverlapping leases as
the current execution window needs. It retains the unused tail for the next
batch. Leases may be noncontiguous when CPUs or other GPUs allocate between
them, so every header receives its explicit leased nonce; no contiguous-base
assumption is made. The final lease ends at the 32-bit nonce-space boundary
without wrapping, and final GPU batches may be partial.

An assembler belongs to one device and one immutable job. Expiration, tip
changes, reorganization, cancellation and restart retire its pending tail.
Retired work may leave unused nonces, but cannot assign them twice or submit
stale candidates. Completed GPU hashes still require authoritative CPU hash
equality and target validation. A V2-to-V1 transition rebuilds and verifies the
V1 backend with its original cap; a manual V2 selection above 256 reports an
incompatible V1 capacity instead of expanding the V1 path.

## Qt controls

V1 retains the existing integer spin box and expectations. The V2 control
represents the full unsigned context range exactly, with zero decimal places,
and is bounded by selected devices' resource budgets. Automatic mode selects
each device's safe capacity. The existing layout and visual styling remain.

V2 displays resource maximum, selected contexts, actual allocation, contexts
currently in flight and execution window. Scratchpad and total device-buffer
bytes are distinct. Post-allocation headroom is labeled measured only when a
fresh backend free-memory query succeeded; otherwise it is an estimate.
Headroom describes allocation time, not live telemetry. A reduced automatic
allocation is reported separately from its original estimate.

## Regression evidence and limits

Simulations cover 1, 128, 192, 255, 256, 257, 325, 512, 1024 and 8193 contexts,
with resource arithmetic also checked at 65537 and representational limits.
Coverage includes lease tails, concurrent CPU/multiple GPU allocation, final
nonce-space batches, allocation fallback, grid/occupancy bounds, resident
buffer offsets, cancellation checkpoints, stale completed candidates,
expiration, restart and version-changing reorganizations. Physical NVIDIA or
AMD execution is not implied by simulation or CUDA compilation. The permanent
CPU/GPU arithmetic and header-synchronization tests remain required.

The native and CUDA fake-runtime builds, offscreen Qt tests, focused unit and
functional results for the delivered revision are recorded in the delivery
report and GitHub Actions. Hardware throughput and watchdog qualification
remain local validation gates.

## Changed files

The G1 change is limited to these files, relative to its starting commit:

```text
doc/mercahash-v2-g1.md
doc/mercahash-v2-validation.md
doc/mercahash-v2.md
src/mining/cpu_miner.cpp
src/mining/cpu_miner.h
src/mining/cuda_backend.cu
src/mining/gpu_backend.cpp
src/mining/gpu_backend.h
src/mining/gpu_check.cpp
src/mining/opencl_backend.cpp
src/qt/miningpage.cpp
src/qt/miningpage.h
src/qt/test/miningtests.cpp
src/test/CMakeLists.txt
src/test/cuda_backend_tests.cpp
src/test/gpu_capacity_tests.cpp
src/test/gpu_scheduling_tests.cpp
```

## WSL2 build and validation

Use the separate revision worktree and existing local `project` remote. Do not
reuse or delete any wallet/blockchain data. The cloud delivery uses the
authorized official `origin`; these commands retain the user's WSL layout.

```sh
cd /home/gwestcot/projects/mercatura-mercahash-v2
git status --short --branch
git fetch project feature/mercahash-revision-v2
git merge --ff-only project/feature/mercahash-revision-v2
git rev-parse HEAD

SRC=/home/gwestcot/projects/mercatura-mercahash-v2
BUILD=/home/gwestcot/projects/mercatura-mercahash-v2-g1-build
cmake -S "$SRC" -B "$BUILD" \
  -DCMAKE_C_COMPILER=gcc-12 -DCMAKE_CXX_COMPILER=g++-12 \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_IPC=OFF \
  -DBUILD_GUI=ON -DBUILD_TESTS=ON \
  -DWITH_CUDA_MINING=ON -DWITH_OPENCL_MINING=ON \
  '-DCMAKE_CUDA_ARCHITECTURES=86-real;86-virtual'
cmake --build "$BUILD" -j4

"$BUILD/bin/test_bitcoin" --run_test=crypto_tests/mercahash_v1_vectors
"$BUILD/bin/test_bitcoin" \
  --run_test=gpu_capacity_tests,gpu_scheduling_tests,gpu_miner_tests,cpu_miner_tests,mercahash_v2_tests,mercahash_gpu_tests,mercahash_gpu_sync_tests,cuda_binding_tests,mercahash_activation_tests,mercahash_network_schedule_tests,worker_limits_tests,headers_replay_tests,headers_sync_chainwork_tests
"$BUILD/bin/test_cuda_backend_host"
QT_QPA_PLATFORM=offscreen "$BUILD/bin/test_mining-qt"
python3 "$BUILD/test/functional/test_runner.py" \
  --jobs=4 --timeout-factor=6 --combinedlogslen=0
```

Dependencies and compiler/CUDA compatibility are described in
[the original validation guide](mercahash-v2-validation.md). RTX 3050 is SM 8.6.
These commands only build and run isolated tests. A clean CPU/GPU/Qt build may
take 20–60 minutes; focused CPU tests typically take 1–15 minutes, fake-runtime
and Qt tests seconds to a few minutes, and the full functional suite 30–180
minutes depending on CPU contention and cache preparation. These are planning
ranges, not measured runtimes on the user's machine.

## Physical GPU parity and capacity measurements

First inspect the device list and run parity on both algorithms. Substitute an
existing independent 80-byte-header corpus if available; the built-in
deterministic inputs also exercise full/partial batches.

```sh
"$BUILD/bin/mercatura-gpu-check" --list
"$BUILD/bin/mercatura-gpu-check" cuda:0 --version 1 --contexts 4 --repetitions 3
"$BUILD/bin/mercatura-gpu-check" cuda:0 --version 2 --contexts 0 --repetitions 3
```

The V2 output reports resource estimate, actual resident contexts, execution
window and allocated buffer bytes. Repeat with explicit counts around the
boundary **only when each count fits the reported safe capacity**:

```sh
"$BUILD/bin/mercatura-gpu-check" cuda:0 --version 2 --contexts 256 --repetitions 1 --benchmark-seconds 30
"$BUILD/bin/mercatura-gpu-check" cuda:0 --version 2 --contexts 257 --repetitions 1 --benchmark-seconds 30
"$BUILD/bin/mercatura-gpu-check" cuda:0 --version 2 --contexts 325 --repetitions 1 --benchmark-seconds 30
```

For larger devices, repeat at 512, 1024 and other valid manual counts; AMD uses
its discovered `opencl:N` ID. A 4 GiB RTX 3050 may safely fit **fewer** than 256
contexts after desktop usage and reserves. Even a 6 GiB card may not fit 325.
Do not bypass the safety budget to reach a test count. Simulations provide
larger-capacity coverage when the physical device cannot.

`--benchmark-seconds` is V2-only, accepts 1–60 seconds, and runs a fixed manual
measurement after mandatory vectors and CPU/GPU differential checks. It does
not select counts or tune the miner, connect to a node, or submit blocks.
The timed phase reports completed hashes, elapsed seconds, hashes/second and
deadline drain time. Preflight CPU parity is excluded from timed throughput
and can take several minutes for hundreds of contexts. Save the full output
for each count together with GPU model/VRAM, driver/CUDA version, WSL version,
desktop load, Qt selected/allocated/active contexts and observed stop latency.
Compare actual throughput and cancellation responsiveness in Phase G2 before
choosing an optimal count. G1 automatic mode deliberately starts at capacity.
