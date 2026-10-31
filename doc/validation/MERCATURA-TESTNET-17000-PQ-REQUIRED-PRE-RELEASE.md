# Mercatura testnet 17,000 / compulsory-PQ pre-release qualification

Local implementation: **PASS**. Overall release qualification: **PARTIAL** until
the new source has completed platform CI and the founder-controlled release gates.
This report authorizes no merge, official version, tag, binary publication or live operation.

## Source identity and publication boundary

- Original branch: `security/bitcoin-core-2026-10-backports`.
- Original exact HEAD: `187816a7335a3880f40f98bf73570a76d6572f18`.
- Candidate branch: `release/testnet-v2-17000-pq-required`; publish only to `project`.
- Consensus commit: `17d61cc09a474ccbde45d8c2b882346a2dbe4e9b`.
- Networking commit and exact locally tested code HEAD: `e37770f040708681d9ca02780cb288830b3a2bc4`.
- The final documentation-only publication commit follows this tested code. A
  commit cannot embed its own content-addressed SHA; the external final handoff
  and publication receipt record the exact published HEAD, remote ref readback,
  and production/test/workflow blob parity with `e37770f040708681d9ca02780cb288830b3a2bc4`. Do not substitute a
  branch name for an exact source identity when qualifying release packages.
- Prior official `v0.1`: `631886dd5159614b628ced3fe4fa1e289ce19451`.
  Neither `v0.1`, `main`, nor the original security branch is modified.

All official local receipts below record clean `e37770f040708681d9ca02780cb288830b3a2bc4`. Preliminary precommit
results are retained but are not presented as additional independent qualification.
All timestamps are UTC. The evidence bundle contains command arrays, options,
compiler/build configuration, binary hashes, result summaries and reproduction
helpers. Full raw local logs remain in the workspace; sensitive datadirs and
identity/cookie material are excluded from the exported bundle.

## Exact production changes

The code difference has exactly 14 directly related files. In production:
`CTestNetParams` adds only `nMercaHashV2Height = 17'000`; `pq::RequiredMode`
unconditionally enforces public-testnet PQ. CMake descriptions, CLI/RPC diagnostics,
package metadata, regression fixtures and smoke expectations reflect that policy.
There is no new protocol or duplicate PoW dispatcher. The deprecated legacy
`MERCATURA_REQUIRE_PQ_TESTNET` option remains parseable but cannot disable policy.
`TestnetEnforcedBuild()` and the retained RPC field describe effective binary
policy, not a live coordinated-network upgrade. `testnet_activation` is
`required-from-startup`, with no height meaning. Qt peer authentication and expected
identity verification remain separate; accurate per-peer pending states remain.

Consensus tests update two genesis-fixture calls to supply known candidate height
zero. This preserves the production fail-closed unknown-height guard. Two obsolete
all-public-networks-unset assertions now exclude TESTNET; actual testnet activation
is covered by the new permanent boundary case. No assertion is weakened to permit
invalid proofs or transport fallback.

## Consensus and mining evidence

The permanent `pow_tests/public_testnet_mercahash_v2_17000` uses actual TESTNET
parameters and production `GetPoWVersion`. Exact parameter 17,000 and genesis
`b3fc707c9a89dc45d1cf6a837284b22ad2b6fd41b99af601079db9dcbeff6920` pass.
Height 0, 16,998 and 16,999 use V1. Heights 17,000, 17,001, 1,000,000 and INT_MAX
use V2. DGW target continuity at H-1/H/H+1 passes against identical parameters
with only the hash schedule removed. Capacity activation remains 8,000 and target
spacing 150 seconds. No difficulty reset, genesis rewrite, signaling vote,
checkpoint, assumed-valid change or public command-line override is introduced.

`mercahash_activation_tests`, its transition suite and
`mercatura_mercahash_v2.py` passed: wrong-version-exclusive proofs, valid V2,
below-boundary rejection, ancestry/context/cache guards, historical header heights,
regtest height-20 transition, fork/reorg behavior and safe live worker replacement.
The regtest override and V2-genesis fixtures remain unchanged. Mainnet, signet,
testnet4 and ordinary regtest defaults retain their prior activation settings.
The named new boundary and policy cases have explicit entered/left execution proof.
No 17,000-block artificial chain was mined.

Source inspection confirms the unchanged solo-mining path derives candidate height
from a validated parent under `cs_main` and uses `GetPoWVersion`. Worker epochs,
version-aware replacement and stale-result checks remain unchanged. CPU worker,
resource budget, native solo, Qt Start/Stop/Close, V2-transition and ML-DSA reward
tests passed. V1 retains 128 MiB and V2 16 MiB scratchpads; software mining fees
remain zero. Offline `bitcoin-util grind` already requires explicit version on
transition-configured networks; it cannot infer ancestry from an isolated header.
That offline-tool guard is unchanged and does not add a node consensus override.

GPU algorithms, memory budgets, CUDA/OpenCL dispatch and combined scheduling were
inspected for preservation. The headless OpenCL backend compiled and its host
regression passed. **No physical NVIDIA or AMD execution/performance qualification,
CUDA compiler qualification, or GPU combined runtime claim is made.** Three Qt
six-worker rows intentionally skipped because this host's real CPU/memory budget
is insufficient; all other reported Qt rows passed. Linux offscreen Qt smoke is
not installer or founder-wallet migration qualification.

## Default PQ policy and controlled connections

Public-testnet nodes omit `-pqp2p` in permanent required/discovery fixtures. Those
ordinary nodes establish encrypted authenticated `pq-v1` sessions. Explicit
`required` works, `off` fails startup, malformed options fail, both values of the
legacy profile input cannot weaken policy, and mainnet remains compulsory.
Regtest opt-in and signet/testnet4 behavior pass their existing policy assertions.
The ordinary GUI CPU-only build was explicitly configured with legacy flag OFF;
headless OpenCL and the public-GUI profile use ON. Actual runtime default-required
and off-rejection checks pass in the independent configurations, regardless of GUI.

The selected PQ suites and native functional variants passed classical inbound
and outbound rejection, BIP324 rejection, failed-authentication/no-downgrade,
pin failure and verified-pin separation, application-data gating, restarted
sessions, resource/admission limits, disconnect/recovery, block and transaction
relay, native PQ mining rewards, controlled discovery/bootstrap, reorg and reindex.
Network-disabled startup still reports required=true. Authenticated unpinned peers
are not marked expected-identity verified. These are owned loopback tests, not
live public cutover observations or independent operator reachability evidence.
ML-KEM-768, ML-DSA-65, SHA3-384 and the five-message handshake are unchanged.

## Historical V1 and database evidence

The original fixture is available again locally. The prior security report's
original-snapshot gap remains true for that old checkpoint; this is fresh evidence
at `e37770f040708681d9ca02780cb288830b3a2bc4`. Founder attestation: blocks and chainstate copied after normal Qt
shutdown, reported producer version 0.1.2; exact producer build is unknown.
Original archive SHA256:
`b536540e7ef66c39bd271a23741e0b23b2d4d5933c9af1b74f0ce89901ec6b78`. All 14 reference files
and the original archive were hashed against the authenticated manifest before
and after; originals are unchanged and never opened by Core.

A writable disposable COPY directly loaded existing chainstate without mandatory
reindex and restarted cleanly. The expected height 12,332, tip
`ce169dff26447d62a22d2ed987aa30706a62348abb283c6127062836b8f3c47d`, chainwork
`00000000000000000000000000000000000000000000000000000000194e765e`, UTXO serialization hash
`ab3643c2a1bb2b42c7afe48d3d5d1117d6c7fe61735b90a12e7a13ee1e8eb5bf` and genesis matched. A separate new node started
at genesis with default compulsory PQ and connected only to the owned prefix donor,
synced the first 64 real historical V1 blocks from a separately replayed prefix
fixture, and matched that prefix tip/work/UTXO state. Peers were
encrypted/authenticated `pq-v1`, correctly unverified when unpinned. Recent-block
`verifychain(4,6)` passed for archive and fresh node; prefix chainstate reindex,
full prefix reindex and offline restart preserved
state and required policy. Both nodes stopped cleanly. No public peer was contacted.

The duplicate full `verifychain(4,0)` attempt was deliberately stopped cleanly
after inspecting authenticated prior receipts showing roughly 55-minute cost.
Full historical verify/reindex is not relabeled as freshly completed here.
Prior real-snapshot full verification and chainstate reindex evidence is reused only with
its original source identity; fresh native `mercatura_reindex.py`, storage
recovery and `feature_security_storage_compat.py` cover current-source disposable
reindex and storage behavior. The new historical check is full-snapshot direct load/restart plus fresh
PQ synchronization and both reindex modes on a 64-block real-history prefix.
Full 12,332-block fresh replay/reindex is not claimed at the new source. Optional indexes, founder real-wallet migration, old-reader
wide-offset rollback and native Windows original-database execution remain
unqualified by this new fixture check. No chain reset or mandatory reindex is needed.

## Measured test totals and build configurations

- **35/35 registered selected CTest tests passed in the default build; zero failed
  or CTest-skipped.** One additional headless OpenCL host CTest passed (fake ICD;
  no GPU execution): **36/36 CTest invocations across the two configurations.**
  These contain **261 reported Boost case invocations and 1,836,473
  passed assertions**. Qt counts are recorded separately; they are not inferred
  from Boost output or merged into assertion totals.
- Qt reports 21 general-wallet rows passed, 23 mining rows passed with three
  resource skips, and four peer-security rows passed. Four separate app-mining
  integration wrappers passed (CPU options, PQ V2 and transition). Internal Qt
  initialization/cleanup rows are included in Qt's own totals.
- Forced select socket-security binary passed separately, and both new named
  regression leaves executed successfully. These repetitions are not added to
  the 35-suite unique total.
- **56/56 Mercatura-native default functional variants passed**, zero failed or
  skipped, exact inventory match, jobs=4, supported timeout factor=3 on this
  two-core host. One additional inherited-reference storage regression passed:
  **57/57 total functional invocations**. The full inherited Bitcoin suite was
  not run. CSV `ALL` is an aggregate row, not an additional test.
- Three offline bootstrap inventory tests and four lab-cleanup tests passed.
- Include, locale and include-guard lints passed; `git diff --check` is required
  again at the documentation and final publication boundaries. Full CI lint is
  not claimed from these selected local lint scripts.
- Fresh out-of-source Release CPU-only Qt/wallet/tests build passed on Linux
  x86_64, GCC 14.2, Qt 6.8.2. Normal daemon, CLI and Qt version/startup checks pass.
- Independent headless OpenCL build and host test passed; default required-PQ
  and off-rejection are verified against its actual daemon.
- Public-testnet GUI build profile and isolated default-PQ Qt startup smoke passed
  without packaging for publication. OFF-profile tested binaries were hashed and
  preserved before the deliberate ON/public-GUI reconfiguration. All binary
  manifests identify `e37770f040708681d9ca02780cb288830b3a2bc4`; no official version changed.

Selected registered tests: `pq_policy_tests`, `pq_p2p_tests`, `pq_assurance_tests`, `pq_identity_tests`, `pq_transport_tests`, `mercahash_v2_tests`, `mercahash_activation_tests`, `mercahash_activation_tests_transition`, `pow_tests`, `block_capacity_tests`, `testnet_capacity_tests`, `mercatura_emission_tests`, `mercatura_controller_tests`, `mercatura_fixedpoint_tests`, `cpu_miner_tests`, `worker_limits_tests`, `mining_statistics_tests`, `mining_tests_wallet`, `mining_tests_solo`, `mining_tests_v2`, `witness_resource_tests`, `sigopcount_tests`, `crypto_tests`, `test_peersecurity-qt`, `test_mining-qt`, `test_mining_app-qt-1`, `test_mining_app-qt-2`, `test_mining_app-qt-pq-v2`, `test_mining_app-qt-pq-transition`, `net_tests`, `sock_tests`, `flatfile_tests`, `blockmanager_tests`, `blockstorage_durability_tests`, `test_bitcoin-qt`.

| Exact clean-source command receipt | Outcome | Seconds |
|---|---|---:|
| `build-committed` | PASS | 13.77 |
| `focused-committed` | PASS | 310.58 |
| `security-select-committed` | PASS | 0.16 |
| `leaf-boundary-committed` | PASS | 0.11 |
| `leaf-policy-committed` | PASS | 0.11 |
| `qt-default-public-smoke` | PASS | 0.87 |
| `native-functional-committed` | PASS | 1690.74 |
| `storage-functional-committed` | PASS | 141.33 |
| `historical-real-default-pq-prefix-qualified` | PASS | 63.42 |
| `build-headless-opencl` | PASS | 834.33 |
| `test-headless-opencl-host` | PASS | 0.03 |
| `configure-public-gui-profile` | PASS | 0.87 |
| `build-public-gui-profile` | PASS | 193.24 |
| `qt-public-profile-smoke` | PASS | 0.82 |
| `version-public-gui` | PASS | 0.11 |
| `version-headless-opencl` | PASS | 0.01 |
| `lint-includes` | PASS | 2.21 |
| `lint-locale` | PASS | 1.40 |
| `lint-include-guards` | PASS | 0.59 |
| `daemon-help-committed` | PASS | 0.02 |
| `bootstrap-inventory-path-corrected` | PASS | 0.21 |
| `pq-lab-cleanup-path-corrected` | PASS | 0.12 |

## Security and scope preservation audit

All three backports remain ancestors: urgent-data receive backpressure `77a4de7`,
paused-send memory exhaustion `2f25a22`, block/undo overflow `0a66162`; portability
repairs `4f2fbde` and `187816a7` are retained. The preservation audit verifies
365 protected files byte-for-byte against
the original source, including socket/storage paths, PoW validation, transaction
consensus/ownership, crypto, wallet and CPU/GPU algorithms. The cryptographic and
admission-worker prefix of `net_pq.cpp` is unchanged; its edits are policy only.
The entire chain-parameter file otherwise matches the baseline after removing
the sole new TESTNET activation assignment.

Mainnet/testnet genesis, mainnet consensus and required PQ, network identity,
DGWv3, target interpretation/chainwork, adaptive emission/subsidy, fees, capacity
schedule, frozen V1/V2 arithmetic/vectors, witness-v2, ML-DSA authorization, wallet
ownership/derivation and GPU kernels remain unchanged. No dependency upgrade,
Bitcoin refactor or new security protocol is included. Mainnet V2 remains
**DEFERRED** until several days of actual post-activation testnet observation.

Resolved setup/test interruptions are preserved, not hidden: initial genesis
fixture omitted known height zero; OpenCL SDK loader-link setup was incomplete;
the disposable snapshot initially inherited read-only reference permissions;
two ancillary commands initially used paths relative to the wrong working
directory and were corrected before execution. No production failing assertion
was disabled. The stopped duplicate historical full verification is not a PASS. A second
full-history sync trial was intentionally interrupted; its fresh node remained
in expensive header processing beyond the helper's 90-second shutdown allowance
and was killed only after that timeout. That trial is not qualified as PASS;
the shutdown limitation and preserved cleanup receipt remain evidence. The
bounded real-prefix trial records its own independent clean shutdown results.

## CI and platform qualification checkpoint

Baseline standard CI run [41051865838](https://github.com/Mercatura-Core/Mercatura-Core/actions/runs/41051865838)
completed all eight jobs successfully at `187816a7335a3880f40f98bf73570a76d6572f18`. This does **not** qualify
the two new changes. New-source standard CI is **PENDING publication/initial
observation** here. GUI/PQ CI is **NOT RUN at this documentation checkpoint**.
The final external publication receipt records the one initial new-source run
snapshot and at most one exact-branch manual dispatch attempt, with URLs/head SHA,
status/conclusion or the precise blocked-dispatch reason and founder command.
No CI polling, watching, retries or pending-as-PASS claims are permitted.

Windows MSVC, macOS ARM64, Linux ARM64, supported cross-compilation and native
platform runtimes require new-source CI qualification; old green runs are only
baseline evidence. Physical GPU, independent external AKE assessment and actual
multi-operator/live bootstrap operation are not established by local Linux tests.
The founder's experimental AKE waiver and formal independent-operator deferral
remain documented residual risks, not completed external certifications.

## Activation window and founder-controlled gates

Founder-reported height **13,450**, recorded **2026-10-31T05:10:19.382813+00:00**, leaves
**3,550 blocks** to 17,000 relative to that report. No block hash was supplied.
Explorer responses were inconsistent/cached; no independently verified fresh tip
is claimed, and no forecast is inferred. The founder instructed proceeding and
reported a 4–5 hour manual release process. Recheck the actual tip/window for the
later rollout; never silently move the approved activation height.

Ready for founder integration review after final publication audit: **YES**.
Ready to enter manual release preparation: **NO while exact-source platform CI or
required review gates remain incomplete**. No live deployment or release is
authorized by completion of this development task.

Founder prerequisites are source/diff review, completed new-source CI, approved
integration into `v0.1`, exact integrated-source release qualification, compatible
platform packages, operator/miner inventory and acknowledgments, reachable PQ
bootstrap/pins, explorer/backbone readiness, wallet/data backups, coordinated
rollback and fresh public PQ sync checks. PQ cutover occurs immediately at startup;
V2 consensus starts independently at 17,000. After that height a V1-only binary
cannot follow the chain; rollback must retain V2 support or use an explicitly
coordinated recovery plan. Observe several days of real V2 before mainnet work.

The final handoff separately confirms normal `project` branch publication,
unchanged `v0.1`/`main`/security refs, all owned processes exited and evidence hash
readback. No merge, force-push, official version, tag/release, binary publication,
public restart, DNS/seed edit or announcement is performed.
