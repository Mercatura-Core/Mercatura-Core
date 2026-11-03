# MercaWeave v0.1 — WO06 Qt and native mining integration

Started 2026-11-02; completed 2026-11-03, America/Los_Angeles. Offline research only.

## Pre-edit inventory and boundaries

Verified clean research root /workspace/Mercatura-MercaWeave-v0.1 on
research/mercaweave-v0.1 at 259ef51316245a84348a1f6cdffaff9787bbcd8e.
Original /workspace/Mercatura-Core is clean on work at
4b88b619fb1b08c7f30112a38eb471880b2a6af4, also project/v0.1.
Both origin/project refer to https://github.com/Mercatura-Core/Mercatura-Core.git;
the two existing worktrees are retained without fetch/pull/push or branch edits.
No AGENTS.md was found at repository or ancestor paths.

Read the complete WO05 report. The authoritative locked spec and November 2
implementation directive are embedded in the self-contained WO01 attachment,
/workspace/attachments/05713317-32f0-4abd-8c3e-31a77781b6ee/Pasted text.txt,
Parts II/III. This is the supplied authoritative equivalent of the named files;
no missing-spec stop condition applies. Recovery defaults are 100 address
candidates and 20 successive unused groups; issuance rotates at 64 addresses.

Actual inspected paths: ReceiveCoinsDialog -> AddressTableModel::addRow
(BECH32M compatibility bucket, native v2) -> ReceiveRequestDialog and recent
requests; WalletModel::prepareTransaction -> interfaces::Wallet::createTransaction,
WalletModel::requestUnlock, SendCoinsDialog PSBT/FinalizeAndExtractPSBT/sendCoins;
WalletModelTransaction native fee-size helper; TransactionDesc native status;
MiningPage -> MiningSession -> wallet::GetMiningDestination -> SoloWorkProvider
and MiningController. MiningSession coordinates wallet preparation off Qt and
tracks unload tokens. GetMiningDestination accepts owned native v2 only and
SoloWorkProvider places the full reward into the selected payout script.
Existing fee, reward, CPU/GPU, MercaHash, unlock, send and broadcast paths stay
unchanged. Existing Qt mining tests use real regtest chain/node state and actual
mining, so they are outside this offline work order and must not be launched.

Implementation inventory, established before source edits:

1. Shared Qt capability/presentation model and optional native-widget panels;
   production network authorization remains unconditionally closed, with mock
   injection compiled only in isolated targets. No secret fields in Qt state.
2. Narrow additive bindings in WalletModel and existing Receive/Send/Mining
   layouts. Default v2 receive and native mining controls retain their paths.
3. Isolated backend adapting actual WO05 address, funding, PSBT and validation
   functions; expensive tasks off the GUI thread, owned public results and stale
   generation rejection, guarded QObject lifetimes.
4. Deterministic mature-reward planner/scheduler: disabled default, threshold 50
   per exact key, strict v2 destination, maturity, resource/fee/dust checks,
   reservations, idempotence, cancellation/conflict/reorg/unload handling. No
   signer/unlock/broadcast timer and no durable live-wallet scheduler.
5. Focused headless Qt and planner tests in three configurations, actual affected
   application translation-unit compilation, normal/isolated source and symbol
   boundary checks, WO05 regression reruns and preserved prior evidence.

Actual environment is Debian/GCC/x86_64 cloud, not the example WSL/Ryzen host.
WO05 Release baseline was rerun: 22/22 pass, 11.07 seconds. Qt runtime 6.8.2 was
present without development files. Matching Debian Qt development packages were
downloaded through the permitted package proxy and extracted only under ignored
work/wo06-tools; no system installation, node, wallet or datadir is involved.

This inventory was written before implementation; the qualified results follow.

## Implemented architecture and capability boundary

The pre-edit inventory above is retained as the original implementation plan.
The following sections record implemented behavior and qualification.

There are four separate layers: ordinary Qt presentation, an isolated WO05
wallet adapter, a reward policy/proposal planner, and the unchanged cryptographic/
consensus implementation. The normal GUI never links the adapter or planner.
It constructs the shared presentation model with grouped-wallet capability and
network authorization unavailable. Its ordinary compile of Availability returns
Network even if a caller fabricates positive capability booleans. Compilation,
unlock and opt-in cannot authorize an existing network.

`MERCATURA_MERCAWEAVE_QT_RESEARCH_TEST_ONLY` appears as a definition only in the
standalone test CMake project. It exposes mock-backend injection only in separate
isolated model objects/executables. There is no activation lookup, height,
schedule, network exception, runtime switch or production-accessible positive
capability setter. Source/compile/link checks and a negative header compilation
prove this boundary. The ordinary Qt control has no attachIsolated, Bridge,
Planner or protected-seed helper symbols. Normal and isolated QObject builds
are not linked into one binary.

Availability distinguishes compiled presentation, wallet support, unavailable
network authorization, lock, watch-only status, public inventory, group metadata,
signer, explicit opt-in, synchronization, loaded state and isolated test mode.
Unknown actions and indeterminate state fail closed. A separate diagnostic
reason explains every blocked action; these booleans never mint a WO04 proof.

## File-by-file implementation

| Files | Implemented responsibility |
|---|---|
| src/qt/mercaweavemodel.h/.cpp | Shared capability decisions, public snapshots, integer MCA formatting, asynchronous requests, lifetime/generation checks and closed production gate. |
| src/qt/mercaweavepanel.h/.cpp | Optional collapsed receive/send/mining panels, native controls, role/group/inventory/recovery display, exact transaction-aware estimates, security warnings and disabled reasons. |
| src/qt/walletmodel.h/.cpp | Parent-owned presentation model, encryption-state observation and optional native-v2 transaction preview; presentation errors do not change native send success. |
| src/qt/receivecoinsdialog.cpp, sendcoinsdialog.cpp, miningpage.cpp | Narrow panel insertion and model binding in existing layouts. Existing receiving, sending, QR/history and miner control bodies remain intact. |
| src/qt/CMakeLists.txt | Normal Qt model/panel sources only, with no research definitions or backend dependency. |
| src/test/mercaweave/qt_wallet_bridge.h/.cpp | Test-only adapter using actual WO05 issuance/inventory, funding, PSBT inspection and reward planning. No signing, unlock or broadcast service. |
| src/test/mercaweave/reward_planner.h/.cpp | Deterministic eligibility, per-key trigger, exact funding/splitting, aggregate fee caps, reservations, proposal lifecycle and controlled scheduler ticks. |
| src/test/mercaweave/wallet_funding.h/.cpp, wallet_psbt.h/.cpp | Explicit authenticated synthetic spend-height parameter, default 200 retained; narrow correction preserving dust-error provenance during sweep splitting. No protocol/digest/derivation change. |
| qt_model_tests.cpp, qt_integration_tests.cpp, reward_tests.cpp, qt_qr_tests.cpp | Headless actual Qt component, actual WO05 adapter, planner, native QR/clipboard and adversarial/lifetime tests. |
| check_qt_isolation.py, existing isolation allowlists | Normal-source erasure, unchanged native paths, compile/macro boundaries, actual application object compilation, symbol separation and negative mock-header checks. |
| qualify_qt_mining.py | Reproducible post-build comparison of unit summaries, locked vectors, WO05 workload records, 2× scaling and earlier evidence hashes. Writes summaries only under ignored work/. |

## Receive and witness-v2 compatibility

Normal ReceiveCoinsDialog still calls AddressTableModel::addRow with BECH32M,
the native witness-v2 compatibility bucket. No address default changes, silent
migration, ECDSA/Schnorr fallback or new ordinary-wallet storage exists.
ReceiveRequestDialog, recent requests, labels, clipboard and QR sources are
unchanged. The existing QRImageWidget, GUIUtil, PlatformStyle, QValidatedLineEdit
and Bech32 implementation are linked unmodified into a focused headless test.
That test renders a synthetic native-v2 program, copies the actual exported QR
image and rejects an overlong URI. It never supplies a funded destination.

The optional panel starts collapsed; normal v2 receiving causes no new modal
warning. The grouped issue/rotation actions remain disabled in the normal GUI.
Isolated tests exercise explicit opt-in, account selection, actual purpose-0
issuance, public ranges, group index, lifecycle, the 64-address rotation boundary
and purpose-2 mining-group distinction. Group identity uses the full exact
1,952-byte public key in the adapter, never a label, account number or truncated
identifier. Widget labels are descriptions, not ownership evidence.

Unlocked isolated issuance delegates to WO05 Wallet::Issue. Locked/watch-only
issuance consumes only explicitly pre-generated, verified public inventory;
it does not call secret derivation or unlock. Exhaustion fails closed. The
inventory queue is research-local, not a durable deployed reservation database.
All grouped demonstration text says SYNTHETIC ONLY and is not a Bech32 v3 payment
destination. Ordinary Qt has no backend capable of issuing it.

The panel explicitly warns that spending can link addresses sharing a key,
compromise of a group key threatens its outstanding coins, different group
indexes do not isolate master-seed compromise, nonces do not remove these risks,
and large deployments may require metadata backups and wider discovery.

## Send, funding and PSBT visibility

Native WalletModel creation/signing and SendCoinsDialog commit/PSBT paths remain
the existing implementation. The normal preview reads actual selected coin
scripts, native fee size, weight, total fee and change position. An unavailable
or failing optional preview is cleared without converting a valid native send
into a creation failure. Native fee-rate presentation uses integer CFeeRate;
MCA formatting preserves 100 base units per MCA and never uses floating point.

The isolated adapter uses actual Funding::Fund with Automatic, StandardPQ or
MercaWeave eligibility, and actual GroupPSBT::Inspect. A strict mode that cannot
fund reports Funds and clears stale estimates; it does not switch modes or
reinterpret v2 ownership. Tests exercise native-only, grouped-only and mixed
actual PSBTs. Previews show inputs/outputs, native input count, per-transaction
group authorizations, anchors, followers, expected ML-DSA signatures, full wire
bytes, total weight, largest transaction weight, fee/rate and change type.
Split-proposal estimates total every transaction, not just the first batch.

Every group uses its exact public key; unrelated keys remain independent.
Every v2 input retains its native authorization. Fees and estimates come from
WO05 serialization, including the 5,301-byte anchor, 39-byte follower, ordinary
inputs, marker/flag/counts, outputs and change. Existing multi-recipient batching
is not attributed exclusively to grouping. External PSBT previews also enforce
full-byte fee policy; a cryptographically well-formed under-fee PSBT is blocked.

Unsigned or partially authorized PSBTs are explicitly incomplete and never
presented as ready for broadcast. Existing signatures are actually verified
against the owned synthetic view before complete authorization is displayed.
Complete authorization still displays grouped broadcasting unavailable. There
is no new grouped finalize/import/export/merge/external-device/broadcast control
in the normal GUI. Existing native PSBT controls are unchanged. WO05 already
supplies isolated partial signing/combine/finalization/extraction, including
foreign participants, but this work does not pretend that the normal global-
Params PSBT finalizer safely supports unactivated groups. The new test adapter
has no Sign method; explicit test preparation uses the existing WO05 signer.

## Native mining integration and zero software fees

MiningPage retains its existing navigation, CPU/GPU worker controls, adaptive
budgets, start/stop handlers, resource checks, hashrate, runtime and reward
statistics. MiningSession, GetMiningDestination, SoloWorkProvider and every
mining/cryptographic source are unchanged. Removing the three panel-insertion
lines and one include restores MiningPage byte-for-byte to WO05. Receive and
Send have equivalent original-source erasure checks.

The optional panel identifies independent-key v2 as the actual reward default,
shows unavailable authorization, mining-group lifecycle, mature/immature/eligible
counts and future consolidation estimates. Isolated selection checks that the
full public key belongs to an issued purpose-2 group. It records synthetic
configuration only and never changes MiningSession's real reward destination.
Repeated synthetic rewards use one designated address/nonce without issuing an
address per reward; this does not authorize reuse of signing randomness.

Mining software, developer and pool/operator fees remain 0%. No donation,
hidden destination, reward deduction, intermediate grouped output, emission,
coinbase maturity, consensus or MercaHash V1/V2 change exists. Blockchain fees
for later spending/consolidation remain separate and visible.

Actual linked full-application mining sessions and CPU/GPU operation were not
run. The inherited mining Qt tests initialize real regtest chain/node state
and actually mine; running them would violate WO06. Compatibility evidence here
is source preservation, normal compilation of the actual affected application
translation units, production-closed controls and runtime tests of the shared
panels/models. It is not a full native-miner runtime claim.

## Consolidation planner and scheduler

Planner is a real stateful C++ component over an actual research Wallet and owned
View. It does not contain seeds, a timer, an unlock method, signing callback or
broadcast interface. Controlled evaluations are scheduler ticks. Sweeping is
disabled by default, requires explicit opt-in and isolated authorized capability,
and defaults to 50 mature reward coins in one exact-key group. The trigger is
configurable. Twenty-five rewards under each of two unrelated keys do not meet
the 50-per-key trigger.

Eligibility reacquires coins and verifies wallet ownership, native v3 script,
purpose 2, actual coinbase flag, maturity at the authenticated synthetic spend
height, safety/confirmed status and external reservations. Duplicate candidate
outpoints fail. Watch-only, locked, unsynchronized, missing-signer, unavailable-
network, unloaded or reorganization states cannot plan an executable operation.
No silent unlock or migration occurs. The planner supports an exact selected
key and deterministic outpoint ordering.

An eligible default destination is a freshly issued independent-key v2 record,
generated through WO05 on the worker only after capability/threshold checks.
An explicit destination is strictly validated as a native-v2 Bech32m destination
in the isolated fixture network. V3/invalid targets fail; no intermediate group
destination is created. Ordinary production destinations are never changed.

Construction calls Funding::Sweep, not a second transaction builder. Existing
400,000-WU standardness, full-byte fees, minimum one-unit fee, two-unit dust,
resource checks and 4,096-input/256-group/1,024-member protocol bounds apply.
The stricter effective restriction controls splitting. A 1,025-member single-key
workflow actually splits into two transactions. Fee caps apply to their combined
fee as well as individual transactions. Changing fees, destination, balance,
view epoch, height or policy revision invalidates the old proposal identity.
No maximum theoretical group is advertised as relayable.

One pending proposal owns a distinct input set. Repeated unchanged evaluation
reuses its identity; it does not generate a conflicting proposal or a new
receiving address. Cancellation/failure/conflict/replacement release pending
reservations and suppress blind rescheduling of that exact fingerprint until
state/policy changes. Reorgs and loss of authorization retire stale proposals.
Unload is terminal for that planner owner. History is bounded to 256 entries;
proposal sequence exhaustion fails closed.

Attach checks the exact unsigned body, actual spent data, current epoch and
original spend height. It verifies supplied PSBTs through WO05 and validates
complete transactions through WO04. Missing signatures remain Partial; invalid
signatures fail and suppress the proposal. It accepts no caller success boolean.
There is no submission or Broadcast state. Current real-network input policy
still rejects v3; isolated consensus-fixture success is not mempool acceptance.

The scheduler has no durable database/journal, native coin-lock lease, automatic
transaction sender or restart recovery. Those belong to WO07. GUI cancellation
covers pending proposals; generation/lifetime invalidation safely drops in-flight
results. No unrestricted privileged signer or automatic wallet unlock exists.

## Thread safety, privacy and recovery

Model is GUI-thread-affine and enforces that at runtime in Release as well as
Debug; wrong-thread mutations/requests are rejected. QtConcurrent workers own
the backend and immutable request values, never raw QObject/wallet-model
pointers. QFutureWatcher completion is context-bound to the model; QPointer
bindings, generation checks and nonblocking invalidation discard late results
on opt-out, stale state or destruction. The owned research wallet survives only
as long as an in-flight job needs it; old results cannot update destroyed views.
Duplicate/reentrant actions are blocked while a request is pending.

No seeds, expanded signing keys, decrypted material or signer state appear in
Qt snapshots, signals, labels, logs or scheduler state. Secret access and memory
cleansing remain the existing WO05 implementation. The production getter reads
lock/private-key-disabled state only. Expensive new issuance, PSBT verification
and planning run on the worker; existing native transaction creation behavior
is preserved rather than redesigned.

Recovery display includes account/purpose/group, issued ranges, rotation,
metadata backup availability and discovery limitations. Defaults are 100 address
candidates per branch and 20 successive unused groups per account/purpose.
They are hints, not complete seed-only recovery guarantees. Live/resumable scans
and native metadata export/import are explicitly unavailable. Watch-only
tracking uses explicit public records. Normal UI uses existing Qt controls,
translations, palette and layout conventions; no third-party GUI framework or
custom dashboard is introduced. No broad platform visual/runtime claim is made.

## Focused threat review

| Attack/failure | Implemented control and test | Remaining boundary |
|---|---|---|
| Accidental grouped issuance / fake positive capability | Closed normal compile, disabled actions, default opt-out, negative header/symbol/positive-boolean tests. | Deployment authorization is not implemented. |
| Stale lock, missing signer or watch-only signing | Backend rechecks actual wallet; locked public inventory is separate; no unlock/sign API. | Real CWallet authorization lifecycle remains WO07. |
| Label/account used as group proof | Full exact public-key identity plus actual WO05 ownership reconstruction. | Independent cryptographic review remains required. |
| Duplicate/conflicting sweeps | Pending input set, fingerprint reuse, terminal suppression, external reservations and generation checks. | Durable database/native coin locks and mempool reconciliation are deferred. |
| Reorg or changed maturity/balance | Actual synthetic coin height/flag, checked epoch/height, reorg pause and fresh acquisition. | Real chainstate/reorg notification integration is deferred. |
| Fee exhaustion / split underreporting | Per-transaction and aggregate fee caps; real sizes; split totals; under-fee and dust tests. | Live fee estimation/RBF/package policy is unqualified. |
| Malicious destination or stale PSBT | V2 destination validation, exact-body binding, all signature checks and explicit partial/failure states. | Device/coordinator interoperability and full offline prevout authentication remain. |
| Late callback / model unload | Owned worker backend, weak Qt binding, context-bound completion, generation invalidation and wrong-thread rejection. | TSan and system-level stress remain. |
| Recovery false confidence / key linkage | Explicit metadata/discovery/compromise warnings; no seed-only guarantee or nonce privacy claim. | Native rescan and database migration remain. |

This is the implementing agent's focused self-review, not an independent audit.

## Qualification commands and evidence

The final code checkpoint is bc59fb5cbbbc9710e885b71fbd3e92aef6966919. All earlier evidence is preserved.
Matching Qt 6.8.2 development packages, Boost 1.83 headers, GL development headers
and libqrencode 4.1.1 development files were extracted under ignored work/ only.
They were obtained through the allowed package proxy, with TLS verification and
no system installation. A normal developer Qt/Boost installation can supply the
same dependencies instead of the local prefix.

From the research root:

```bash
WO06_QT_PREFIX="$PWD/work/wo06-tools/qt-sdk/usr"
work/tools/cmake/data/bin/cmake -S src/test/mercaweave -B work/build-release -DCMAKE_BUILD_TYPE=Release -DMERCAWEAVE_QT_TESTS=ON -DCMAKE_PREFIX_PATH="$WO06_QT_PREFIX"
work/tools/cmake/data/bin/cmake --build work/build-release -j2
work/tools/cmake/data/bin/ctest --test-dir work/build-release --output-on-failure
work/tools/cmake/data/bin/cmake -S src/test/mercaweave -B work/build-debug -DCMAKE_BUILD_TYPE=Debug -DMERCAWEAVE_QT_TESTS=ON -DCMAKE_PREFIX_PATH="$WO06_QT_PREFIX"
work/tools/cmake/data/bin/cmake --build work/build-debug -j2
work/tools/cmake/data/bin/ctest --test-dir work/build-debug --output-on-failure
work/tools/cmake/data/bin/cmake -S src/test/mercaweave -B work/build-sanitize -DCMAKE_BUILD_TYPE=RelWithDebInfo -DMERCAWEAVE_SANITIZERS=ON -DMERCAWEAVE_QT_TESTS=ON -DCMAKE_PREFIX_PATH="$WO06_QT_PREFIX"
work/tools/cmake/data/bin/cmake --build work/build-sanitize -j2
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 work/tools/cmake/data/bin/ctest --test-dir work/build-sanitize --output-on-failure
python3 src/test/mercaweave/qualify_qt_mining.py --release work/build-release --debug work/build-debug --sanitize work/build-sanitize
git diff --check
```

| Configuration | CTest pass/fail/skip | Application qualification | Additional scale check |
|---|---|---|---|
| Release | 28 / 0 / 0 | Four actual affected normal application TUs compile. | Two Qt binaries at 2× scale. |
| Debug | 28 / 0 / 0 | Same four actual normal application TUs compile. | Same two Qt binaries at 2× scale. |
| ASan + UBSan | 28 / 0 / 0 | Same application TUs and new logic instrumented. | Same two Qt binaries at 2× scale. |

All 22 prior targets remain and actually reran in each final configuration. Six
new targets cover normal capability closure, isolated Qt lifecycle/previews,
reward policy/scheduler, real WO05 Qt adapter, native QR/copy, and compile/source/
symbol isolation. Deterministic unit summaries per build are: normal Qt 24,
isolated Qt 39, reward planner 55, Qt wallet/mining adapter 57 and native QR 5
checks (180 new assertions); wallet 248 and PSBT 67 assertions with 1,000 rejected
PSBT mutations. Wallet increased from 247 by the dust-error regression test.
WO04's existing 2,153 assertions, 2,000 mutations and 2,800 concurrent script
reads remain in the rerun integration suite.

The post-build runner verifies 84 final CTest passes, 72 workload comparisons
against WO05's 24-row reference (including 48 cross-build comparisons), identical
locked vector stdout, deterministic unit summaries and six 2×-scale runs. It
checks all 57 earlier evidence files byte-for-byte. Full vectors and
public fixture hashes remain unchanged. Code, the runner and this engineering
report are committed; raw logs, binaries, dependency downloads, Qt caches and
experiment output remain ignored under work/ or /tmp, as WO06 requires.

ASan and UBSan report no remaining runtime failures. The new QR target initially
needed QValidatedLineEdit's real RTTI implementation from GUIUtil's sanitizer
metadata; linking the actual unchanged source fixed it. No fake RTTI or new
sanitizer suppression was introduced. The older pre-existing policy.cpp vptr-
only exception remains scoped to its WO03 malleability target. New research
logic remains fully instrumented. LeakSanitizer remains unavailable in this
ptrace environment and is not called passed; detect_leaks=0 is explicit. TSan,
Clang/alternative compilers, coverage-guided fuzzers and independent audit were
not performed. Cross-platform linked application runtime was not performed.

Performance is not deployment capacity: the exact sweep implementation rebuilds
prefixes, and the 1,025-member split/aggregate-cap case makes Debug substantially
slower than Release. This is a known research planner performance limit, not a
MercaHash change or a reason to weaken bounds. Headless scaling checks exercise
widgets at 1×/2×; manual full-application theme, accessibility and platform review
remain appropriate before deployment.

## Local checkpoints, hygiene and final acceptance

Starting HEAD/branch are recorded above. Validated implementation commits:

```text
f18a08251e08114c6e6b08c206668efcd7cc3fa7 Add fail-closed Qt capability model and optional grouped PQ panels
a6f368ae888670087878a5f3841f85a5b664cf39 Preserve dust failures instead of treating every sweep policy error as size exhaustion
4d87b89b2ba26367b27fc595cc3728179c137cb4 Pass authenticated spend height through research funding and PSBT preflight
a682535a5717cb47935bdaf43a6fac8e62ab6412 Plan mature grouped rewards with bounded idempotent proposal state
1e074a5468d0091c7a6b011f95b0cc0220658209 Reject cross-thread Qt model writes and keep native preview status separate
200ab34ca081e52424c3a931a7e55ed416e18cdb Bind split reward proposals to aggregate fees and their exact validation context
82be2434627e00f59cc1e07a1f19354cc5e4ed99 Bind optional receive send and mining panels to an isolated WO05 wallet adapter
eebc758f850295f607a7601e7fd510b085bce548 Show aggregate size fees and authorization counts for split proposals
64a46ddf8c8300ac6da460acbdef4e4f516c9539 Qualify native QR clipboard and normal Qt mining isolation without a node
bc59fb5cbbbc9710e885b71fbd3e92aef6966919 Report strict funding-mode failures accurately and clear stale previews
```

The final containing report/qualification-runner commit is the ending WO06 HEAD.
Its own SHA is provided in the final response and by `git rev-parse HEAD`, rather
than recursively embedded in its content. Both worktrees are clean after that
commit; the original branch/HEAD and project/v0.1 remain unchanged. No prior
checkpoint was amended, no branch recreated and no push/pull/merge/rebase/reset
occurred. Only authorized Qt integration, isolated helpers/tests and this report
change. No secrets, raw logs, downloaded dependencies, binaries or datadirs are
staged. All coherent commits followed relevant tests, diff inspection and
`git diff --check`.

**READY WITH EXPLICIT RESTRICTIONS**

WO06 implements and tests normal-gated Qt panels/models and actual isolated WO05
wallet/reward planning integration. It authorizes no production custody,
activation, deployment or real broadcast. Unactivated raw v3 still follows the
normal node's unknown-witness behavior; UI gating is not consensus protection.
Do not fund those outputs as protected MercaWeave custody.

WO07 must separately qualify full-node cache/CScriptCheck/ConnectBlock behavior,
actual chain and coinbase maturity, CWallet/database persistence and migration,
real mempool acceptance/rejection/RBF/packages, broadcast, reorg/restart behavior,
native coin reservations and durable scheduling, live future sweeps, linked Qt
wallet/miner lifecycle, CPU/GPU runtime compatibility, multi-platform visual and
runtime testing, system stress and independent audit preparation. None is marked
complete by these isolated tests. No WO07 work is initiated here.
