# MercaHash V2 cloud validation and handoff

The rebuild continues the accepted `666b3a8` checkpoint on
`feature/mercahash-v2-rebuild`, descended from the original V1 baseline
`724428dbe551fb6bfb8c81272ef74647ebbba839`. Production activation X is **unset**.
Mainnet, public testnets and signet retain V1. All cloud-executable milestones
are complete. Physical release qualification is
**DEFERRED — PHYSICAL HARDWARE REQUIRED (NOT TESTED)**.

## Source and compatibility

The original `/workspace/Mercatura-Core` checkout remains clean at
`631886dd5159614b628ced3fe4fa1e289ce19451`. The separate rebuild worktree is
`/workspace/mercatura-v2-rebuild`; no branch was pushed, merged or reset. G1
`128ea7e091da4f5ec9d2c71cdc0d4257b981b07c` supplied the frozen V2 specification,
vectors and narrowly reviewed device arithmetic, not its coordinator or its
provisional production activation policy. The accepted baseline report and
`/workspace/work/staged-output` artifacts remain unchanged and their manifest
still verifies.

`src/crypto/mercahash.cpp` is byte-identical to baseline, SHA256
`b4d331120ae8d6d0b8e052b65ed3891b468ad3ca28c368e3fd43b561b0fbe225`.
V2 lives separately in `src/crypto/mercahash_v2.cpp`. Permanent literal outputs
cover all seven approved V2 header patterns and V1 patterns/nonzero nonces.
Scratchpad reuse, independent allocations, unaligned access, canaries and release
buffer-size checks pass. Header encoding remains 80 bytes and block identity
remains SHA256d. DGWv3, 150-second timing, emission, wallet authorization and
post-quantum transport rules were not changed.

The [architecture and consensus report](MERCAHASH-V2-REBUILD.md) records exact
genesis identities and targets. Separate regtest configurations implement:

| Flags | Enforced algorithm |
| --- | --- |
| `-mercahashv2height=-1` | Historical V1 throughout |
| `-mercahashv2height=0` | V2 including a distinct, validated genesis |
| `-mercahashv2height=20` | V1 below 20, V2 beginning at 20 |
| `-mercahashv2height=0 -regtestminingstress` | Separate V2 genesis and fixed harder `1f0fffff` target |

Both overrides are rejected on every public network, including explicitly
disabled values. The harder option preserves the hash workload and normal
regtest no-retarget rules; its target averages about 4,096 attempts per block.
Every V2 genesis is actually hashed and validated at load, including during
reindex. Incompatible genesis datadirs fail startup.

Candidate algorithms come from verified ancestry, not the active tip or peer
claims. Unknown-height activated work fails closed; unknown P2P ancestry requests
bounded headers before memory-hard validation. Header reuse requires exact
indexed TREE-valid fields and height. Block validation caches carry a memory-only
algorithm tag. Tests cover both activation directions and cache misuse.

## Native CPU and Qt evidence

The default controller uses authoritative hashing, private scratchpads and
versioned native Solo jobs. Ordinary block turnover retains workers; version
changes cancel and join the old pool before replacement. Stop publishes
cancellation without calling provider code. A joined background helper interrupts
the provider while the coordinator owns submission, joins and teardown. Qt
statistics sample independently of template work and avoid a chain lock for idle
height display. The existing native Mining tab, PQ reward address, worker controls,
wallet behavior and permanent 0% software fees remain in place.

A build with both CUDA and OpenCL disabled compiled the Qt/test targets and node library
and passed 78 mining/activation/resource cases, the original V1 vector case and
the live one-worker V2 Qt row (3 Qt-reported passes). Both GPU backends were then
restored and the entire configured build completed successfully. CPU development
and native mining do not require a GPU toolkit or a production activation height.

`final-qt.log` reports **21 passed, 0 failed, 3 skipped**, 75.419 seconds. Actual
Solo/Qt runs used one and two actual hashing workers, three Start/Stop cycles in each
of the three schedules. Six-worker rows correctly skip under the two-CPU quota;
six-worker scheduling logic has separate test coverage.

| Live Qt schedule | Worst Stop dispatch | Handler | Observed completion | Statistics publication gap |
| --- | ---: | ---: | ---: | ---: |
| V1 only | 2 ms | 1 ms | 101 ms | 526 ms |
| V2 from genesis | 7 ms | 1 ms | 51 ms | 529 ms |
| Height-20 transition | 3 ms | 1 ms | 51 ms | 508 ms |

These are measured event-loop and joined-stop timings, not estimates. V2 easy
runs accepted 46–55 blocks per roughly 2.1-second cycle; statistics continued
updating. Transition tests actually crossed 20. Session teardown maxima were
145 ms for V1, 16 ms for V2 and 16 ms for transition; shutdown timing is separate
from an ordinary asynchronous Stop. The full application Close/reopen harness
passed twice on the same disk wallet/chain for each worker count. Maximum Close
handler time was 17 ms, full node/application exit 107 ms and heartbeat gap 59 ms
(`final-app-close.log`). The final restored-build CTest rerun also passed the
CUDA host-runtime check and both two-run Close/restart variants (3/3); under
concurrent regression load its maxima were 19 ms, 83 ms and 116 ms respectively.

The additional qualification launchers passed live CPU runs for V1, V2 easy,
transition and harder V2. Independent localhost peers synchronized and passed
`verifychain(4,0)`. The harder run completed 203 genuine CPU attempts and accepted
one block; this short, lucky sample is not a target-probability benchmark. It
measured Stop completion 23 ms, Close handler 8 ms and application exit 102 ms.
All these launchers use actual hashing and consensus validation, without a
provider or device test double. Hardware runs must use longer repeated sessions.

Separate three-cycle, five-second standalone CPU measurements use synthetic
public work and do **not** qualify wallet/block integration:

| Algorithm | Workers | Mean H/s | Worst Stop join |
| --- | ---: | ---: | ---: |
| V1 | 1 | 6.977 | 147.611 ms |
| V1 | 2 | 13.075 | 154.270 ms |
| V2 | 1 | 62.935 | 6.839 ms |
| V2 | 2 | 119.332 | 14.735 ms |

All workers terminated each cycle. Stop request time was at most 0.024 ms. V2
post-stop RSS can retain roughly 19/36 MiB in the process allocator, stabilizing
across repetitions; this is not a claim of zero process RSS after cleanup. The
older immutable V1 baseline and supplied G1 Ryzen/RTX results describe different
measurements and are not presented as comparable hardware performance results.

## CUDA, OpenCL and combined coordination

V1 and V2 kernels compile as distinct fixed versions. V2 uses measured resource
budgets, bounded launch windows, explicit cancellation drains and rotating use of
the resident pool. Automatic allocation retries are bounded; Manual requests
are exact. V2 capacity has no inherited 256-context ceiling. CPU and GPU leases
share one nonce cursor; retained lease tails, windows larger than 256, final nonce,
job replacement and version changes are covered. Each GPU candidate must match
an independent authoritative CPU hash and target before submission.

`final-mining-unit.log` reports **65 cases passed**, 14.471 seconds, covering
portable and specialized device arithmetic, GPU budgets, nonce scheduling,
failures, stale results, cancellation and statistics. A clearly labeled CPU
reference backend additionally mined real V2 blocks through native Solo/Core in
GPU-worker and combined configurations. It establishes host integration and
consensus behavior; it is not physical GPU evidence.

The production CUDA host object passed 801 V1 and 389 V2 cancellation/restart
checkpoints, faults, full/partial batches, cleanup and a simulated 8,193-context
resource case (`cuda-fake-runtime.log`). That runtime does not execute device
instructions. OpenCL 1.2 syntax checks passed for the actual V1 and V2 portable
kernels. GCC compiled the production OpenCL host integration.

Real CUDA host/device compilation and the complete Qt/node/test link succeeded
using Clang 19.1.7 and the official CUDA 12.8.93 SDK, SM75 plus PTX. No SDK or
system headers were modified. Clang warns that CUDA 12.8 is newer than its
partially supported 12.5; the warning remains in the build log. This is compiler
evidence, not an NVCC/GCC12, SM86 or device-runtime qualification. GPU discovery
and both physical GPU/combined launchers correctly returned 77 in this container.

Only complete returned batches count as hashes. Separate started/completed/
cancelled counters expose progress when rapid CPU wins repeatedly retire GPU
work. Harder combined qualification additionally requires completed hashes from
both devices. Host tests exercise uninterrupted progress, rapid CPU solutions,
blocked providers, repeated replacements, GPU failure with CPU continuation,
CPU failure, Stop and joined shutdown.

## Regression repairs and remaining qualification

The initial broader checkpoint uncovered three fixture issues: a non-contiguous
branch replay batch, an unfinished peer-test merkle root hidden by manually set
`fChecked`, and dual-valid trial blocks left eligible during the activation reorg.
The corrected peer fixture passes normal full validation. Replays remain
contiguous, and accepted trial siblings are explicitly invalidated. The focused
56-case rerun and five-node functional rerun passed; original failure logs are
retained alongside their repaired runs.

The complete repaired broad selection passed **147 cases with no errors** in
1,669.274 seconds (27 minutes 49 seconds). It includes activation/ancestry,
header replay, DGW, emission and block capacity, full block/reorganization
validation, chainstate/snapshot/startup handling, witness resources, native Solo
and wallet/PQ spending (`broad-consensus-unit-repaired.log`).

The other 11 native functional scripts passed: historical hashing, mining,
chain reorganization, coinbase templates, native Solo/PQ reward spending,
network identity, header replay, DGW, DGW reindex, general reindex and P2P block
propagation (`native-functional-final.log`). The separately repaired V2 script
passed in 60 seconds (`activation-functional-repaired.log`). This is an 11-script
pass plus a repaired-script rerun, not a claim that the initial 12-script run
passed unchanged.

Physical RTX 3050 CUDA/OpenCL parity, VRAM behavior, real GPU/combined Qt
responsiveness and Ryzen six-worker throughput remain **DEFERRED — PHYSICAL
HARDWARE REQUIRED (NOT TESTED)**. Windows and macOS execution were unavailable.
The [WSL qualification procedure](MERCAHASH-V2-HARDWARE-QUALIFICATION.md) supplies
NVCC 12.8/GCC12 build commands, device checks, exact/manual context experiments,
three repeated isolated native runs and an acceptance record. Final release
requires those results. Selecting X and coordinating the separate PQ networking
upgrade remain explicit later decisions.

The bundle handoff includes all new commits from baseline, a source patch,
changed-file map, machine-readable results and preserved validation logs. Verify
the provided SHA256 manifest and bundle before importing into a new worktree.
Do not overwrite an existing G1 branch or its uncommitted linker fixes.

## Reproduction and source map

Build prerequisites and the WSL CUDA configuration are in the hardware guide.
For a Linux CPU-only build with those host dependencies installed:

```sh
cmake -S . -B ../mercatura-v2-cpu-build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_GUI=ON -DBUILD_TESTS=ON \
  -DENABLE_IPC=OFF -DWITH_CUDA_MINING=OFF -DWITH_OPENCL_MINING=OFF
cmake --build ../mercatura-v2-cpu-build -j4
```

The cloud dependency prefix/environment is session-local; do not copy its
absolute compiler paths into the WSL build. From a configured build, reproduce
the principal checks as follows (Qt offscreen is optional on a desktop):

```sh
BUILD=../mercatura-v2-rebuild-build
"$BUILD/bin/test_bitcoin" --run_test=crypto_tests/mercahash_v1_vectors
"$BUILD/bin/test_bitcoin" --run_test=mercahash_v2_tests,mercahash_activation_tests,cpu_miner_tests,worker_limits_tests,mining_statistics_tests,mercahash_gpu_tests,mercahash_gpu_sync_tests,cuda_binding_tests,gpu_capacity_tests,gpu_scheduling_tests,gpu_miner_tests,mining_tests_v2_solo
QT_QPA_PLATFORM=offscreen "$BUILD/bin/test_mining-qt"
QT_QPA_PLATFORM=offscreen ctest --test-dir "$BUILD" \
  -R 'test_cuda_backend_host|test_mining_app-qt' --output-on-failure
python3 "$BUILD/test/functional/test_runner.py" --jobs=4 --timeout-factor=3 \
  mercatura_mercahash_pow mercatura_mining mercatura_chain_reorg \
  mercatura_coinbase_template mercatura_solo_native mercatura_network_identity \
  mercatura_mercahash_v2 mercatura_headers_replay mercatura_dgw \
  feature_mercatura_dgw_reindex mercatura_reindex mercatura_p2p_propagation
"$BUILD/bin/test_bitcoin" --run_test=mercahash_v2_tests,mercahash_activation_tests,mercahash_native_transition_tests,pow_tests,headers_sync_chainwork_tests,headers_replay_tests,peerman_tests,validation_block_tests,validation_block_tests_dgw,validation_chainstate_tests,validation_chainstatemanager_tests,validationinterface_tests,miner_tests,block_capacity_tests,testnet_capacity_tests,mercatura_controller_tests,mercatura_emission_tests,mercatura_fixedpoint_tests,witness_resource_tests,witness_resource_tests_chain,witness_resource_tests_net,mining_tests_wallet,mining_tests_solo,mining_tests_v2_solo
```

The broad inherited unit selection mines many V1 chains and can take tens of
minutes under the cloud quota. Keep its complete run separate from short
performance measurements.

| Area | Principal source changes |
| --- | --- |
| Frozen hashing | `crypto/mercahash_v2.cpp`, version/geometry declarations, permanent tests |
| Activation and fixtures | `consensus/params.h`, chain parameters, V2 genesis validation, harder block fixture |
| Validation and synchronization | `pow.*`, `validation.*`, `headerssync.*`, `net_processing.cpp`, memory-only block cache tag |
| Native work and CPU | `mining/solo.cpp`, `cpu_miner.*`, worker limits and standalone benchmark |
| GPU arithmetic and resources | CUDA/OpenCL backends, portable/specialized kernels, budget helpers and parity checker |
| Native Qt | cached height accessor, versioned resource preview/statistics, live MiningSession tests |
| Real application qualification | `qt/test/miningapptests.cpp`, `test/mining/qt_restart.py`, three qualification launchers and watchdog |
| RPC/offline tools | height-aware native solver, explicit offline version and genesis-tool selection |
| Regression coverage | activation, ancestry/cache, worker epochs, GPU leases/faults, native wallet blocks and functional history |

Implementation commits are `c600d1f` (consensus), `3a5a98e` (CPU/Qt), `dccd52c`
(CUDA/OpenCL/combined), `9525352` (fixture repairs), and `ec82a0e` (isolated
hardware qualification), following baseline checkpoint `666b3a8`. The exported
`commit-history.txt` also includes the final validation-documentation commit;
`changed-files.txt` lists every changed path and its size.
