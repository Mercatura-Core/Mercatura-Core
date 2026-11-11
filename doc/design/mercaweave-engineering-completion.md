# MercaWeave engineering completion and owner handoff

**B — CONDITIONALLY COMPLETE**. Linux CI is blocked by dependency DNS, a local
test-only correction awaits publication/requalification, and interactive desktop
review remains outstanding. Windows/macOS dedicated qualifications passed on the published source. The owner can begin isolated regtest testing on an available
graphical machine using the development build and provided helper.

Production integration is identified by
`c93806131232f5df65958102892d29007c5ff773`. Public MercaWeave activation remains
unset. This checkpoint does not merge, release, create an installer, deploy,
select public activation, or begin CPU/GPU miner optimization. Independent
security approval remains a separate public-deployment gate.

## MANUAL MERCAWEAVE TESTING — PROJECT OWNER

Use **only disposable test wallets**. The helper reuses the existing native
three-node qualification infrastructure: Qt sender, daemon recipient, daemon
validator/miner, loopback-only PQ connections, one temporary regtest schedule,
real PoW and mature funds. It never deletes a directory. Its temporary height
110 is a fixture, not a public activation proposal.

The owner helper is **local handoff material, not yet pushed**:
[mercaweave-owner-regtest.py](../../contrib/devtools/mercaweave-owner-regtest.py).
Download/copy that single file to your development checkout's
`contrib/devtools/`. It imports the already-published fixture from `c938061`;
no production patch, research backend, or new testing framework is required.
Keep these instructions beside the wallet while testing.

New actual Qt evidence: [grouped Receive](mercaweave-engineering-completion-artifacts/owner-qt/Owner-Receive-v3.png),
[grouped Inputs](mercaweave-engineering-completion-artifacts/owner-qt/Owner-Inputs-mode-2.png),
[normal grouped confirmation](mercaweave-engineering-completion-artifacts/owner-qt/Owner-Confirm-mode-2.png).
These are unedited native offscreen captures; interactive desktop testing remains outstanding.

### 1. Obtain a development build on your graphical computer

The current worker has verified development applications under
`/workspace/Mercatura-MercaWeave-upgrade/work/build-native-release/bin/`:
`mercatura-qt`, `mercaturad`, and `mercatura-cli`. Their identities are recorded
below. This Debian worker has no interactive desktop. Do not assume its binaries
are portable to Ubuntu 22.04 or native Windows.

For Windows 11 with Ubuntu 22.04 under WSL2, open the Ubuntu terminal on the
owner's machine. WSLg must supply a real display; check it first:

```bash
printf 'DISPLAY=%s\nWAYLAND_DISPLAY=%s\n' "$DISPLAY" "$WAYLAND_DISPLAY"
```

If both are empty, arrange WSLg/a graphical desktop before interactive testing.
An offscreen launch cannot provide hands-on interaction. The following target
machine procedure is provided for reproduction; it was not executed under WSLg
on this cloud worker. Use a separate development checkout, with GCC >=12.1,
CMake >=3.22, Boost >=1.74, Qt >=6.2 and the wallet dependencies. Ubuntu 22.04's
stock GCC 11 is below the source's compiler minimum.

```bash
sudo apt-get update
sudo apt-get install gcc-12 g++-12 cmake ninja-build python3 pkg-config \
  libboost-dev libevent-dev libsqlite3-dev libqrencode-dev \
  qt6-base-dev qt6-tools-dev qt6-tools-dev-tools
```

Clone into a **new** directory and pin the qualified source:

```bash
git clone --single-branch --branch integration/mercaweave-v0.1-upgrade \
  https://github.com/Mercatura-Core/Mercatura-Core.git mercatura-owner-src
cd mercatura-owner-src
git checkout --detach c93806131232f5df65958102892d29007c5ff773
```

Copy the linked owner helper into `contrib/devtools/` in this checkout. Build
ordinary development executables; no install/package target is needed. Allow
roughly 30–60 minutes on the stated Ryzen 5/16 GB machine, depending on caches:

```bash
cmake -S . -B build/mercaweave-owner -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc-12 \
  -DCMAKE_CXX_COMPILER=g++-12 -DBUILD_GUI=ON -DBUILD_TESTS=ON \
  -DBUILD_GUI_TESTS=OFF -DENABLE_IPC=OFF -DWITH_ZMQ=OFF \
  -DWITH_USDT=OFF -DWITH_DBUS=OFF -DWITH_QRENCODE=ON \
  -DWITH_CUDA_MINING=OFF -DWITH_OPENCL_MINING=OFF \
  -DMERCATURA_PUBLIC_TESTNET_RELEASE=OFF
cmake --build build/mercaweave-owner \
  --target bitcoin-qt bitcoind bitcoin-cli -j4
```

The internal CMake targets above emit the Mercatura-named executables. For this
cloud checkout, skip rebuilding and use `work/build-native-release` below.
Native Windows development instructions remain in [build-windows.md](../build-windows.md);
this Linux worker supplies no Windows executable or official installer.

### 2. Start the three real nodes — Terminal 1

Run from the checkout. Choose a new path that does not exist. Initial seeding
normally takes 2–4 minutes; wait for the printed **READY** line and leave this
terminal open. No installed mainnet/public-testnet profile is used.

```bash
MCA_OWNER_ROOT="$HOME/mca-owner-$(date +%Y%m%d-%H%M%S)"
printf 'Fixture path: %s\n' "$MCA_OWNER_ROOT"
python3 contrib/devtools/mercaweave-owner-regtest.py start \
  --build-dir build/mercaweave-owner --state-dir "$MCA_OWNER_ROOT"
```

The normal command prefers X11/XWayland when DISPLAY is set, using the base Qt
xcb plugin. A Wayland-only session needs its matching Qt Wayland runtime. Qt
opens the `sender` wallet; `receiver` and `observer` run independently in daemons.
The READY output contains the independent recipient's v2/v3 addresses, chain tip,
balances and eligible input counts. It confirms authenticated grouped ownership.
For headless validation only, explicitly add `--qt-platform offscreen`.

### 3. Control and verify — Terminal 2

Use the exact fixture path printed by Terminal 1. Substitute it once below:

```bash
MCA_OWNER_ROOT='PASTE-EXACT-FULL-FIXTURE-PATH'
python3 contrib/devtools/mercaweave-owner-regtest.py status \
  --state-dir "$MCA_OWNER_ROOT"
```

`status` checks all three nodes identify as regtest, have PQ-authenticated and
encrypted loopback peers, agree on their tip, and recognize authenticated grouped
coins. Amounts are MCA. It also prints recipient addresses. Copy the v2 recipient
for sends below; a recipient's type does not select the source authorization.

### Test A — Standard PQ witness-v2, through Qt

1. In Qt, select wallet **sender**, open **Receive**, select **Standard PQ —
   Witness v2**, enter a recognizable label, and click **Create new receiving
   address**. Copy that address from the native request dialog.
2. Fund the copied address using fixture setup RPC, then confirm the funding:

```bash
python3 contrib/devtools/mercaweave-owner-regtest.py fund \
  --state-dir "$MCA_OWNER_ROOT" --address 'PASTE-QT-RECEIVE-ADDRESS' --amount 5
```

3. Open **Send**. Select **Standard PQ — Witness v2 only**. Enter the independent
   recipient's address and **1.00 MCA**. Review the normal fee and confirmation,
   then sign and send through Qt. The initial regtest fee estimator can lack
   history; the existing fallback/custom controls remain available.
4. Copy the transaction ID from Qt's transaction details. Verify two-hop relay,
   mine on the independent node, and verify confirmations:

```bash
python3 contrib/devtools/mercaweave-owner-regtest.py verify \
  --state-dir "$MCA_OWNER_ROOT" --txid 'PASTE-QT-TRANSACTION-ID'
python3 contrib/devtools/mercaweave-owner-regtest.py mine --state-dir "$MCA_OWNER_ROOT"
python3 contrib/devtools/mercaweave-owner-regtest.py verify \
  --state-dir "$MCA_OWNER_ROOT" --txid 'PASTE-QT-TRANSACTION-ID'
```

Expect input versions containing only `2`, presence on all three nodes, then at
least one confirmation. `actual_wallet_fee` follows the wallet RPC's negative
fee convention; its absolute value is the cost.

### Test B — Genuine MercaWeave witness-v3, through Qt

1. In **Receive**, select **MercaWeave Grouped PQ — Witness v3**, label the request,
   create it through Qt, and copy its genuine grouped address.
2. Fund it twice so you can inspect two grouped inputs sharing one group:

```bash
python3 contrib/devtools/mercaweave-owner-regtest.py fund \
  --state-dir "$MCA_OWNER_ROOT" --address 'PASTE-QT-GROUPED-ADDRESS' --amount 5
python3 contrib/devtools/mercaweave-owner-regtest.py fund \
  --state-dir "$MCA_OWNER_ROOT" --address 'PASTE-QT-GROUPED-ADDRESS' --amount 5
```

3. In **Send**, choose **MercaWeave — Witness v3 only** and click **Inputs…**.
   Input type appears beside Amount. In list mode select those two newly funded
   5.00-MCA outputs using their label/address. Review the fee preview, close Inputs,
   enter the independent v2 recipient and 1.00 MCA, and sign/broadcast using Qt.
4. Run the same `verify`, `mine`, `verify` commands from Test A with this new txid.
   Expect `[3, 3]`, one grouped anchor, one follower, and independent relay/block
   presence on all three nodes. These structural observations accompany actual
   native consensus validation; the helper does not substitute its own signer.
5. Check recipient and sender balances using `status`, and inspect confirmations
   in Qt. A v2 recipient with v3 inputs remains a grouped-authorized spend.

### Test C — Coin Control and mixed inputs

1. Choose **Automatic**. Open **Inputs…**, select an eligible v2 coin, and close.
   Switch to **MercaWeave v3 only** while retaining that incompatible selection.
   Attempt a 1.00-MCA send. Expect an explicit spending-mode conflict and no
   broadcast. Deselect the conflicting coin explicitly; no fallback is intended.
2. In Automatic, manually select one v2 and one v3 input, review the ordinary
   confirmation, and send. `verify` should report both versions, independent
   relay, then confirmation after `mine`.
3. Toggle list/tree mode, filter inputs, and reopen Inputs. Check type labels,
   amounts, confirmations, checkboxes and selections. Full addresses remain
   available in tooltips/copy actions; group-aware fee preview remains approximate
   until funding, with actual signed size reported by `verify`.

### Test D — Wallet security and persistence

Use only a disposable test passphrase. In Qt **Settings → Encrypt Wallet…**,
follow the native prompts. The helper never stores or supplies a passphrase.
While locked, attempt a legitimate Send and cancel the native unlock prompt:
no signing/broadcast should occur. Retry and enter the correct passphrase in Qt;
verify the legitimate transaction as above. Unlock is requested by the signing
workflow; do not assume a separate permanent-unlock menu exists. The temporary
unlock context normally relocks after use.

With the fixture still running and no confirmation dialog open, restart all
three owned processes, preserving wallets, balances and grouped records:

```bash
python3 contrib/devtools/mercaweave-owner-regtest.py restart --state-dir "$MCA_OWNER_ROOT"
python3 contrib/devtools/mercaweave-owner-regtest.py status --state-dir "$MCA_OWNER_ROOT"
```

In Qt, reopen **sender** if necessary using the wallet menu. Use File's normal
close/open-wallet actions to exercise reload; verify the existing v2 balance and
authenticated grouped coins. Actual desktop lock/unlock and these owner's manual
observations remain for the owner; earlier native Qt/wallet lifecycle tests are
preserved separately, not described as new hands-on execution.

Stop safely from Terminal 2; Terminal 1 returns after all owned nodes stop:

```bash
python3 contrib/devtools/mercaweave-owner-regtest.py stop --state-dir "$MCA_OWNER_ROOT"
```

Use this command instead of broad process-killing commands. It also handles a
lost foreground controller by authenticating to these exact disposable profiles
and stopping any surviving regtest nodes through RPC. All data/logs remain.
Each `start` requires a new directory; `restart` preserves the active fixture.
After stop, retain the old fixture for inspection/backups and choose a new path
for a fresh session. No automatic cleanup removes a wallet. Do not close the
foreground controller before stopping the cluster.

## Publication and platform qualification

Starting checkout: clean `integration/mercaweave-v0.1-upgrade` at
`c93806131232f5df65958102892d29007c5ff773`. Initial remote:
`ca3b2d9dde296b4ebbd120212c243fcb066f1581`. Fetch/ancestry found zero remote-only
and exactly two local-only commits: `2d79c330f5ac246156d758e34763bbba9ff45524`
and `c93806131232f5df65958102892d29007c5ff773`. Both historical evidence manifests
and the source scope were verified. The only production change in those commits
is Coin Control presentation; the other C++ change is Qt test code.

The explicitly authorized ordinary fast-forward push succeeded. Git's remote-ref
read and GitHub commit API both confirmed exact remote HEAD `c938061`. No force,
rebase, merge, unrelated commit, release or further push occurred.

New workflows on this exact source:

- [Standard CI 43043090272](https://github.com/Mercatura-Core/Mercatura-Core/actions/runs/43043090272): **PASS**.
- [Dedicated native qualification 43043090273](https://github.com/Mercatura-Core/Mercatura-Core/actions/runs/43043090273): **FAIL overall**, Linux dependency DNS; Windows/macOS jobs **PASS**.

| Platform/job | Build | Unit/Qt (pass / fail / skip) | Native functional | Result / timing |
|---|---|---|---|---|
| Linux x86_64 native Qt, `130599807180` | NOT RUN | NOT RUN | NOT RUN | **BLOCKED**, apt DNS exit 100 |
| Windows x86_64 MSVC Qt, `130599807693` | PASS | **192 / 0 / 1**, 1614.55 s | **59 / 0 / 0**, 1030 s runner | **PASS**, exact `c938061` checkout verified |
| macOS ARM64 Qt, `130599807638` | PASS | **190 / 0 / 3**, 2018.15 s | **59 / 0 / 0**, 1254 s runner | **PASS**, exact `c938061` checkout verified |
| Standard macOS ARM64 headless, `130599790036` | PASS | **187 / 0 / 1**, 1721.62 s | **59 / 0 / 0**, 1368 s runner | **PASS**, exact `c938061` checkout verified |
| Standard Windows headless, `130599790361` | PASS | **187 / 0 / 1**, 1616.20 s | **59 / 0 / 0**, 1149 s runner | **PASS**, exact `c938061` checkout verified |
| Standard CI lint, `130599866827` | — | PASS | — | **PASS** |

Linux job `130599807180` **failed in dependency installation**, before CMake,
with apt exit 100 and `Temporary failure resolving 'azure.archive.ubuntu.com'`.
The Linux build/unit/functional gate is **BLOCKED by infrastructure**, not a
passing or failing application test. Its
[exact diagnostic](mercaweave-engineering-completion-artifacts/linux-ci-dns-failure.txt)
is retained. The workflow remains non-green. No tests are removed/skipped and no
unverified package-mirror source change is introduced. A future complete
cross-platform rerun needs the separately authorized repaired test source.

The former Windows Qt C4996 compilation failure has been resolved on the actual
MSVC runner: the published typed exception assertion compiles without disabling
tests or compiler diagnostics. Windows and macOS unit/Qt and full native functional suites completed
successfully. Their final job conclusions are **success**. Linux application
qualification remains blocked; no platform result is inferred from another OS. Completed-job logs verify checkout SHA, compiler and counts. Windows used
**MSVC 19.51.36260.0**; macOS used **AppleClang 17.0.0.17000013**. CTest-level skips
are the inherited classical `script_assets_tests`, plus the two existing macOS
Qt application restart/close lifecycle cases. No new skip was introduced.
The complete Windows wallet Qt executable passed in **232.54 s**. Native
functional counts are each one invocation; no partial runs are combined.
[Windows Qt details](mercaweave-engineering-completion-artifacts/ci-job-130599807693.json),
[macOS Qt details](mercaweave-engineering-completion-artifacts/ci-job-130599807638.json),
and [standard macOS details](mercaweave-engineering-completion-artifacts/ci-job-130599790036.json)
retain every native case and runtime. The dedicated workflow is **FAIL overall**
because Linux dependency installation failed. It is not called green. Cross-packaging remains deliberately
skipped for this integration branch and is not a passing installer build.

## Focused integration review and reused evidence

This is an internal completion review, not independent external security approval.
The following production boundaries are unchanged by `2d79c33`/`c938061` and by
this handoff. Previous findings/vectors remain authoritative within their stated
scope; frozen WO08 limitations are historical and are not erased.

| Boundary | Review anchor / existing evidence reused |
|---|---|
| Public activation unset; regtest-only option | `src/kernel/chainparams.cpp`, activation design and `mercaweave_activation_tests`; only regtest assigns the optional parameter |
| Native v2 authorization unchanged | No wallet/Script/crypto/consensus changes in the two pushed commits; native regression and Qt actual v2 send |
| v3 candidate-height/active-tip rules | Activation design, prior 59-case native run and real three-node H−1/H/reorg cases |
| Transaction/prevout/genesis binding and membership | `GroupContext::MatchesJob`, current-coin reconstruction, permanent wrong-amount/script/key/nonce/role/domain vectors |
| Cache/assumevalid cannot bypass groups | `CheckInputScripts` dispatches grouped authorization before the legacy cache; `ConnectBlock` forces checks when `SCRIPT_VERIFY_MERCAWEAVE` is set |
| PSBT completeness and finalization | Native `FinalizeWalletPSBT`; incomplete signing stays incomplete; prior wallet/PSBT tests and new fixture funding requires both signing/finalization complete |
| Authenticated ownership and recovery | `CWallet::MercaWeaveAvailable`, native records/mandatory wallet flags, prior restart/rescan/reindex/backup tests |
| Explicit v2/v3 source selection | Native policy in `CCoinControl`; prior spending-mode cases and new actual Qt conflict/mixed sends |
| Recipient type independent of source | Prior fee/spending qualification; new actual Qt v3-authorized spend pays independent v2 recipient |
| Grouped fees | Existing group-aware estimator, prior exact two-v2 versus two-v3 size comparison, new actual Qt two-input grouped signed structure |
| Lifecycle/stale capabilities | Prior complete Qt wallet executable, lock/unlock/model-unload tests, 30-checkpoint reorg invalidation and new fixture lifecycle check |
| Research isolation | No production assignment of guarded fixture execution, existing build/test isolation fences; ordinary native wallet/consensus paths used |
| Latest changes | `coincontroldialog.cpp` only presentation/tooltips; Qt test/probe/functional evidence; no protocol or key format changes |

No new consensus, cryptographic, ownership or PSBT defect has been demonstrated
by this targeted review. Public activation remains postponed. Independent audit,
release approval, operator readiness and public activation are separate gates.
The project's next miner integration stage is not begun by this checkpoint.

Reused reports, unchanged:
[Send modes](mercaweave-send-spending-modes.md),
[multi-node qualification](mercaweave-multinode-regtest-qualification.md),
[CI/UI hardening](mercaweave-preactivation-ci-ui-hardening.md),
[activation](mercaweave-upgrade-activation.md),
[build/approval](mercaweave-upgrade-build-and-approval.md), and
[frozen security qualification](mercaweave-v0.1-final-security-qualification.md).
Historical local evidence includes a clean 59/0/0 native invocation, three Qt
executables, four wallet suites, 30 Qt-backed three-node checkpoints, the original
15 plus 38 later screenshots, and three pre-existing six-worker mining skips for
host budget. None is relabeled as a newly executed test in this task.

## New local validation and binary identities

The exact existing Qt/daemon/CLI hashes were reverified before reuse. Host remains
Debian 13.6 / Linux, GCC 14.2, Qt 6.8.2 Fusion; no DISPLAY/WAYLAND_DISPLAY, WSLg
mount/socket, native Windows executable path or cloud desktop capability exists.
No invasive display changes were made. Actual Qt widget tests/screenshots remain
**offscreen**, and genuine interactive Windows/WSLg desktop review remains
**NOT TESTED / BLOCKED**. Owner testing can begin on an available graphical
machine with a compatible development build and this helper.

| Newly executed check | Estimate | Actual result/runtime |
|---|---|---|
| Existing application identity checks | seconds | PASS, all three hashes unchanged |
| Optional widget-probe build | small inspection build | PASS, 4.531 s |
| Actual Qt Receive v2/v3 and Send v2/grouped/mixed, manual-conflict rejection | 2–4 min after seeding | 7 PASS observations across interrupted/resumed execution; not a clean aggregate run |
| Fresh owner controller restart/overwrite refusal/stop | 2–4 min seeding + about 1 min controls | PASS, 6.679 s controls after seeding |
| Affected native three-node case after maturity fix | 6–10 min with concurrent fixture | PASS, 382.971 s, all 26 checkpoints |
| Published-fixture one-file helper compatibility | 2–4 min seeding | PASS, real Qt/2 daemons, v2/v3 funds, normal stop |
| Activation guard unit smoke | sub-second check | PASS, 0.730 s |
| Final local lint/diff/scope | 1–3 min | PASS, final lint 15.708 s, 384 Python source files plus repository checks ([receipt](mercaweave-engineering-completion-artifacts/final-lint-receipt.json)); earlier lint 17.296 s |

The native Qt v2 send used one v2 input, 5407 signed bytes, fee 0.06 MCA. The
actual Qt grouped send consumed the two newly funded GUI-issued v3 outputs:
one anchor, one follower, 5520 signed bytes, fee 0.06 MCA. Both relayed and
confirmed independently on all three nodes. Automatic selected a manually
specified v2/v3 pair; incompatible explicit v3 mode produced a native conflict
message with no broadcast. Recipient v2 outputs did not change source v3
authorization. These are genuine Qt button/confirmation actions and native
wallet signing, not replacement RPC sends.

The environment reset interrupted the first driver after five completed checks
(broken output pipe). The resumed driver needed to reopen Send explicitly after
its previous transaction; that visibility assumption was corrected. Its restart
stage then timed out because the interrupted controller was gone. These failures
and successful transaction observations are retained separately. A new controller
on fresh data subsequently passed restart/stop and directory-refusal checks; it
preserved balances, authenticated records and tip while changing all three PIDs.
No private wallet directory was deleted or reused as fresh data.

The local owner-control helper extends only the published test's interactive hold
for restart control and safe shutdown. It preserves the original native seeding,
real PoW, common activation schedule and PQ topology. The optional Qt probe gains
ordinary Receive/confirmation/checkbox actions for verification. No production
source is changed by this task; no private signing or research bypass is added.
The helper remains a Linux/WSL development utility; native Windows desktop
operation is not claimed by this worker.

New unedited owner screenshots are under
`/workspace/Mercatura-MercaWeave-upgrade/doc/design/mercaweave-engineering-completion-artifacts/owner-qt/`:
`Owner-Receive-v2.png`, `Owner-Receive-v3.png`, `Owner-Inputs-mode-2.png`,
`Owner-Inputs-mode-0.png`, `Owner-Confirm-mode-1.png`, `Owner-Confirm-mode-2.png`,
`Owner-Confirm-mode-0.png`, and `Owner-Conflicting-input-rejected.png`.
[New owner observations](mercaweave-engineering-completion-artifacts/owner-gui-results.json),
[lifecycle receipt](mercaweave-engineering-completion-artifacts/owner-lifecycle-receipt.json),
[published-fixture compatibility](mercaweave-engineering-completion-artifacts/published-fixture-compatibility-result.json),
and [corrected native case](mercaweave-engineering-completion-artifacts/boundary-repaired-native-results.json)
identify what actually executed. [Binary identities](mercaweave-engineering-completion-artifacts/binary-identities.json)
record exact paths and hashes. The original fifteen and later 38 screenshots are
unchanged in their previous artifact directories.

The live fixture's private wallet databases, cookies and seeds are never committed.
Only public receipts, transaction IDs, screenshots, safe configuration summaries,
source/binary hashes and review evidence belong in handoff artifacts. A host reset
interrupted the local smoke driver/controller; completed Qt transaction evidence
is retained and remaining lifecycle checks use a fresh fixture. That interrupted
attempt is not described as a single clean full-suite run.

## Test-only boundary correction discovered during handoff

The second fresh fixture exposed a non-deterministic setup problem in the
published native test. At tip 110, automatic funding could select a coinbase
that becomes immature after the test rewinds to tip 108. The node then correctly
rejects the transaction as `bad-txns-premature-spend-of-coinbase`, preceding the
intended `mercaweave-inactive` assertion. This is a fixture/admission-priority
issue, not a consensus or native wallet security defect.

The minimal local repair selects an explicit native v2 coin with at least 103
confirmations before constructing that funding PSBT. It remains mature across
the two-tip rewind. The original exact inactive-v3 rejection assertion is kept;
no rejection alternative, skip or protocol change is introduced. The focused
full case passed all 26 checkpoints in 382.971 seconds. The owner helper applies
the same funding safeguard when used with the published `c938061` fixture, so
its single-file download works without a production or additional source patch.

The direct test correction is **local only**, commit `1ecc222faa195ef1d9478147f55d94ede7e0777a`. Public CI on `c938061` does not
verify it. A separately authorized publication and cross-platform rerun remain
necessary before treating the repaired test harness as frozen. No additional
push is performed by this task. This correction does not justify repeating
unchanged cryptographic/security campaigns or starting miner optimization.

## Final engineering freeze recommendation

**B — CONDITIONALLY COMPLETE** is the final readiness classification. The
production implementation at `c938061` is stable for owner regtest testing, with
actual Windows MSVC and macOS native Qt/unit/functional qualification. This task
changes no production source. Keep that production checkpoint stable while these
specific remaining gates are completed:

1. Obtain an unblocked Linux CI build/unit/functional result; the current job
   failed at dependency DNS before application testing.
2. Separately authorize publication of the local maturity-fixture correction and
   handoff changes, then verify the repaired harness cross-platform. The local
   corrected full three-node case already passes all 26 checkpoints.
3. Perform the owner's genuine interactive desktop walkthrough, including the
   native GUI lock/unlock/reload observations. This cloud worker supplies native
   offscreen evidence and a tested one-file control utility, not a WSLg desktop.

The owner **can begin manual isolated regtest testing** on an available graphical
machine using the compatible development build and linked helper. These are
specific verification gates, not a new implementation campaign. Independent
external security approval, public activation, release/merge approval and public
node deployment remain separate and unclaimed. No additional push is authorized;
all new changes are local. No miner optimization/integration is started. Preserve
vectors, all old reports/screenshots and the new evidence.


## Local inventory and delivery identity

Published production source remains `c93806131232f5df65958102892d29007c5ff773`.
Local test repair: `1ecc222faa195ef1d9478147f55d94ede7e0777a`. The separate local
handoff commit contains the single-file development utility, optional widget-probe
actions, this guide and public evidence. Neither new commit is pushed. The final
handoff commit SHA, clean working-tree check, source scope, fixture shutdown and
remote-ref verification are recorded in
`/workspace/Mercatura-MercaWeave-upgrade/work/engineering-completion/delivery-receipt.json`.
[Final integrity checks](mercaweave-engineering-completion-artifacts/final-integrity-check.json)
verify fixture shutdown and preserved sources/evidence.
No production `src/` file changed in this task; all 190 historical protected paths
are untouched, including preservation of the earlier known integration exception
in `src/mining/solo.cpp`. The original production/research worktree heads remain
unchanged. This is not independent security certification or public deployment
readiness, and no public activation height has been selected.
