# MercaHash V2 hardware qualification

Status: physical RTX 3050, Ryzen six-worker, mixed-device Qt, Windows and macOS
execution are **DEFERRED — PHYSICAL HARDWARE REQUIRED (NOT TESTED)** in the cloud. This procedure supplies
those missing results. Source compilation, host arithmetic models and fake
CUDA runtime tests are separate evidence. Production activation X remains unset.

## Import and build under WSL2 Ubuntu 22.04

Keep the existing G1 worktree, uncommitted linker fixes, wallets, chains and CUDA
artifacts intact. The incremental bundle requires the established baseline
`724428dbe551fb6bfb8c81272ef74647ebbba839`, already present in the original repo.
Check its SHA256SUMS and verify the bundle from that repository before importing.
If a destination or branch exists, inspect it and choose a new destination/name.

```sh
cd ~/projects/Mercatura-Core
git cat-file -e 724428dbe551fb6bfb8c81272ef74647ebbba839^{commit}
git bundle verify /path/to/mercahash-v2-rebuild.bundle
git fetch /path/to/mercahash-v2-rebuild.bundle \
  feature/mercahash-v2-rebuild:review/mercahash-v2-rebuild
git worktree add ~/projects/mercatura-v2-rebuild review/mercahash-v2-rebuild
```

Use the existing Windows NVIDIA driver and WSL CUDA 12.8 toolkit. Required host
packages include GCC/G++ 12, CMake >=3.22, Ninja, Boost >=1.74 development headers,
libevent, SQLite, Qt6 >=6.2 base/tools development packages, Python3 and OpenCL
headers/loader. Enable Qt tests explicitly. The RTX 3050 is SM 86; include PTX for
forward-compatible discovery. Build out of tree with `-j4`:

```sh
cd ~/projects/mercatura-v2-rebuild
cmake -S . -B ~/projects/mercatura-v2-rebuild-build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=/usr/bin/gcc-12 -DCMAKE_CXX_COMPILER=/usr/bin/g++-12 \
  -DCMAKE_CUDA_COMPILER=/usr/local/cuda-12.8/bin/nvcc \
  -DCMAKE_CUDA_HOST_COMPILER=/usr/bin/g++-12 \
  -DCUDAToolkit_ROOT=/usr/local/cuda-12.8 -DCMAKE_CUDA_ARCHITECTURES=86 \
  -DBUILD_GUI=ON -DBUILD_TESTS=ON -DENABLE_IPC=OFF \
  -DWITH_CUDA_MINING=ON -DWITH_OPENCL_MINING=ON
cmake --build ~/projects/mercatura-v2-rebuild-build -j4
```

The cloud build used GCC 14.2 and Clang 19.1.7 with the official CUDA 12.8.93
compiler/runtime headers and real SM75 device compilation. Clang warns that CUDA
12.8 is newer than its partially supported 12.5; this is recorded in the logs.
No system or CUDA SDK headers were patched. This build does not establish the
WSL NVCC/GCC12 or physical SM86 result.

## Permanent checks before live mining

```sh
BUILD=~/projects/mercatura-v2-rebuild-build
ctest --test-dir "$BUILD" -R 'test_cuda_backend_host|test_mining' --output-on-failure
"$BUILD/bin/test_bitcoin" --run_test=mercahash_v2_tests,mercahash_activation_tests,mercahash_native_transition_tests,mercahash_gpu_tests,cuda_binding_tests,gpu_capacity_tests,gpu_scheduling_tests,cpu_miner_tests,gpu_miner_tests,mining_tests_v2_solo
python3 "$BUILD/test/functional/test_runner.py" --jobs=4 --timeout-factor=3 mercatura_mercahash_v2 mercatura_mercahash_pow mercatura_solo_native
```

List the real device and run independent CPU/device comparisons. `77` means no
supported GPU was detected, which is a deferred check. Device parity is a real
hardware result only when these commands execute on that device:

```sh
timeout -k 10s 30s "$BUILD/bin/mercatura-gpu-check" --list
for version in 1 2; do
  timeout -k 10s 1800s "$BUILD/bin/mercatura-gpu-check" cuda:0 \
    test/data/mercahash-gpu-headers.txt --version "$version" --repetitions 3
done
timeout -k 10s 1800s "$BUILD/bin/mercatura-gpu-check" cuda:0 \
  --version 2 --contexts 286 --repetitions 3 --benchmark-seconds 60
```

Manual counts must fit the current safe resource budget; the tool does not tune
clocks, bypass reserves or silently reduce a manual request. Repeat at Automatic
and several safe counts above and below 256. The historical 540–546 H/s at 286
contexts is a supplied G1 reference, not a result of this rebuild. Measure actual
throughput and responsiveness; resident capacity alone is not a tuning target.

## Bounded native Qt CPU, GPU and combined launchers

These launchers open the actual Qt application, create a fresh disk wallet and
regtest chain, press the actual MiningPage controls, measure a real Stop, restart
mining, and close the normal application window. They also start one fresh
localhost peer, connect only to that peer, observe localhost cookie-authenticated
RPC, and require independent synchronization plus `verifychain(4,0)`. No hashing
or provider test double is used. Neither node connects to external peers.

Every invocation creates its own new directory under `work/hardware-qualification`
(or `--output-parent`). All logs, JSONL statistics and test data are retained for
review. The watchdog stops only its own process groups on a stalled Qt heartbeat
or the total deadline; it preserves the experimental data and attempts to capture
thread wait channels/stacks and, when installed and permitted, a bounded GDB
backtrace before terminating the test. Default minimum mining
time is 180 seconds and total deadline 600 seconds. `--seconds` is a minimum;
accepted blocks and the mode-specific progress criteria must also be met.

```sh
contrib/devtools/qualify-cpu.sh --build-dir "$BUILD" --workers 1
contrib/devtools/qualify-cpu.sh --build-dir "$BUILD" --workers 2
contrib/devtools/qualify-cpu.sh --build-dir "$BUILD" --workers 6
contrib/devtools/qualify-gpu.sh --build-dir "$BUILD" --device cuda:0
contrib/devtools/qualify-combined.sh --build-dir "$BUILD" --device cuda:0 --workers 1
contrib/devtools/qualify-combined.sh --build-dir "$BUILD" --device cuda:0 --workers 6 --harder
```

Repeat each hardware configuration at least three times. Each invocation gets a
new directory, including repetitions; for example:

```sh
for run in 1 2 3; do
  contrib/devtools/qualify-combined.sh --build-dir "$BUILD" \
    --device cuda:0 --workers 6 --harder --seconds 180 --deadline 600
done
```

The default schedule is V2 from its validated, distinct genesis. Use `--schedule
v1` for the V1 comparison and `--schedule transition` for height 20; the latter
must actually reach V2 before completing its first mining phase. `--harder`
requires V2 from genesis and selects the distinct `nBits=1f0fffff` chain. Nothing
sets X or changes a public network. `--contexts N` requests an exact manual GPU
count; omit it for Automatic. `--offscreen` exercises real Qt event handling
without a desktop when appropriate. A visible WSLg run remains part of the
physical acceptance checklist.

Easy combined mode requires CPU hashing and real GPU batch starts, safe
cancellation, accepted blocks and responsiveness. It does not require a GPU block
win or a completed GPU batch when CPU discoveries repeatedly retire the work.
Harder combined mode additionally requires completed CPU **and GPU** hashes.
Cancelled incomplete batches receive no completed-hash credit. GPU-only mode
requires completed GPU hashes and valid accepted blocks. Core candidate rehashing
and consensus validation still use CPU in GPU-only mode; CPU mining workers are
disabled.

The JSON summary names the exact schedule/mode, actual progress, timing and peer
verification result. JSONL distinguishes startup, first mining phase, Stop and
restarted mining. The local datadirs contain test-only regtest wallets; collect
logs and summaries for reporting without publishing wallet files or RPC cookies.

For a separate interactive wallet, create a new directory and launch explicitly:

```sh
TEST_DIR=$(mktemp -d /tmp/mercatura-v2-interactive.XXXXXX)
"$BUILD/bin/mercatura-qt" -regtest -mercahashv2height=0 -datadir="$TEST_DIR" \
  -connect=0 -listen=0 -listenonion=0 -discover=0 -dnsseed=0 -fixedseeds=0 -natpmp=0
```

Create a test wallet and select CPU, GPU or combined mode on Mining. Press Start;
mining never starts automatically. Add `-regtestminingstress` only with the V2
from-genesis schedule and a separate fresh directory. Bound this interactive
session externally when collecting unattended evidence.

## Acceptance record

Record CPU model, WSL CPU/memory limits, GPU/VRAM, Windows/WSL/kernel versions,
NVIDIA driver, CUDA/host compiler, exact commit, build flags, selected and allocated
contexts, execution window, free VRAM/headroom, and hashrates. Keep the accepted
baseline report unchanged; add a separate hardware results file.

- V1 and V2 permanent vectors pass on CPU and physical GPU, including full,
  partial, repeated and nonzero-nonce batches.
- One, two and six actual CPU workers run; six-worker logic simulations are
  labeled separately from this measurement.
- V1-only, V2-from-genesis and height-20 transition preserve valid history;
  wrong-version work is rejected, and reorg/restart/sync tests pass.
- Easy V2 CPU-only, GPU-only and combined sessions produce genuine blocks that
  the independent peer accepts. No invalid candidate is submitted.
- Harder combined work yields completed hashes from both devices. Batch starts,
  completions and cancellations explain zero easy-chain GPU rates where relevant.
- Displayed counters and rates continue updating. Normal Stop dispatch, handler
  and worker completion stay below one second; investigate every outlier.
- Close while mining terminates workers and the application promptly; same-chain
  wallet restart works. No mining continues after the joined shutdown barrier.
- Repeated Start/Stop, wallet unload, tip replacement, allocation pressure,
  device faults and buffer reuse leave no orphan threads or live device buffers.
- Record integrated-wallet rates separately from standalone parity/benchmark
  rates. Compare against the preserved V1 checkpoint and supplied G1 reference
  without presenting different environments as equivalent performance evidence.

Final release qualification remains open until the physical evidence above is
collected and reviewed. A public-testnet upgrade still requires explicit choice
of X and separate review/coordination of the post-quantum networking release.
