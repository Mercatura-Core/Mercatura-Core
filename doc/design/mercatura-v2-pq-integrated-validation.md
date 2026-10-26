# MercaHash V2 + PQ P2P integrated validation

Validation session: October 25, 2026, America/Los_Angeles; retained UTC logs are dated October 26. This is an engineering validation checkpoint, not a release approval or independent security audit.

## Repository and source boundaries

- Branch: `validation/pq-v2-integrated-hardening`.
- Validated source HEAD: `c2e6f1a9d1136ceed19ba8083ed52b7a0dff5b11`. The documentation/evidence commit follows this source commit and is identifiable in Git history for this report.
- GitHub integration HEAD was checked with `git ls-remote` before fetching: `bfc8f3bf5d4559be591655c867c33100c4c3fa48`.
- Ordered integration parents: mining `cc5db1df34d362bdfecd11003404bc307c2219b0`, PQ `ea389cbdd8129a9b2a1ead0ca97590b291b771b3`.
- The mounted checkout initially had a clean `work` branch at `631886dd5159614b628ced3fe4fa1e289ce19451`. That branch was preserved. A new validation branch was created from the fetched integration commit. No original branch was reset or overwritten.
- The previous session's unfinished sanitizer probe, Python endpoint and CMake changes were absent. They were not imported. The new adapter test targets were implemented and validated here.
- `project` was configured to `https://github.com/Mercatura-Core/Mercatura-Core.git`. Pushes are restricted to this new branch through `project`; no push to `origin`, force-push or public deployment is part of this work.

The integration checkpoint, Stage A validation, hardening audit, frozen transport specification and V2 cloud/hardware qualification documents were reviewed. Their historical tests are not counted below. Consensus, chain parameters, validation, wallet, mining implementation, MercaHash implementations and vendored ML-KEM/ML-DSA sources remain byte-for-byte unchanged against the integration base. Production-header changes only complete include-guard comments. PQ remains explicit, isolated-regtest-only; public-network rejection and required-mode no-fallback policy remain in place.

Frozen SHA-256 values were rechecked:

- Transport specification: `5960a8a1f032280f3a4bacb9aad07331eb1cae975e566e657e58d8d84ddc2e42`.
- Candidate handshake fixture: `6222abf5163ee34a987fe383a8e10b07e50abf1a3ab93f2a0e031a50dd9415f5`.

## Build configuration and results

Managed Debian 13 x86_64, Linux 6.18.55, Intel Xeon Platinum 8370C, two-core cgroup quota, 8 GiB memory limit, no swap. GCC/G++ 14.2.0, CMake 3.31.6, Ninja 1.12.1, Boost 1.83, libevent 2.1.12, SQLite headers 3.46.1, Qt 6.8.2. Clang 19.1.7 was used for focused crypto sanitizers/fuzzing and OpenCL syntax checks.

The Release build uses the repository's normal hardening and `-O2` flags. Wallet, tests, daemon, CLI, Qt and OpenCL mining are enabled. IPC, ZMQ, DBus, QR encoding and CUDA mining are disabled. Dependencies were extracted under `/workspace/work/deps`; `CMAKE_PREFIX_PATH` and `PKG_CONFIG_SYSROOT_DIR` select that prefix. No system SDK or vendored crypto source was patched.

```sh
cmake -S . -B /workspace/work/build-integrated -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/workspace/work/deps/usr \
  -DENABLE_IPC=OFF -DBUILD_GUI=ON -DWITH_DBUS=OFF -DWITH_QRENCODE=OFF \
  -DWITH_ZMQ=OFF -DWITH_OPENCL_MINING=ON
cmake --build /workspace/work/build-integrated \
  --target all pq_adapter_sanitizer_tests pq_adapter_tsan_tests -j2
```

The local CMake/Ninja binaries were added to PATH and their library directory to LD_LIBRARY_PATH; `PKG_CONFIG_SYSROOT_DIR=/workspace/work/deps` was needed for relocated pkg-config files. One compiler job was used while CPU-heavy regression tests ran, then two jobs resumed.

| Build/check | Observed result |
| --- | --- |
| `bitcoind` / `mercaturad` | PASS; version command executes |
| `bitcoin-cli` / `mercatura-cli` | PASS |
| `test_bitcoin`, `pq_stagea_tests`, `pq_resource_probe` | PASS |
| Complete configured `all` target | PASS; includes wallet/utilities, Qt, Qt unit/mining binaries and native mining tools |
| `bitcoin-qt` / `mercatura-qt` | PASS |
| Native CPU mining components | PASS; compiled into combined node and exercised below |
| OpenCL host backend and `mercatura-gpu-check` | PASS compilation |
| Actual V1 and V2 portable kernels, Clang OpenCL 1.2 syntax checks | PASS both |
| GPU discovery | Exit 77: “NVIDIA CUDA backend was not compiled”; “No AMD OpenCL GPU detected” |
| CUDA compilation, GPU/device parity, GPU/combined mining | NOT EXECUTED: missing CUDA toolkit and supported GPU hardware |

No GPU hardware qualification follows from these software checks.

## Integrated unit regression

Five selected groups all passed. Totals: **276 distinct selected cases, 811,761 assertions**. The executable contains 1,023 cases; the full suite was not run. Cases outside each selection were reported as skipped, not passed. Exact suite filters and durations are retained in the public evidence JSON.

| Group | Passed cases | Passed assertions | Duration | Result |
| --- | ---: | ---: | ---: | --- |
| consensus | 95 | 38,736 | 182.42 s | PASS |
| pq-network | 57 | 565,634 | 15.79 s | PASS |
| native-mining | 69 | 68,361 | 192.40 s | PASS |
| wallet | 13 | 3,657 | 19.55 s | PASS |
| adjacent-network | 42 | 135,373 | 17.86 s | PASS |

Coverage includes permanent V1/V2 vectors, activation/fork/wrong-version selection, PoW, block capacity, witness validation through disk/reindex/chain connection, native CPU/Solo/V2 mining, reward maturity/fees/PQ spending, PQ primitives and handshake/packet vectors, identity parsing/cleanup, asynchronous transport/admission/deadlines, BIP324, keys/scripts, wallet encryption persistence and adjacent networking. GPU unit cases use documented CPU reference devices/test doubles and do not qualify physical GPUs.

The separate Release and instrumented focused PQ runners each passed 10 cases / 13,899 assertions. These overlap the main selection and are not added to the 276-case total. The independent primitive checker passed 287 checks, including pinned-source checks and independent primitive/HKDF/golden-vector comparisons. Independent packet reference verification passed 1,348 packets and three rekey boundaries per direction.

## Real-node connectivity, relay and identity lifecycle

All three positive functional runs passed on the combined daemon, with new retained loopback-only regtest directories. Each run used two PQ-required nodes and a classical rejection fixture. Durations were 27.167 s (V2), 107.263 s (transition), and 301.039 s (V1), with concurrent compilation during these runs.

| Schedule | V2 height | Final height | Matching post-restart tip | Result |
| --- | ---: | ---: | --- | --- |
| v2 | 0 | 103 | `6f59fa37e23c7817828899070cc50bf9712f53ff8ac8a868bdebae34a7fd248d` | PASS |
| transition | 20 | 103 | `55c0c9d79edbc9c49a89dd80c7fb7345f13b07524f3e8991cd65f12d466d12ff` | PASS |
| v1 | -1 | 103 | `b108714ca1c7b62de4289fb9a32f5844a91a670542c23b30b08ed1850be5db97` | PASS |

Both endpoints reported `pq-v1`, ML-KEM-768, ML-DSA-65, encryption and presented-key authentication, with matching session IDs. Per-message byte accounting confirmed 7,750/7,738 handshake bytes, totaling 15,488. Unpinned sessions correctly reported no expected-identity verification; pinned outbound sessions reported it.

Every run relayed a native PQ transaction and confirmation block, reconciled the receiver's confirmed 1.00 MCA balance, changed both ephemeral identities on reconnect, loaded persistent identities, rejected a wrong pin without fallback, and recovered after an explicit trusted pin update. New rotation coverage creates a separately named identity, preserves the original identity file, rejects the old pin, and requires an explicit pin update. Restarted nodes relayed another block, agreed on headers/hash/height and passed `verifychain(4,0)`. All nodes shut down successfully.

V2 transaction example: `abe04b50609d50ff3a5c31021e360f76db2136816d16ad07e0ee4b1042b47bad`.
Its confirmation block: `d4b3a8c9186a225bb446eacec4c7cb610785e2e2210c899394e5dfa4744c0e10`.
All three runs' public transaction, block, session and identity observations are in the evidence JSON. The 101-block funding fixture exists for coinbase maturity, not a production cadence requirement.

The separate negative functional run passed 24 wire-mutation/disconnect scenarios, two absolute deadlines and established-peer preservation at full inbound capacity. Observed preface/record deadlines were 5.054/30.052 seconds despite later fragments. No unauthenticated version delivery or classical retry was observed. Public-network enablement rejection was exercised in the positive runs.

## Native Qt mining with PQ networking

The existing Qt miner lifecycle CTest passed in 108.63 seconds. New Linux CTest cases register actual Qt/native CPU mining over required PQ for V2-from-genesis and height-20 transition. An additional two-worker V2 run passed. These are real native miners and disk wallets, not producer test doubles. Each run has a minimum ten-second initial mining window and requires at least three accepted blocks; it does not require hundreds of blocks within seconds.

| Schedule | CPU workers | Initial-phase accepted blocks | Final verified height | Stop completion ms | Close completion ms | Result |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| transition | 1 | 19 | 39 | 30 | 114 | PASS |
| v2 | 1 | 96 | 112 | 19 | 118 | PASS |
| v2 | 2 | 51 | 70 | 57 | 108 | PASS |

Both live mining endpoints were observed using authenticated PQ transport. Stop, restart and normal window Close completed with joined sessions; reported heartbeat gaps were 62–212 ms. The producer was then reopened as a non-mining daemon from its retained chain. Both nodes established matching fresh PQ sessions, synchronized exact block/header tips and passed full `verifychain(4,0)`. Final hashes, public session IDs and timing statistics are retained in evidence. These are offscreen Linux results on this host; the production target remains 150-second blocks.

Reproduce the permanent mining regressions with:

```sh
ctest --test-dir /workspace/work/build-integrated \
  -R 'test_mining_app-qt-pq-' --output-on-failure
python3 contrib/devtools/qualify-mercahash.py \
  --build-dir /workspace/work/build-integrated --mode cpu --schedule v2 \
  --workers 2 --seconds 10 --deadline 180 --offscreen --pq
```

## P5/P6/P7 hardening evidence

**P5:** Fresh ephemeral identities, persistent loading, owner/mode checks, restrictive umask, corruption, wrong-network/key binding, symlink/hardlink/FIFO/type rejection, no-clobber concurrent creation, pin mismatch and recovery passed in the existing tests on the integrated build. New real-node tests cover explicit identity rotation and trusted pin replacement on all three PoW schedules. Secret move/failure/destruction cleansing and temporary-file cleanup passed. Physical erasure of persistent files, encrypted storage policy, Windows ACL/publication and hostile same-user filesystem compromise are not qualified.

**P6:** Integrated adapter tests cover silent inbound quota charging, per-source/anonymous/global bounds, bounded workers/queue, cancellation, queue/execution deadlines, late-result rejection, shutdown and lease cleanup. Both injected admission-allocation failures were caught with zero pending charges. The dedicated requested-C++-heap probe observed zero retained bytes after idle, partial-M1 and four-concurrent-session teardown. Peaks were 1,832, 3,998 and 135,659 bytes respectively. These measurements exclude allocator/libc overhead, native stack and whole-process RSS. Source inspection places KEM/keygen/signature work in `Context::Run` outside networking/chain locks; this is not exhaustive runtime lock or sustained-load qualification.

**P7:** New deterministic tests reject M3/M4/M5 substitution between sessions with different nonces but valid identical identities/KEM inputs, including zero responder decapsulations for rejected M3. Every byte position in the 85-byte network discriminator is tested for rejection before randomness. Frozen packet/rekey, signature/KEM error, transcript/confirmation and authenticated-packet failure tests pass.

New excluded `pq_adapter_sanitizer_tests` and `pq_adapter_tsan_tests` directly instrument the production adapter, protocol, identity format and POSIX storage sources. Each passed 12 enabled cases; the existing benchmark remains skipped. ASan/UBSan with leak checking passed 100,915 assertions; TSan passed 100,909 assertions. Assertion counts vary with polling/fragmentation loops. Other Core dependencies remain uninstrumented: these are adapter-boundary results, not whole-node sanitizer certification. The focused Clang build separately instruments crypto/protocol primitives and genuine libFuzzer targets.

Six genuine guided campaigns ran for 120 seconds per target with ASan, UBSan, leak checking, retained corpora and fixed seeds:

| Target | Executions | Final corpus files | Crash artifacts |
| --- | ---: | ---: | ---: |
| `pq_handshake` | 5,035 | 71 | 0 |
| `pq_packet` | 1,590,756 | 69 | 0 |
| `pq_packet_stream` | 10,765 | 43 | 0 |
| `pq_identity` | 88,595 | 85 | 0 |
| `pq_headers` | 19,126,418 | 10 | 0 |
| `pq_command` | 30,994,713 | 273 | 0 |

Total: **51,816,282 executions**, approximately 730 seconds including campaign overhead, zero sanitizer findings/crash artifacts. These are bounded regression campaigns, not extended fuzzing or independent AKE qualification. Extended campaigns and full-node/live-mining sanitizer coverage remain open.

## Findings, corrections and commits

No production consensus, crypto or transport logic defect was confirmed in these runs. Two newly exercised mining fixtures initially failed with “Independent peer did not synchronize the mined chain”: Qt closed cleanly while a fast regtest producer was ahead of its peer. The fixture assumed all blocks had already been served before Close. The corrected PQ runner reopens the stopped producer without a miner, qualifies PQ reconnect/recovery, requires exact tip agreement and verifies both chains. The same two CTest cases then passed, followed by the two-worker run. Original failure logs and directories are retained.

Lint fixes preserve pinned upstream source conventions, restore executable script modes and complete project include-guard comments. Test/security changes are separate from lint commits.

| Source commit | Change |
| --- | --- |
| `344c2cc73099598d986dacd6549462c80b1e9df0` | lint: preserve pinned ML-KEM source conventions |
| `d86a8d798b1ab121be786c9e82c07f355d4e63b0` | lint: mark PQ developer and functional scripts executable |
| `7fdacea0b30432044ceb42146e698c5d2a9baa9e` | lint: complete PQ header guard comments |
| `339136629a39ee6abbe8fa37cf8243069c97ad3b` | test: reject PQ cross-session and network-domain substitutions |
| `71419e5d09a1e7ff7618faf6e1dd64672b1ad3f9` | test: qualify PQ relay across V2 schedules and explicit identity rotation |
| `0dd3de768de820409d5ca10a3b9802028b865dba` | test: qualify PQ adapter and storage with ASan UBSan and TSan |
| `c2e6f1a9d1136ceed19ba8083ed52b7a0dff5b11` | test: qualify native V2 mining over PQ with restart recovery |

This report and its public JSON evidence are committed separately after these validated changes.

## CI and environment ledger

Local include, include-guard, file-permission and test-registration lint checks passed. `git diff --check` passed. A narrow documented vendor exclusion covers `src/crypto/mlkem_native/`; the Mercatura crypto wrappers remain linted, and upstream math was not changed.

Remaining local lint findings: `net -> net_pq -> net` circular dependency and locale-dependent helper/test functions. Source inspection also confirms direct `std::filesystem` use in `net_pq_identity.cpp`; Python import-order findings were not fully addressed. The cycle comes from `net.cpp` constructing/stopping the concrete PQ implementation while `net_pq.h` derives from `Transport` declared in `net.h`. A reviewed shared transport/interface boundary is follow-up architecture work; this checkpoint introduces no new network coupling. Full Rust/Python lint tooling and remote CI completion are not claimed green.

| Observation | Classification and disposition |
| --- | --- |
| Initial Git proxy connection failure; Python `[Errno 1] Operation not permitted` | Command network sandbox permission; Git verification/fetch succeeded with tool-authorized network permission. No destination-policy bypass was used. |
| Initial CLI compile: `fatal error: event2/buffer.h: No such file or directory` | Relocated pkg-config advertised `/usr/include`; corrected `PKG_CONFIG_SYSROOT_DIR`. |
| Initial CLI link: unresolved libevent `bufferevent_*`/listener symbols | Local development package selected static archives without relocated shared runtime libraries; extracted matching runtime packages and refreshed discovery. No source workaround. |
| Early unit launcher: `[Errno 13] Permission denied` on `test_bitcoin` | Launcher observed the output during linking; rerun after successful build completion. No unit case executed in the failed launch. |
| `LeakSanitizer has encountered a fatal error`; `LeakSanitizer does not work under ptrace (strace, gdb, etc)` | Initial focused run's leak phase failed in the ordinary sandbox. Tool-authorized execution outside ptrace passed with leak checking enabled; the initial attempt is not counted as a complete sanitizer pass. |
| Two initial Qt/PQ fixtures could not finish synchronization after producer Close | Test-fixture lifecycle assumption, reproduced on both schedules and corrected as described above. |
| GPU discovery exit 77 | Supported hardware/toolchain unavailable; device qualification remains unexecuted. |

No explicit Daybreak/security-access rejection occurred during this session. No current test remains blocked by such a restriction. Earlier reports' access messages are historical and are not reclassified as new results. Existing evidence/data were preserved; temporary qualification processes used isolated loopback fixtures and were shut down.

## Remaining qualification and recommended next milestone

This branch is not a mainnet release candidate. Remaining gates include:

- Independent AKE, key-confirmation and two-contribution combiner analysis, plus independent endpoint interoperability and security approval.
- Extended guided fuzzing, sustained churn/admission fairness, allocator/stack/RSS profiling, full-node/live-network/mining ASan/TSan, and systematic stale-generation/every-worker-stage disconnect injection, and broader shutdown/failure/reorg/partition/three-node IBD/endurance qualification. The unfinished Python endpoint remains absent; no completed malicious-authenticated-peer harness is claimed.
- The full 1,023-case unit suite, full functional/Qt suites and outstanding CI findings. Selected passing tests do not close these gates.
- Windows/macOS/ARM64/WSL builds and runtime qualification. Windows persistent identity storage still deliberately fails closed pending owner ACL and atomic publication support. Platform stack, filesystem and shutdown behavior need local testing.
- Actual CUDA compilation, physical CUDA/OpenCL V1/V2 parity, VRAM/resource failure behavior, GPU/combined Qt mining and visible desktop/owner Ryzen qualification, including six-worker operation.
- Production identity storage, operator rotation/backup/erasure policy, cleartext identity privacy and unpinned-intermediary limits.
- Decentralized fresh-node bootstrap, independent community peers, IPv4/IPv6/NAT/Tor qualification and separately authorized public-testnet deployment stages.

Recommended next milestone: review this integrated Linux baseline and outstanding lint boundary, then run longer full-node instrumented/soak qualification and independent AKE/interoperability review alongside Windows/macOS and physical GPU qualification. Mainnet genesis/configuration and public-testnet activation X remain separate approved checkpoints. No genesis, public chain parameter or deployment change was made here.

Public measurements and exact suite filters are in [mercatura-v2-pq-integrated-evidence.json](mercatura-v2-pq-integrated-evidence.json). Distributed evidence excludes wallets, identity seeds, RPC cookies and operational secret files; raw test data remain in the workspace.
