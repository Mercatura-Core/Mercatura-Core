# Mercatura Core v0.1.2 public-testnet release qualification

This is the review and build procedure for [Issue #1](https://github.com/Mercatura-Core/Mercatura-Core/issues/1),
starting from `fca0511eb8d0e04edd17a5368a714a02d20c6dd0` on `v0.1`.
It does not authorize publication, a tag, a deployment, or changes to `main`.
The previous `v0.1.1-testnet` prerelease stays available. Previously built
CPU-only Windows/Linux binaries are superseded, not relabeled.

## Reproduce the cloud candidate

`.github/workflows/testnet-release.yml` runs on the release-automation branch
and supports manual dispatch once available in the repository's default branch.
Each runner checks out the workflow SHA, not a moving release ref. Workflow
changes require a new SHA and a new run on **all three platforms**. The assembly
job requires all platforms to succeed and rejects mismatched source manifests.
It writes `SHA256SUMS` from the four actual files, not from Actions' wrapper ZIPs:

- `Mercatura-Core-v0.1.2-testnet-win64.zip`
- `Mercatura-Core-v0.1.2-testnet-win64-setup.exe`
- `Mercatura-Core-v0.1.2-testnet-linux-x86_64.tar.gz`
- `Mercatura-Core-v0.1.2-testnet-macos-arm64.zip`

Download the `Mercatura-Core-v0.1.2-testnet-review-bundle` Actions artifact and
extract it once. Its macOS asset is already the actual distributable ZIP; do not
publish the outer Actions download as the macOS release ZIP. Artifacts expire
in 30 days; download and preserve approved artifacts and reports before then.
No workflow has release-write permissions or a publication/tag step.

| Platform | Toolchain / configuration | Distribution dependencies |
| --- | --- | --- |
| Windows x64 | Windows 2022 runner, VS2022 v143 **14.40**, CUDA **12.8.1**, vcpkg pinned baseline; CUDA + OpenCL **ON**; dynamic Qt with QR/wallet | Included Qt/plugins and redistributable runtime DLLs; system NVIDIA driver and/or AMD full-profile OpenCL 1.2 driver |
| Linux x86_64 | Ubuntu **24.04**, GCC **13**, CUDA **12.8**, system OpenCL SDK; CUDA + OpenCL **ON**; Qt6/QR/wallet | Bundled application libraries/plugins, Ubuntu 24.04-compatible glibc and graphics stack; system GPU driver/ICD |
| macOS ARM64 | macOS 15 runner, native Apple Clang and Homebrew Qt6/QR; CUDA/OpenCL explicitly **OFF** | Bundled CPU-only `.app`; ad hoc signed; not Developer ID signed or notarized |

The JSON evidence records runner image, actual CUDA version, compiler/cache
configuration and source SHA. Retain CMake caches, CTest logs and runner logs for
exact dependency/compiler versions. CUDA binaries contain `sm_75` and `sm_86`
code and compute-7.5 forward-compatible PTX: **minimum compute capability 7.5**,
including RTX 3050 6 GB's 8.6 target. Older NVIDIA architectures are not claimed.
No unsupported-compiler override, MinGW CUDA substitution or stub is permitted.
CUDA uses its static runtime and includes the toolkit redistribution notices;
no driver is shipped. AMD OpenCL is dynamically loaded from the system.

`contrib/release/qualify.py` checks explicit features, binary architecture,
version, both production backend definitions in Qt/daemon symbols or link maps,
and CUDA cubin/PTX images. A GPU checker listing exit **77** is a hardware skip,
not successful parity. A missing compiled backend is always a failure. Windows
packaging uses `windeployqt`, checks imported DLLs, creates a portable ZIP and
an NSIS installer from identical staged files, then installs, byte-compares and
uninstalls in a disposable runner directory. The installer creates a testnet
shortcut and its uninstaller removes only listed package files, never wallet
or blockchain directories. Linux bundles dependencies with relative RPATHs,
checks dependencies and versions, extracts the archive and checks startup.
macOS rebuilds the app from the same SHA and rejects residual Homebrew links.
After inserting release resources, it reseals and verifies the final bundle with
an ad hoc signature; this is not Developer ID signing or notarization.

The public-testnet build lock applies to the **GUI**, as in the baseline.
Daemon/CLI preserve existing network flags: specify `-testnet` for public testnet.
No public network node is launched by release qualification. Existing isolated
regtest functional tests use fresh disposable data directories only. The release
workflow runs Core and native mining tests, frozen MercaHash vectors, PQ/wallet
functional tests and Qt mining lifecycle tests. The inherited generic Bitcoin
Qt suite is outside this native Mercatura release gate; it is not disabled in
the build. The existing general CI matrix remains a separate required gate.

Linux intentionally does not bundle glibc, the system graphics stack, an
OpenCL ICD or GPU drivers. It targets Ubuntu 24.04-compatible desktops, not every
Linux distribution. Bundled shared Qt may be replaced by compatible shared
libraries; dependency versions and license notices accompany the package.
Sources for distribution packages are available from Ubuntu's corresponding
source archives; Qt sources are also available at https://code.qt.io/.
Static CUDA runtime notices must remain with redistributed binaries.
Code signing, notarization, physical GPU testing, historical testnet replay and
production deployment are separate qualifications; never infer them from a green
compile or simulated device tests.

## GPU Mining and CPU + GPU Mining: Beta hardware plan

**GPU Beta, live hardware validation pending.** CPU remains independently usable
when no supported GPU/runtime is available. Discovery errors and unsupported
cards must remain visible; mining starts only through an explicit user action.
No physical card is advertised as tested by this cloud workflow.

Use a disposable wallet and isolated test environment first. Do not point these
automated checks at an existing personal wallet or running node. For community
field testing after release approval, retain a backup, shut down gracefully,
verify SHA256SUMS, and record the exact release source SHA.

1. Record OS, exact GPU model/VRAM, driver/OpenCL versions, CPU, RAM and package
   SHA. Intended owner devices are NVIDIA **RTX 3050 6 GB** and AMD **Vega 4 GB**
   (exact AMD model unconfirmed). These are test targets, not qualified hardware.
2. Run the included `mercatura-gpu-check --list`, then
   `mercatura-gpu-check cuda:0` or `mercatura-gpu-check opencl:0`. All four frozen
   vectors must match authoritative MercaHash V1 before mining. Optionally pass
   `test/data/mercahash-gpu-headers.txt` as the second argument for extra parity.
   Exit 77 means no supported device was found; record the error, not a pass.
3. In Qt, test **GPU Mining (Beta)** separately on CUDA and AMD OpenCL. Confirm
   detection, selected device/context count, live rate and responsive Start/Stop.
   Repeat Stop/Start, close/reopen and wallet lock/unload; no automatic restart.
4. Test **CPU + GPU Mining (Beta)**, manual CPU limits and per-device versus
   combined counters. Verify reduced CPU capacity accounts for GPU verifiers.
   Record wall-clock H/s over at least 10 and 60 seconds and session averages.
5. On an isolated chain, verify actual accepted blocks through an independent
   peer, wallet-owned PQ coinbase outputs, immature rewards and mature balances.
   Exercise tip replacement/reorg, stale candidates, peer loss/reconnect and
   restart/recovery. No finite public-testnet block discovery time is promised.
6. Start with conservative context counts. Test memory pressure and allocation
   failures safely, then sustained load and clean cancellation. Record watchdog,
   driver-reset, responsiveness and error messages; do not bypass self-tests.
7. Measure **wall power** and CPU/GPU temperatures with external/vendor tools.
   Record ambient conditions, duration, fan policy, H/s, watts and H/s per watt
   (or watts per H/s). Cloud elapsed time cannot establish power efficiency or
   thermal safety. Stop if the system becomes unstable or unsafe.
8. Community NVIDIA/AMD owners: report model, driver, OS, detection, vector
   parity, performance, stability, thermals and power observations on the project
   issue tracker. Never include private keys, seeds, wallet files or RPC secrets.

## Approval and publication boundary

Proposed notes are in [release-notes-0.1.2-testnet.md](release-notes/release-notes-0.1.2-testnet.md),
with the complete Issue #1 text, Beta invitation, zero-fee policy and height-8,000
hard-fork warning. They must not be shortened into a CPU-only or hardware-tested
claim. Upgrade the public-testnet node/miner fleet before height 8,000; older
blocks retain 1 MiB, and height 8,000 increases capacity to 4,194,304 serialized
bytes. This procedure makes no further consensus change.

After **explicit owner approval only**, a maintainer can perform these actions
using the reviewed source SHA and downloaded complete review bundle. These are
instructions, not automated steps:

```sh
# Verify all required CI/release jobs, source manifest and package checksums first.
sha256sum --check SHA256SUMS
git tag -a v0.1.2-testnet <approved-source-sha> -m 'Mercatura Core v0.1.2 public testnet'
git push project refs/tags/v0.1.2-testnet
gh release create v0.1.2-testnet --repo Mercatura-Core/Mercatura-Core \
  --verify-tag --prerelease --title 'Mercatura Core v0.1.2 — Public Testnet' \
  --notes-file PROPOSED-PRERELEASE-NOTES.md \
  Mercatura-Core-v0.1.2-testnet-win64.zip \
  Mercatura-Core-v0.1.2-testnet-win64-setup.exe \
  Mercatura-Core-v0.1.2-testnet-linux-x86_64.tar.gz \
  Mercatura-Core-v0.1.2-testnet-macos-arm64.zip SHA256SUMS
```

Do not move a published tag or replace artifacts silently. If an approved
release proves defective, stop recommending the affected package, report the
specific fault, preserve v0.1.1 and the source/artifact records, and prepare a
new reviewed version. Deployment rollback is a separate decision: pre-upgrade
public-testnet software may reject valid blocks from height 8,000, so ordinary
binary rollback across that boundary is not safe to assume. Never reset a live
chain or restore a wallet without an independently approved recovery procedure.
