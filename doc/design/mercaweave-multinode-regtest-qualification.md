# MercaWeave multi-node regtest and native Qt qualification

This is a pre-activation development checkpoint. Public MercaWeave activation is
**undecided and unset** on mainnet, public testnet, testnet4, signet and default
regtest. No public height, merge, tag, release, installer, public binary or
software deployment is authorized or performed here.

## Source, push and scope

Starting checkout: `/workspace/Mercatura-MercaWeave-upgrade`, branch
`integration/mercaweave-v0.1-upgrade`, clean at
`ca3b2d9dde296b4ebbd120212c243fcb066f1581`.

**PASS:** fetched remote integration head
`121e02949fb0473ab1314629acedaa3faca47054`; ancestry check returned zero remote-only
and two local-only commits. Those are the earlier research-test isolation
correction `a9867db` and the completed Send checkpoint `ca3b2d9`. The authorized
ordinary fast-forward push succeeded; Git's remote-ref read and GitHub's commit
API both confirmed exactly `ca3b2d9dde296b4ebbd120212c243fcb066f1581`. No force push,
rebase, merge, history replacement or unrelated branch update was used.

Only that existing checkpoint was pushed. All subsequent qualification tests,
screenshots, this report, and the Qt test portability repair remain **local**.
The delivered local qualification commit is identified separately from the
remote source in the delivery receipt. Production wallet, consensus, networking,
cryptography, mining and public-chain parameter source is unchanged in this task.

## Local delivery boundaries

The local checkpoint adds the three-node functional case, its native-runner
registration, the optional Linux Qt inspection probe, this report and its curated
public review artifacts. The only changed existing C++ file is
`src/qt/test/wallettests.cpp`, containing the exception-assertion portability fix.
These changes are unpublished. The final remote-ref check still returned exactly
`ca3b2d9dde296b4ebbd120212c243fcb066f1581`.

Production and frozen research checkouts remain clean at their original heads.
Of the historical 190 protected paths, 189 match the original archive and the
sole earlier integration exception is `src/mining/solo.cpp`; **this task changes
none of those 190 paths**. Recomputed daemon, CLI and actual Qt SHA-256 identities
still match the pushed-source applications. See
[final preservation check](mercaweave-multinode-regtest-artifacts/final-preservation-check.json).
`git diff --check` passed. The final full repository lint passed in 14.815 seconds;
its [execution receipt](mercaweave-multinode-regtest-artifacts/final-lint-receipt.json)
records the command and source state. Later edits only fill measured report
results and regenerate the JSON artifact manifest.

## Native capability audit

The old WO08 review's private-fixture limitation is historical evidence, not a
statement of the current integration capability. It remains preserved unchanged.
The current native `CWallet::PQGroupExecution` in `src/wallet/pqgroup.cpp` obtains
execution from actual chain parameters and active-tip height.
`CWallet::MercaWeaveAvailable` additionally requires native PQ wallet state and
private-key support. `GetNewPQGroupDestination` enforces capability and unlock;
normal native funding, signing, PSBT, mempool and block paths are used.

The new positive tests use native wallet-generated keys, actual mined/matured
coins, real ML-DSA-65 signatures and real independent node processes. They do not
set the guarded in-process research execution pointer, fabricate owned coins,
load fixture signing keys or enable research Qt backends. The existing native
`mercatura_mercaweave.py` connects two delayed peers and keeps its immediate third
node isolated; the new case explicitly connects **three peers with one schedule**.

The reviewed design sources are `mercaweave-upgrade-activation.md`,
`mercaweave-upgrade-wallet-compatibility.md`,
`mercaweave-upgrade-build-and-approval.md`, the frozen WO08 qualification, and
`mercaweave-send-spending-modes.md`. Existing native networking, grouped signing,
persistence and Qt tests were inspected and reused.

## Build and environment

| Item | Measured value |
|---|---|
| Execution host | Debian GNU/Linux 13.6, Linux 6.18.57; 2-CPU quota, 8 GiB memory |
| Compiler / CMake | GCC 14.2; CMake 4.4.4 |
| Qt / platform / style | Qt 6.8.2; `offscreen`; Fusion |
| Configuration | Existing native Release, wallet + GUI + unit/GUI tests; IPC, CUDA/OpenCL mining disabled |
| Build directory | `/workspace/Mercatura-MercaWeave-upgrade/work/build-native-release` |
| Native applications | `bin/mercaturad`, `bin/mercatura-cli`, `bin/mercatura-qt` under that build |
| Native tests | `bin/test_bitcoin`, `bin/test_bitcoin-qt`, mining/application/peer-security Qt executables |
| Functional runner | `work/build-native-release/test/functional/test_runner.py` |
| Build parallelism | `-j4` |
| Native functional parallelism | `--jobs=4`, timeout factor 3 |

The initial all-target build verification succeeded in approximately **5.906 s**
(measured from retained log creation/final-write timestamps); setup/build estimate
was 3–8 minutes. All original executables were verified against the previous
hash receipt before rebuilding. The application build then ran on clean pushed
source, before local qualification changes. Exact absolute paths, build receipt,
initial SHA-256 identities and the later repaired Qt-test identity are recorded
in [binary-identities.json](mercaweave-multinode-regtest-artifacts/binary-identities.json).
The daemon, CLI and actual Qt wallet hashes remain unchanged by the later
**test-only** rebuild.

**BLOCKED:** Ubuntu 22.04.6 WSL2 and a visible WSLg desktop are unavailable in this
executor. `DISPLAY` and `WAYLAND_DISPLAY` are unset. Cloud status reported a ready
executor with an empty desktop-capability list, and no computer-use/desktop tool
was offered. Environment ID:
`ccarenv_b64_Y2NhcmVudl8zNzkxNjE2NThkMzQ4MTkxOTIzZGY0YjQ0YTIyYmRmMQ`.
This is an actual native application rendered offscreen, **not** a WSLg desktop
inspection, a mock Send panel, or a claim about that unavailable platform.

## Three real processes and isolated activation

All connected nodes use `-regtest` from their new framework-generated configs and
one temporary `-mercaweaveheight=110` fixture. **110 is only this disposable test
schedule, not a proposed public activation height.** Default chain parameters
are unchanged. Every node uses `-pqp2p=required`, `-connect=0`, loopback P2P binding,
explicit loopback RPC binding/allowlist, discovery/seeds/onion discovery disabled,
and a separate cookie/datadir. The new case overrides the framework's unsafe
SQLite option with `-unsafesqlitesync=0` on every process.

Initial process identities in the completed GUI run:

| Node | Executable | PID | P2P port | RPC port | Disposable datadir |
|---|---|---:|---:|---:|---|
| 0 | mercatura-qt | 34737 | 13899 | 18899 | `/workspace/Mercatura-MercaWeave-upgrade/work/multinode-qualification/gui-three-node-06/node0` |
| 1 | mercaturad | 34759 | 13900 | 18900 | `/workspace/Mercatura-MercaWeave-upgrade/work/multinode-qualification/gui-three-node-06/node1` |
| 2 | mercaturad | 34772 | 13901 | 18901 | `/workspace/Mercatura-MercaWeave-upgrade/work/multinode-qualification/gui-three-node-06/node2` |

Connections form **0 ↔ 1 ↔ 2**, so node 2 learns a submitted transaction through
another relay. Every observed peer address is loopback, with
`pq_authenticated=true`, `pq_encrypted=true`, and transport `pq-v1`. Native block
mining, real coinbase maturity, common heights and best-block hashes are checked.
No daemon is launched against the Qt node's datadir. Restarts get new PIDs and
reconnect through the same supported P2P mechanism.

At active tip 109, candidate block 110 can admit the correctly signed native-v2
funding transaction with v3 outputs, while grouped wallet issuance remains
closed. At tip 108, its next block is inactive and all nodes reject that funding
with `mercaweave-inactive`; explicit grouped funding is unavailable and v2 funding
still works. Reconsideration restores the common protected chain. A second real
boundary reorg closes an already prepared native Qt Send confirmation, preserves
the selected v3 mode as unavailable, and broadcasts nothing.

## Measured native transactions and adversarial validation

**PASS:** native v2 funding of wallet-issued v3 addresses relays and confirms on
all nodes. Authenticated ownership is recognized by the sender/receiving wallets.
A genuine v3-only spend uses two same-key inputs with one anchor and one follower,
relays over both hops, and is mined by the independent validator. All three
accept the block. Native v2 transactions remain accepted after activation.
Automatic mixed v2/v3 signing, relay and confirmation also pass.

Explicit-mode cross-type manual inputs are rejected without substitution;
insufficient eligible grouped funds fail even while v2 funds remain. Actual
input scripts are checked independently of recipient type. Default native change
and existing PSBT paths are also covered by the focused spending-mode functional
case and native Qt wallet tests.

For comparable signed transactions with two inputs and two native outputs:

| Actual structure | Full serialized bytes | Funded fee |
|---|---:|---:|
| Two independent native v2 inputs | 10716 | 0.11 MCA |
| Two same-group v3 inputs, one anchor + one follower | 5520 | 0.06 MCA |
| Measured saving | 5196 bytes | 0.05 MCA |

The native fee-rate convention here is `Decimal("0.001")`, with `COIN=100`.
Both fees equal the actual full-byte calculation; no Bitcoin witness discount
or per-member full-signature multiplication is used. Coin Control previews show
an approximation marker; the real Send confirmation uses the funded fee.
The final headless native case additionally warms each validator with a valid
admission before checking mutated witness/body forms.

The following reasons are asserted exactly on **all three mempools and all three
independent block submissions**, using actual current coins and native PoW. The final automated test uses distinct
coinbase extranonces/headers per validator so prior P2P rejection cannot replace
the original RPC reason with `duplicate-invalid`:

| Invalid form | Mempool reason | Block reason |
|---|---|---|
| Signature mutation | `pqgroup-authorization-14` | `pqgroup-authorization-14` |
| Wrong grouped public key or commitment nonce | `pqgroup-authorization-13` | `pqgroup-authorization-13` |
| Self/future follower role, reordered roles, duplicate key anchor | `bad-txns-too-many-sigops` | `bad-blk-sigops` |
| Missing required witness | `bad-witness-nonstandard` | `bad-blk-sigops` |
| Changed signed transaction body | `pqgroup-authorization-14` | `pqgroup-authorization-14` |
| Foreign-genesis output commitment | `pqgroup-authorization-13` | `pqgroup-authorization-13` |

Malformed structure gets the existing fail-closed work charge before cryptographic
authorization; missing witness fails policy even earlier. Neither is mislabeled
as a signature failure. Wrong live amount and script PSBT snapshots are separately
rejected by the wallet with `Inputs missing or spent` before signing.

The foreign-domain negative reuses only the locked public commitment oracle in
`src/test/mercaweave/reference.py`. It first matches the genuine native commitment,
then funds a deliberately wrong-domain program using real v2 coins. Its adversarial
witness must fail genesis-bound ownership before a borrowed signature can be
considered. It is negative real-node evidence, not a positive fixture signer or a
claim of valid foreign-network custody. No chain's genesis or parameters change.

## Reorg, restart, persistence and resources

**PASS:** receiving-wallet process restart after grouped confirmation; validator
restart with a genuine pending grouped transaction retained; wallet unload/reload;
rescan of authenticated known ownership ranges; actual reindex and level-4 replay;
competing branches; a disconnected grouped transaction returning to the mempool;
reconfirmation; spent-input PSBT rejection; and wallet UTXO/balance reconciliation.
All final nodes independently run `verifychain(4, 0)` and agree at height **120**:
`8679c518284cc0853ff47e9480c66c33486b17943688f85efe49cab107464ded`.

The three-node case is not an operator backup/disaster-recovery certification.
Existing native backup/restore and recovery tests run separately in the native
functional checkpoints; their scope must not be confused with this scenario.

The resource monitor retained 179 samples at two-second intervals and process
high-water marks across planned restarts. Maximum observed RSS was **507804 KiB**
for Qt and **451492 KiB** for a daemon. Processes stopped cleanly; no crash,
assertion, deadlock, unplanned public connection, unresolved synchronization stall
or final mempool inconsistency was observed. Invalid-admission/block errors and
planned restart/split disconnections are expected test events. These bounded
observations do not constitute a long-running leak or physical-GPU qualification.

All 29 named checkpoints from the completed GUI/three-node run are retained in
[results.json](mercaweave-multinode-regtest-artifacts/results.json), including
public transaction IDs, exact safe node arguments/configuration and common tips.
The full per-checkpoint names are listed at the end of this report.

## Native Send placement and screenshots

**PASS (offscreen):** the native Send dropdown is the first row directly below
the main navigation toolbar, above Pay To. The Spending mode label is on the
left, the dropdown next, Inputs immediately to its right, and availability text
at the right. It is visible with advanced Coin Control disabled. Automatic is the
default; the popup displays all three complete labels. At 1000×800 and 850×650,
no dropdown clipping, recipient overlap or fee-control displacement was observed.
The actual Qt Fusion theme is retained. Supported/unsupported wallet switching,
manual type restrictions, fee-control expansion, grouped preview, and actual
confirmation invalidation were exercised through native widgets.

**Visual finding:** at Coin Control's default 1000×500 width, the newly added
rightmost input-type column is partly outside the viewport. Full labels are
visible with its existing horizontal scrollbar. Recognition/filtering/manual
restriction work, but simultaneous full labels without scrolling **FAIL** that
usability check. No dropdown redesign or further production UI change was made;
placement and this column-width finding are left for user review.

A fresh regtest estimator legitimately reports insufficient estimation history;
the existing fallback/custom fee controls and actual funded confirmation remain
functional. The inherited recipient placeholder still contains a Bitcoin-style
example; that wording was not changed in this qualification task.

Screenshots are unedited native Qt widget grabs. Popup and confirmation images
capture those actual separate Qt windows; they are not composited mockups.
All exact source paths are under
`/workspace/Mercatura-MercaWeave-upgrade/work/multinode-qualification/screenshots/`.
Lossless review copies are under
`/workspace/Mercatura-MercaWeave-upgrade/doc/design/mercaweave-multinode-regtest-artifacts/`:

| View | Screenshot |
|---|---|
| A: whole Send, 1000×800 | [A-send-inactive-1000x800.png](mercaweave-multinode-regtest-artifacts/A-send-inactive-1000x800.png) |
| A: whole Send, 850×650 | [A-send-inactive-850x650.png](mercaweave-multinode-regtest-artifacts/A-send-inactive-850x650.png) |
| B: expanded inactive dropdown | [B-dropdown-expanded-inactive.png](mercaweave-multinode-regtest-artifacts/B-dropdown-expanded-inactive.png) |
| B: expanded activated dropdown | [B-dropdown-expanded-active.png](mercaweave-multinode-regtest-artifacts/B-dropdown-expanded-active.png) |
| C: Coin Control initial viewport | [C-coin-control-all-types.png](mercaweave-multinode-regtest-artifacts/C-coin-control-all-types.png) |
| C: full type labels after horizontal scroll | [C-coin-control-types-scrolled.png](mercaweave-multinode-regtest-artifacts/C-coin-control-types-scrolled.png) |
| C: grouped manual selection | [C-coin-control-v3-selection.png](mercaweave-multinode-regtest-artifacts/C-coin-control-v3-selection.png) |
| C: grouped selection, full type labels | [C-coin-control-v3-selection-scrolled.png](mercaweave-multinode-regtest-artifacts/C-coin-control-v3-selection-scrolled.png) |
| D: active Automatic | [D-send-active-automatic.png](mercaweave-multinode-regtest-artifacts/D-send-active-automatic.png) |
| D: active grouped mode | [D-send-active-grouped.png](mercaweave-multinode-regtest-artifacts/D-send-active-grouped.png) |
| D: native fee controls expanded | [D-send-active-fee-controls.png](mercaweave-multinode-regtest-artifacts/D-send-active-fee-controls.png) |
| E: unsupported wallet | [E-unsupported-wallet.png](mercaweave-multinode-regtest-artifacts/E-unsupported-wallet.png) |
| F: genuine grouped Send confirmation | [F-grouped-send-confirmation.png](mercaweave-multinode-regtest-artifacts/F-grouped-send-confirmation.png) |
| F: confirmation invalidated by real boundary reorg | [F-stale-confirmation-invalidated.png](mercaweave-multinode-regtest-artifacts/F-stale-confirmation-invalidated.png) |
| G: default regtest, no activation argument | [G-default-regtest-no-activation.png](mercaweave-multinode-regtest-artifacts/G-default-regtest-no-activation.png) |

Widget visibility, geometry, enabled items, text widths, platform/style and
native Coin Control rows are preserved in
[qt-widget-inspection.json](mercaweave-multinode-regtest-artifacts/qt-widget-inspection.json).

## Completed GitHub Actions results

Exact pushed source: `ca3b2d9dde296b4ebbd120212c243fcb066f1581`.
[Main CI 42363956006](https://github.com/Mercatura-Core/Mercatura-Core/actions/runs/42363956006): **PASS**.
[Qt qualification 42363955984](https://github.com/Mercatura-Core/Mercatura-Core/actions/runs/42363955984): **FAIL**.
These are completed observed results, not inferred from an earlier commit.

| Job | Result | Unit tests | Unit seconds | Native functionals | Runner seconds |
|---|---|---|---:|---|---:|
| Linux native Qt | PASS | 194 pass, 1 skip, 0 fail | 1560.00 | 58/58 pass | 878 |
| macOS native Qt | PASS | 190 pass, 3 skips, 0 fail | 1908.55 | 58/58 pass | 1151 |
| macOS native headless | PASS | 187 pass, 1 skip, 0 fail | 2125.96 | 58/58 pass | 1116 |
| Windows native headless | PASS | 187 pass, 1 skip, 0 fail | 1167.65 | 58/58 pass | 758 |
| Windows native Qt | FAIL | NOT TESTED: build failed | — | NOT TESTED: build failed | — |

Lint and meta jobs passed. Cross-packaging was deliberately skipped for this
integration branch and is not counted as a build pass. `script_assets_tests`
skips inherited classical-signature semantics. The two additional macOS mining
application skips reflect existing macOS application-close lifecycle behavior.

Windows Qt failed with MSVC **C4996** at `src/qt/test/wallettests.cpp:634`:
`QVERIFY_EXCEPTION_THROWN` is deprecated. A local typed `std::runtime_error`
try/catch plus `QVERIFY` now preserves the same assertion without that deprecated
Qt macro. The repair builds and passes the native Linux Qt executable.
**NOT TESTED:** the repaired source under native MSVC; **BLOCKED:** a new remote
Windows Qt qualification until separately authorized changes can be published.
The failed pushed workflow remains failed; no follow-up changes were pushed.
See [CI summary](mercaweave-multinode-regtest-artifacts/ci-summary.json) and the
[exact compile error](mercaweave-multinode-regtest-artifacts/windows-qt-ci-error.txt).

The completed exact-source CI unit checkpoints serve the full-unit gate here.
The only subsequent C++ change is the local test assertion repair, validated by
its complete Qt executable; unchanged unit binaries were not redundantly rerun.
The new three-node test and later local changes are not present in remote CI.

## Local tests, runtime estimates and retained failures

| Check | Estimate given before execution | Actual | Result |
|---|---|---:|---|
| Native app build verification and inspection setup | 3–8 min | app build ≈5.906 s | PASS |
| Final actual Qt + three-node qualification | 5–8 min | 355.233 s | PASS, 29 named checkpoints |
| Test-only Qt rebuild | 1–3 min | 18.817 s | PASS |
| Repaired full Qt executable | included in 6–12 min focused round | 221.297 s | PASS |
| Focused spend/coin-selection/PSBT/activation suites | included in focused round | 83.154 s | 4/4 PASS |
| Existing native MercaWeave / spending-mode / three-node cases | included in focused round | 292.122 s including setup | 3/3 PASS |
| Safe interactive launch and marker shutdown | 1–3 min | 4.237 s | PASS, all three owned PIDs stopped |
| Default-regtest Qt/CLI launch, no activation argument | 1–3 min | 6.594 s | PASS, grouped issuance and dropdown unavailable |
| Complete native functional batch, 59 selected | 25–40 min | 1884.606 s driver / 1770 s runner | FAIL: 58 PASS, 1 harness failure |
| Repaired new three-node case, targeted rerun | 6–10 min | 340.134 s driver / 340 s runner | PASS: 1 case, 26 named checkpoints |
| Final repository lint | 1–3 min | 14.815 s | PASS, 383 Python source files plus repository checks |

The focused functional runner reported 207 s active execution: native MercaWeave
207 s, spending modes 110 s, and ordinary three-node PQ 156 s concurrently.
Only the native default suite runs, never the entire inherited Bitcoin functional
suite. Local defaults remain build `-j4` and functional `--jobs=4`.

Early runs are retained, not counted as complete passes: attempt 01 failed its
premature GUI visibility assertion (3.014 s); diagnostic attempt 02 was stopped
at the genuine genesis initial-sync overlay (92.435 s); attempt 03 expected a
cryptographic error where malformed structure first triggers work limits
(96.679 s); attempt 04 expected a work-limit error where missing witness first
fails standardness (101.680 s). Source inspection established the exact earlier
failure paths; assertions were corrected to those specific reasons, not relaxed
to arbitrary rejection. Attempt 05 passed before the final domain case was added
(318.872 s); attempt 06 is the completed GUI run reported here. All attempts,
private datadirs and raw logs remain under `work/multinode-qualification/`.
An initial full-functional attempt then exposed a missing CMake build-directory
link for the new script. Its launch failure was retained, the remaining children
of only that attempt were stopped, CMake refreshed the staging links, and the
complete native checkpoint was restarted. That interrupted batch is not a
passing full-suite result.
The restarted batch also exposed a parallel-run harness race: connected peers
could reject the same invalid block over P2P before its explicit RPC submission,
returning `duplicate-invalid`. The final new case uses distinct valid-PoW
coinbase extranonces/headers per validator while preserving the exact invalid
transaction and expected reason. All other **58 native cases passed** in the
1884.606-second batch. Its raw status remains **FAIL (58 PASS / 1 FAIL)**. The
repaired case then passed separately in **340.134 seconds**, with all **26 named
headless checkpoints** passing, final height 120 and common hash
`89f7642722e3e8e0cc2719cf103eac27e897611858f47a27d0d617fcffb2b665`.
Thus **all 59 unique native cases passed across these invocations**, with zero
final unresolved test failures; this is not a clean single 59-case run.
[Raw batch summary](mercaweave-multinode-regtest-artifacts/native-batch-summary.json),
[final coverage summary](mercaweave-multinode-regtest-artifacts/native-final-summary.json),
and [repaired checkpoint results](mercaweave-multinode-regtest-artifacts/repaired-native-results.json)
retain the distinction. The targeted run was **7 November 2026,
05:18:32–05:24:12 PST**.

Two intermediate helper attempts are also retained: the first failed at 92.632 seconds
because the inherited `CScript` does not implement byte concatenation; explicit
`bytes(...)` fixed that test construction. A serialization preflight then caught
an obsolete `rehash()` call absent from this framework, whose txid properties
recompute from current contents. Only that attempt's own runner and child were
stopped after 69.688 seconds. Removing the call passed a three-distinct-Merkle-root preflight before
the successful final test. Neither intermediate attempt counts as a pass;
no application or consensus source changed for these harness corrections.

The final functional run also checks valid-admission cache warmup before negative
mutations. Application binaries remain the pushed-source binaries throughout;
local test/probe revisions have their own source hashes in execution receipts.
Dates in narrative use America/Los_Angeles: the final GUI run was **7 November
2026, 02:56:58–03:02:53 PST**. Raw receipts retain explicit UTC timestamps for
machine reproducibility. `manifest.json` records review-artifact hashes.

## Reproduce and inspect safely

These commands use this local qualification checkout, which contains the new
unpublished test/probe. A fresh remote clone at `ca3b2d9` does not yet include
those later local files. The existing verified executables can be located with:

```bash
cd /workspace/Mercatura-MercaWeave-upgrade
test -x work/build-native-release/bin/mercatura-qt
test -x work/build-native-release/bin/mercaturad
test -x work/build-native-release/bin/mercatura-cli
sha256sum work/build-native-release/bin/mercatura-qt
```

For the current Linux toolchain, a rebuild uses the existing configuration:

```bash
/workspace/Mercatura-MercaWeave-v0.1/work/tools/cmake/data/bin/cmake \
  --build /workspace/Mercatura-MercaWeave-upgrade/work/build-native-release -j4
```

Record new binary identities after any rebuild. The initial pushed-source hashes
in this report identify the actual applications tested, not every future rebuild.

For a visible X11/WSLg session **where DISPLAY and the Qt xcb runtime are actually
available**, run the following from one terminal. This platform path is provided
for reproduction and remains untested on this host. The tested lifecycle uses the
same command with `--qt-platform=offscreen` instead:

```bash
cd /workspace/Mercatura-MercaWeave-upgrade
MCA_FIXTURE_ROOT=$(mktemp -d /tmp/mca-visual.XXXXXXXX)
printf 'Disposable root: %s\n' "$MCA_FIXTURE_ROOT"
printf 'Stop command for another terminal: touch %q\n' "$MCA_FIXTURE_ROOT/cluster/STOP-ONLY-THIS-FIXTURE"
python3 test/functional/mercatura_mercaweave_multinode.py \
  --configfile="$PWD/work/build-native-release/test/config.ini" \
  --tmpdir="$MCA_FIXTURE_ROOT/cluster" --nocleanup --timeout-factor=3 \
  --qt-binary="$PWD/work/build-native-release/bin/mercatura-qt" \
  --qt-platform=xcb --interactive=inactive
if [ "$?" -eq 0 ]; then touch "$MCA_FIXTURE_ROOT/ALL-OWNED-NODES-STOPPED"; fi
```

The parent is newly allocated; the `cluster` child must not already exist. Each
node gets its own wallet/datadir/ports/cookie. The launcher creates/loads sender,
receiver and observer test wallets. Node 0 mines one actual current block so the
normal initial-sync guard can finish. Click **Send** in the native toolbar, find
Spending mode directly above Pay To, open its popup, switch Automatic/Standard PQ,
and click **Inputs…**. Grouped mode is disabled in this inactive delayed fixture.
No research panel or separate Send wizard is involved. The complete default-no-activation Qt/CLI launch was also executed offscreen: normal v2 wallet creation and a real first block worked, native grouped issuance was rejected, and the grouped dropdown item was disabled. Its exact safe arguments, ports, CLI path and clean shutdown are in [default-qt-inspection-receipt.json](mercaweave-multinode-regtest-artifacts/default-qt-inspection-receipt.json).

In a second terminal, paste the exact `touch` command printed before launch.
It creates that newly allocated cluster's `STOP-ONLY-THIS-FIXTURE` file; this
signals only its own three nodes to shut down through normal RPC. It does not search for or kill unrelated processes. When the first terminal
returns successfully, it marks that newly created root as stopped.

For an activated visual fixture, repeat in a **new** `mktemp` parent, changing
only `--interactive=inactive` to `--interactive=activated`. This mines to the
explicit temporary height, issues/funds real v3 addresses, then holds for manual
inspection. Select wallet sender, Send, MercaWeave mode and Inputs; both types are
available. Use Coin Control's horizontal scrollbar for complete type labels.
Interactive modes are inspection demos, not a substitute for the full automated
qualification.

After a successful shutdown, cleanup is restricted to the newly allocated root:

```bash
case "$MCA_FIXTURE_ROOT" in
  /tmp/mca-visual.*)
    if [ -f "$MCA_FIXTURE_ROOT/ALL-OWNED-NODES-STOPPED" ] &&
       [ -f "$MCA_FIXTURE_ROOT/cluster/.mercaweave-qualification-fixture" ]; then
      rm -r -- "$MCA_FIXTURE_ROOT"
    fi
    ;;
esac
```

No existing path is removed by launch or automatic test teardown with
`--nocleanup`. Never substitute an installed mainnet/testnet directory. To check
unset **default** regtest separately, launch the actual Qt executable in another
fresh datadir with `-regtest -server=1 -connect=0 -discover=0 -dnsseed=0`, loopback
bindings and **no `-mercaweaveheight` argument**; that is distinct from the delayed
fixture above. The default/public unset schedules are also verified by native
activation unit tests.

The automated reproduction and probe compiler invocation are preserved exactly
in the JSON execution/build receipts under `work/multinode-qualification/`.
The probe is Linux test glue using ordinary Qt widget actions; it is never linked
into, installed with, or enabled as a production wallet/signing backend.

## Findings and next checkpoint

- **PASS:** authorized exact push; genuine native three-node grouped relay and
  consensus; mixed/v2 sending; activation boundaries; persistence/replay/reorg;
  actual native offscreen Send/Coin Control inspection and screenshots.
- **FAIL:** pushed Windows Qt CI compilation; local test-only repair is retained.
- **FAIL (visual usability):** full Coin Control input-type text without scrolling
  at the default width. The existing scrollbar exposes it correctly; no UI redesign
  was performed before user placement review.
- **BLOCKED / NOT TESTED:** actual Ubuntu 22.04.6 WSL2/WSLg desktop inspection and
  native MSVC requalification of the unpublished test repair.
- No new consensus, native ownership, wallet signing, networking or PSBT defect
  was demonstrated in the executed scenarios. This is not a release or public
  activation authorization and makes no physical-GPU/live-network qualification.

Recommended next pre-activation checkpoint: review these captured native layouts
and the column-width finding; inspect the same wallet under actual WSLg; and
separately approve publication/requalification of the local test portability fix
and qualification changes if desired. Keep all public schedules unset. No further
push, merge, tag, release or deployment is performed by this work order.

## Executed named GUI / three-node checkpoints

| Result | Checkpoint |
|---|---|
| PASS | `three-independent-loopback-PQ-processes` |
| PASS | `actual-native-Qt-inactive-Send` |
| PASS | `preactivation-and-real-reward-maturity` |
| PASS | `candidate-height-H-versus-active-tip-H-minus-one` |
| PASS | `activation-boundary-reorg-and-v2-preservation` |
| PASS | `native-v2-funding-of-v3-output-relay-and-block` |
| PASS | `actual-native-Qt-Coin-Control-and-wallet-switch` |
| PASS | `actual-native-pending-Send-invalidated-by-activation-reorg` |
| PASS | `explicit-policies-manual-conflicts-output-independence-and-real-fees` |
| PASS | `wrong-actual-prevout-snapshot-rejected-before-signing` |
| PASS | `independent-mempool-and-block-rejection-invalid-signature` |
| PASS | `independent-mempool-and-block-rejection-incorrect-group-public-key` |
| PASS | `independent-mempool-and-block-rejection-incorrect-output-commitment-nonce` |
| PASS | `independent-mempool-and-block-rejection-self-follower-role` |
| PASS | `independent-mempool-and-block-rejection-missing-required-witness` |
| PASS | `independent-mempool-and-block-rejection-changed-signed-transaction-commitment` |
| PASS | `independent-mempool-and-block-rejection-reordered-input-roles` |
| PASS | `independent-mempool-and-block-rejection-duplicate-group-key-anchor` |
| PASS | `real-grouped-two-hop-P2P-relay-and-independent-mined-block` |
| PASS | `existing-v2-authorization-accepted-after-activation` |
| PASS | `receiving-wallet-process-restart-after-grouped-confirmation` |
| PASS | `foreign-genesis-output-commitment-rejected-by-all-validators` |
| PASS | `real-Automatic-mixed-v2-v3-relay-and-confirmation` |
| PASS | `validator-process-restart-and-grouped-mempool-persistence` |
| PASS | `wallet-reload-rescan-authenticated-known-ranges` |
| PASS | `grouped-history-reindex-and-level4-replay` |
| PASS | `real-competing-chain-reorg-and-disconnected-grouped-mempool-return` |
| PASS | `reconfirmation-stale-PSBT-rejection-and-coin-reconciliation` |
| PASS | `final-independent-validation-and-PQ-network-agreement` |
