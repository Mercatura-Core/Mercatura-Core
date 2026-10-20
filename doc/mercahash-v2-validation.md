# MercaHash V2 review and local validation

The revision starts at `724428dbe551fb6bfb8c81272ef74647ebbba839` on
`feature/mercahash-revision-v2`. See [the implementation notes](mercahash-v2.md)
for the frozen algorithm, permanent-vector configuration, ancestry selection,
header presync handling, mining resources and difficulty behavior.

No mainnet, testnet, testnet4 or signet activation is scheduled. The only
activation override is regtest-only. No deployment, reset, wallet migration,
release, merge or production mining is part of this change.

## Local WSL2 source and dependencies

Use the separate revision worktree, preserving the main checkout, original CUDA
worktree, existing GPU builds and every wallet/blockchain datadir. The following
uses the local `project` remote from the original WSL configuration. Branch rename
and fast-forward commands fail safely if that local layout has since changed.
The cloud delivery uses the explicitly authorized official `origin` remote.

```sh
cd /home/gwestcot/projects/mercatura-mercahash-v2
git status --short --branch
git fetch project feature/mercahash-revision-v2
# If this local worktree is still on feature/mercahash-v2:
git branch -m feature/mercahash-revision-v2
git branch --set-upstream-to=project/feature/mercahash-revision-v2
git merge --ff-only project/feature/mercahash-revision-v2
git log -8 --oneline

sudo apt-get update
sudo apt-get install build-essential g++-12 cmake pkg-config python3 \
  libboost-dev libevent-dev libsqlite3-dev
```

GCC 12.1+ and CMake 3.22+ are required by the existing project. Ubuntu 22.04's
standard GCC 11 is insufficient. Dependency installation usually takes 2–10
minutes; fetching the branch generally takes under a minute.

## CPU build first

```sh
SRC=/home/gwestcot/projects/mercatura-mercahash-v2
BUILD=/home/gwestcot/projects/mercatura-mercahash-v2-build
cmake -S "$SRC" -B "$BUILD" \
  -DCMAKE_C_COMPILER=gcc-12 -DCMAKE_CXX_COMPILER=g++-12 \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DENABLE_IPC=OFF -DBUILD_GUI=OFF -DBUILD_TESTS=ON -DBUILD_BENCH=ON \
  -DWITH_CUDA_MINING=OFF -DWITH_OPENCL_MINING=OFF
cmake --build "$BUILD" -j4
```

Allow roughly 15–45 minutes for a clean Ryzen 5 4600G build, and 1–10 minutes
for ordinary incremental rebuilds. These are planning ranges, not measured
results for the user's machine. The commands build without starting a node.

## Focused correctness gates

```sh
"$BUILD/bin/test_bitcoin" \
  --run_test=crypto_tests/mercahash_v1_vectors --log_level=test_suite

ctest --test-dir "$BUILD" --output-on-failure -j4 \
  -R '^(mercahash_v2_tests|mercahash_activation_tests|mercahash_gpu_tests|mercahash_gpu_sync_tests|cuda_binding_tests|pow_tests|cpu_miner_tests|gpu_miner_tests|worker_limits_tests|headers_replay_tests|headers_sync_chainwork_tests|mining_tests_solo|mining_tests_wallet)$'

python3 "$BUILD/test/functional/test_runner.py" --jobs=4 --timeout-factor=6 \
  mercatura_mercahash_v2.py mercatura_mercahash_pow.py \
  mercatura_headers_replay.py mercatura_dgw.py mercatura_reindex.py
```

Allow 5–30 seconds for the original V1 vectors, 5–25 minutes for the focused
unit group (including native Solo and header synchronization), and 5–20 minutes
for the focused functional group including initial cache creation. The new V2
functional test alone typically takes under 2 minutes. Increase timeout factor
on a busy machine instead of changing consensus workloads or test fixtures.
Functional tests create isolated temporary regtest datadirs.

## Authoritative local gates

```sh
ctest --test-dir "$BUILD" --output-on-failure -j4
python3 "$BUILD/test/functional/test_runner.py" --jobs=4 --timeout-factor=6
```

The default functional runner selects the Mercatura-native suite. An inherited
Bitcoin functional `--extended` run is not required for this revision. Allow
approximately 30–180 minutes for all unit suites and 30–180 minutes for the
native functional suite; fixture-heavy V1 verification and PQ work can dominate.
Do not treat the focused cloud checks as a substitute for these local gates.

## Separate GPU and Qt build

Install the existing supported NVIDIA CUDA toolkit (12.0 or newer) in WSL and
verify `nvcc --version`. Do not replace the existing wallet/CUDA build directory.
For Qt/OpenCL compilation, add the development packages:

```sh
sudo apt-get install qt6-base-dev qt6-tools-dev qt6-tools-dev-tools \
  libqrencode-dev ocl-icd-opencl-dev opencl-headers

GPU_BUILD=/home/gwestcot/projects/mercatura-mercahash-v2-gpu-build
cmake -S "$SRC" -B "$GPU_BUILD" \
  -DCMAKE_C_COMPILER=gcc-12 -DCMAKE_CXX_COMPILER=g++-12 \
  -DCMAKE_CUDA_HOST_COMPILER=g++-12 \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_IPC=OFF \
  -DBUILD_GUI=ON -DBUILD_TESTS=ON -DBUILD_BENCH=ON \
  -DWITH_CUDA_MINING=ON -DWITH_OPENCL_MINING=ON \
  '-DCMAKE_CUDA_ARCHITECTURES=86-real;86-virtual'
cmake --build "$GPU_BUILD" -j4
ctest --test-dir "$GPU_BUILD" --output-on-failure -j4 \
  -R '^(test_cuda_backend_host|mercahash_gpu_tests|cuda_binding_tests|mercahash_gpu_sync_tests|gpu_miner_tests)$'

"$GPU_BUILD/bin/mercatura-gpu-check" --list
"$GPU_BUILD/bin/mercatura-gpu-check" cuda:0 "$SRC/test/data/mercahash-gpu-headers.txt" \
  --version 1 --contexts 4 --repetitions 3
"$GPU_BUILD/bin/mercatura-gpu-check" cuda:0 "$SRC/test/data/mercahash-gpu-headers.txt" \
  --version 2 --contexts 4 --repetitions 3
```

The architecture is RTX 3050/Ampere 8.6. If CMake cannot locate the existing
CUDA installation, supply its actual path with `-DCUDAToolkit_ROOT=...` and
`-DCMAKE_CUDA_COMPILER=.../bin/nvcc`. Keep the host compiler compatible with that
toolkit. A clean GPU/Qt build may take 20–60 minutes. Host/emulated correctness
checks generally take 1–10 minutes. Budget 5–30 minutes for V1 device parity and
1–10 minutes for V2; actual device times must be measured. Four contexts reproduce
the previously qualified setup and are not a new performance recommendation.

On an actual supported AMD device, replace `cuda:0` with its `opencl:N` ID and
run both versions. OpenCL compilation without an AMD GPU does not qualify the
device. Windows, macOS, Qt runtime, NVIDIA hardware and AMD hardware each need
their own validation; cloud CPU emulation cannot establish those results.

## Review considerations and next phase

Keep the same regtest activation argument across restarts and reindex. Changing
the simulated schedule requires a fresh isolated datadir. If a manual worker or
GPU-context count fits V2 but cannot fit V1 after a reorg, mining stops with a
resource error instead of silently overcommitting or changing the manual choice.
Automatic budgets are recalculated. The 32 MiB host overhead is conservative and
can be revisited only after measurements.

DGW uses the existing target/chainwork units even though hash cost changes. A
throughput increase can temporarily shorten spacing while its 24-block window
adapts. No reset or difficulty exception was introduced. Activation coordination
and any separately justified difficulty intervention are outside this revision.

After all correctness gates pass locally, measure CPU and GPU scaling, energy per
hash, fill/binding/mixing stage timing, memory use, synchronization cost, historical
header verification and end-to-end sync. Use those measurements to decide worker
counts and GPU contexts. This implementation makes no optimized-count or hashrate
claim, and authorizes no public-network activation.


## Cloud verification evidence

The cloud machine runs Debian 13 with GCC 14.2; this is not the user's Ubuntu
22.04 WSL2 or a physical GPU environment. CMake and dependency packages were
installed under an isolated workspace prefix; no existing wallet/GPU builds or
user datadirs were reused.

- The official remote branch was fetched and verified at the exact baseline,
  including `mercahash_gpu_specialized.h`, before any source edits.
- All seven V1 inputs were checked against the original unmodified baseline
  implementation, with exact output equality. The original four permanent V1
  outputs are unchanged; three additional nonce outputs are now fixed as well.
- Independently structured staged device arithmetic reproduced every permanent
  V2 vector. Clean GCC and Clang 19 builds agreed on all fourteen V1/V2 outputs.
- CUDA 13.0 compiled the actual backend and both specialized kernel instances
  for SM 8.6. The actual host object passed the fake-runtime suite: 801 V1 and
  104 V2 cancellation/restart checkpoints, full/partial batches, injected
  allocation/launch/synchronization/transfer errors and cleanup. The fake runtime
  executes no GPU instructions.
- Both versions passed warp binding emulation, including initial/final blocks,
  context offsets, fill and binding launch boundaries, and cooperative stores.
- Both OpenCL versions passed Clang's OpenCL 1.2 syntax checks. The OpenCL host
  backend and parity utility compiled with the generated embedded source.
- The isolated activation functional test passed mining at H-1/H/H+1, V1-only
  proof rejection at H, unconnected/malformed P2P handling, exact known-header
  replay, side-branch height selection, reorg across activation, VerifyDB,
  restart and reindex, with stable chainwork and UTXO counts/amounts.

The final CMake build passed for mercaturad, mercatura-cli, test_bitcoin,
bitcoin-util, mining-bench and mercatura-gpu-check with OpenCL enabled. The
final 58-case focused run passed: MercaHash V1/V2 vectors, allocation/reuse and
buffer validation, activation/presync/reorg/native Solo jobs, CPU/GPU controller
and worker-limit tests, plus independently staged device parity. It completed
in approximately 21.5 seconds after the final rebuild. Public-network override
rejection and disabled/invalid regtest option handling passed.

The original V1-vector unit case passed. The 59-case affected regression run
passed, including all pow, staged GPU arithmetic, warp barriers/binding, header
sync/replay, native Solo and mining-wallet suites. The four existing functional
checks passed: mercatura_mercahash_pow (4 s), mercatura_headers_replay (12 s),
mercatura_reindex (267 s), and mercatura_dgw (541 s), with 541 s wall time after
cache preparation. These timings were measured under concurrent compilation,
not as performance benchmarks. The new activation functional test passed
separately, and passed again on the final rebuilt daemon in approximately 12 s. Synthetic one-worker V1/V2 controller smoke
checks also passed with exactly 128/16 MiB reported scratchpads and no workers
left active after stop. OpenCL discovery found no AMD GPU (skip status 77).

The standalone scalar/staged V2 vector runner passed AddressSanitizer and
UndefinedBehaviorSanitizer for all seven permanent inputs. LeakSanitizer could
not run under the sandbox's process tracing; the successful sanitizer run used
`ASAN_OPTIONS=detect_leaks=0`. CUDA host tests also passed when compiled with
the explicit SM 8.6 minimum used by the recommended RTX 3050 build; the fake
runtime's capability now follows that configured minimum.

The unfiltered unit suite and complete default native functional suite were
not run in cloud. Full local unit/native-functional gates, physical NVIDIA/AMD qualification, Qt runtime,
Windows and macOS qualification remain required. No hardware parity pass is
claimed from CPU emulation or the fake runtime.

The first CUDA 12.6 cloud compile failed on the image's newer GCC/glibc headers;
CUDA 13.0 was used for the successful compile. This does not replace testing the
user's supported CUDA/host-compiler combination. A first parallel functional run
hit the inherited cache builder's 30-second RPC timeout during concurrent
compilation; the focused rerun uses `--timeout-factor=6`. Initial test-harness
assertions about log scheduling and argument-manager selection were corrected;
consensus rules and frozen workloads were not weakened to make tests pass.

## Modified and added files

- `doc/mercahash-v2-validation.md` — added
- `doc/mercahash-v2.md` — added
- `src/bench/mercahash.cpp` — modified
- `src/bitcoin-util.cpp` — modified
- `src/chainparams.cpp` — modified
- `src/chainparamsbase.cpp` — modified
- `src/consensus/params.h` — modified
- `src/crypto/mercahash.cpp` — modified
- `src/crypto/mercahash.h` — modified
- `src/headerssync.cpp` — modified
- `src/headerssync.h` — modified
- `src/kernel/chainparams.cpp` — modified
- `src/kernel/chainparams.h` — modified
- `src/mining/CMakeLists.txt` — modified
- `src/mining/cpu_miner.cpp` — modified
- `src/mining/cpu_miner.h` — modified
- `src/mining/cuda_backend.cu` — modified
- `src/mining/gpu_backend.cpp` — modified
- `src/mining/gpu_backend.h` — modified
- `src/mining/gpu_check.cpp` — modified
- `src/mining/mercahash_gpu.h` — modified
- `src/mining/mercahash_gpu_specialized.h` — modified
- `src/mining/mining_bench.cpp` — modified
- `src/mining/opencl_backend.cpp` — modified
- `src/mining/solo.cpp` — modified
- `src/mining/worker_limits.cpp` — modified
- `src/net_processing.cpp` — modified
- `src/pow.cpp` — modified
- `src/pow.h` — modified
- `src/primitives/block.h` — modified
- `src/qt/miningpage.cpp` — modified
- `src/rpc/mining.cpp` — modified
- `src/test/CMakeLists.txt` — modified
- `src/test/cpu_miner_tests.cpp` — modified
- `src/test/cuda_backend_tests.cpp` — modified
- `src/test/cuda_binding_tests.cpp` — modified
- `src/test/gpu_miner_tests.cpp` — modified
- `src/test/headers_replay_tests.cpp` — modified
- `src/test/mercahash_activation_tests.cpp` — added
- `src/test/mercahash_gpu_tests.cpp` — modified
- `src/test/mercahash_v2_tests.cpp` — added
- `src/test/pow_tests.cpp` — modified
- `src/test/util/mining.cpp` — modified
- `src/test/util/setup_common.cpp` — modified
- `src/validation.cpp` — modified
- `src/validation.h` — modified
- `test/functional/mercatura_mercahash_v2.py` — added
- `test/functional/test_runner.py` — modified
