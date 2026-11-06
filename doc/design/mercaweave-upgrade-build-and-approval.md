# MercaWeave candidate build, review and release preparation

These instructions build development applications and run isolated tests. They
do not install or deploy them, select public activation, finish the v0.1.3 soak,
or authorize a release. Use a separate checkout and disposable test datadirs.

## Reproducible Linux procedure

Use CMake 3.22 or newer, a supported C++20 compiler, Python 3 and the dependencies
in [the Unix build guide](../build-unix.md). For a Debian/Ubuntu native Qt build,
the dependency set includes a C++ toolchain, Ninja, Boost, libevent, SQLite,
libqrencode and Qt 6 base/tools development packages. Record exact package,
compiler, CMake and source identities with the test results.

```bash
git checkout integration/mercaweave-v0.1-upgrade
git status --short
git rev-parse HEAD
cmake -S . -B build/mercaweave-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_GUI=ON -DBUILD_GUI_TESTS=ON \
  -DBUILD_TESTS=ON -DENABLE_IPC=OFF -DWITH_ZMQ=OFF -DWITH_USDT=OFF \
  -DWITH_QRENCODE=ON -DWITH_OPENCL_MINING=OFF -DWITH_CUDA_MINING=OFF
cmake --build build/mercaweave-release --parallel 4
QT_QPA_PLATFORM=offscreen ctest --test-dir build/mercaweave-release \
  --output-on-failure -j4
python3 build/mercaweave-release/test/functional/test_runner.py --jobs=4
```

Set `CMAKE_PREFIX_PATH` only if using a separately staged dependency SDK. The local
worker uses the preserved Qt/Boost/SQLite SDK and CMake executable from the research
workspace; its exact paths and commands belong to the qualification receipt.
Its two-CPU cgroup quota and 8 GiB memory justify `-j2` and functional `--jobs=2`.
That local adjustment is not a reduction of the suite or a production node option.

For Debug, use another build directory, `CMAKE_BUILD_TYPE=Debug`, and record any
reduced debug-symbol level used for disk limits. For ASan/UBSan, use a further
directory with `-DSANITIZERS=address,undefined` and an instrumented Debug build.
Run instrumented applications with `ASAN_OPTIONS=halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. A worker that cannot run
LeakSanitizer must explicitly record `detect_leaks=0` and the failed capability
probe; this is not a passing leak check. Prefer one instrumented test worker when
several native nodes would otherwise compete for memory.

The standalone locked-protocol comparison can be reproduced separately:

```bash
cmake -S src/test/mercaweave -B build/mercaweave-research \
  -DCMAKE_BUILD_TYPE=Release -DMERCAWEAVE_FULL_INTEGRATION=ON \
  -DMERCAWEAVE_QT_TESTS=ON
cmake --build build/mercaweave-research --parallel 4
QT_QPA_PLATFORM=offscreen ctest --test-dir build/mercaweave-research \
  --output-on-failure
cargo run --manifest-path test/lint/test_runner/Cargo.toml
```

Never point these candidate binaries at a deployed v0.1.3 datadir. Isolated tests
use the existing regtest genesis and native PoW, plus explicit immediate/delayed
`-mercaweaveheight` schedules. The native functional runner remains the authoritative
functional gate; the inherited Bitcoin-wide functional suite is not substituted.

## Cross-platform qualification and installer restriction

The candidate adds a native Linux/Windows MSVC/macOS ARM64 workflow that builds
Qt and runs all native unit/Qt/functional tests. It uploads qualification logs
only. Hardware CUDA/OpenCL results and other architectures remain separately
identified in the qualification report.

General CI previously used `GOAL=deploy` for Windows/macOS cross builds, which
creates installer/package outputs. The integration branch is explicitly excluded
from that matrix to honor this work order's installer restriction. Existing
headless native macOS/Windows tests and lint remain enabled; the dedicated workflow
supplies native Linux and GUI checks. This exclusion is branch-specific and does
not change protected production refs. No skipped cross packaging job is counted
as a passing build or qualification result.

## Review and proposed release checklist

- Verify the final commit, clean worktree, commit ancestry, retained historical
  evidence and exact tested executable/source identities.
- Review activation selection at H-1/H/H+1, unknown-witness controls, context/cache
  binding, queued jobs, work accounting, assumevalid behavior and reorg cleanup.
- Review seed cleansing, authenticated records, atomic mandatory wallet flag,
  persistence, nonce/key rotation, mixed fees and strict partial PSBT operations.
- Review Receive availability/defaults and native Send, copy, QR and labels.
- Recheck native mining payout gates and byte preservation of MercaHash/PQ code.
- Review all initial failures, repairs, final unit/functional results, sanitizer
  scope, skips, platform results and unresolved limitations on the exact candidate.
- Obtain independent consensus/security and wallet review; keep that attestation
  separate from automated test results.
- Independently qualify physical GPU combinations and live-network behavior.
  Do not infer either from host tests or a private regtest chain.
- Confirm the founder has completed the desired v0.1.3 soak and separately
  approved the activation proposal and operator communication plan.
- Obtain explicit approval before any protected-branch merge, release/tag,
  installer creation, public activation or deployment.

## Founder decision checklist

The approval decision should identify the source commit, intended platforms,
remaining accepted limitations, review attestations, soak observations, future
activation proposal, operator readiness and incident owners. A green workflow
does not grant any of those approvals automatically.

No fund migration is required. Existing v2 wallets and rewards remain supported.
Before grouped opt-in, preserve backups and explain mandatory wallet-format
support and issued-range recovery. After consensus activation, downgrading the
node's validator to an unknown-v3 client is unsafe even for a v2-only wallet.
Use the [activation and incident-response design](mercaweave-upgrade-activation.md)
for monitoring and consensus-aware rollback planning.
