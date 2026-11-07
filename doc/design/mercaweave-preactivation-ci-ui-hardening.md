# MercaWeave pre-activation CI and Qt hardening

This local checkpoint repairs Coin Control presentation and prepares the already
implemented Windows test repair for independent MSVC verification. Public
`nMercaWeaveHeight` values remain unset; no push, merge, tag, release, installer,
public binary or deployment is performed.

## Exact source and Windows failure audit

Starting branch: `integration/mercaweave-v0.1-upgrade`, clean at
`2d79c330f5ac246156d758e34763bbba9ff45524`. Both configured remotes identify
`Mercatura-Core/Mercatura-Core`. A fresh remote-ref read confirmed the last pushed
commit is still `ca3b2d9dde296b4ebbd120212c243fcb066f1581`.

The preceding local commit changed 45 files: a three-node functional case and
runner registration, an optional Linux widget-inspection probe, the qualification
report and 40 review artifacts, plus the test-only exception assertion in
`src/qt/test/wallettests.cpp`. Its native runtime source was unchanged. Its
negative-block repair gives each validator a different coinbase/header so P2P
cannot cause an earlier cached `duplicate-invalid` reply to obscure the exact
rejection reason. The probe is optional Linux inspection glue; ordinary native
functional execution does not load it. No unexpected uncommitted work existed.

The completed pushed-source workflows remain historical evidence:

- [Standard CI 42363956006](https://github.com/Mercatura-Core/Mercatura-Core/actions/runs/42363956006): **PASS**.
- [Dedicated qualification 42363955984](https://github.com/Mercatura-Core/Mercatura-Core/actions/runs/42363955984): **FAIL**, Windows Qt compilation.

The failed job's decoded logs were fetched again and reviewed, not inferred from
its workflow label. It used **MSVC 19.51.36260.0**, Visual Studio 18, and vcpkg
**Qt 6.9.1**. The failing step ran:

```text
cmake --build work/build --config Release --parallel 4
src/qt/test/wallettests.cpp(634,5): error C4996:
'QTest::useVerifyThrowsException': Don't use QVERIFY_EXCEPTION_THROWN(expr, type)
anymore, use QVERIFY_THROWS_EXCEPTION(type, expr...) instead
```

The error came from `test_bitcoin-qt.vcxproj`. This was a compiler failure;
Windows Qt runtime/unit/functional qualification never started. General
warnings-as-errors was **OFF**, while the compiler flags included **`/sdl`**, which
makes this MSVC deprecation diagnostic fatal. It was not an exhausted worker,
linker failure, MOC failure, missing dependency, or runtime test failure.

Ordinary Windows CI requests `wallet;tests` with `BUILD_GUI=OFF`; the dedicated
job requests `qt;wallet;tests` and enables `BUILD_GUI`, `BUILD_GUI_TESTS`, and
`BUILD_TESTS`. Thus ordinary Windows success did not compile this Qt assertion.
Linux/macOS dedicated jobs also enable Qt but use their native compilers and
package dependencies. No matrix, CMake flag, warning suppression, assertion or
qualification case is disabled or weakened here.

**Repair already present in `2d79c33`:** a typed `std::runtime_error` try/catch
followed by `QVERIFY` preserves the malformed mining-reward exception check and
removes the deprecated macro. No additional Windows-source repair was needed.
The standard C++ code has no Windows path, quoting, timing or compiler extension
assumption. The exact diagnosed macro call is gone. That establishes source
repair of the observed cause, not successful execution under MSVC.

**PENDING / BLOCKED:** this Debian executor has neither an actual Windows MSVC
worker nor a native Windows graphical desktop. `cl.exe` and `clang-cl` are absent.
No macOS worker is attached. New Windows CI cannot verify unpublished commits
without a separately authorized push. No push or rerun of old unrepaired source
was attempted. Historical Linux/macOS success is not claimed for this new source.

## Production Coin Control change

Only `src/qt/coincontroldialog.cpp` changes production behavior, and only its
presentation:

- Replace fixed column widths with Qt content sizing for checkbox, Amount,
  Input type, Date and Confirmations.
- Put Input type immediately after Amount by moving its **visual** section;
  logical column indices and saved sorting remain unchanged.
- Give Address the remaining width. Label starts at a font-derived width and
  remains resizable. Full addresses remain available through tooltips and the
  existing copy/outpoint actions.
- Keep the complete concise `Standard PQ (v2)` and `MercaWeave (v3)` labels;
  add explanatory tooltips and retain unavailable-mode guidance.

The existing **1000×500** Coin Selection dialog is retained. No minimum-window
increase or replacement dialog is introduced. Checkboxes, amount, label, address,
date, confirmations, transaction identifiers/outpoint access, sorting, tree/list
views, filters, input eligibility and fee calculations remain in the existing
implementation. The Send dropdown, its wording and location are unchanged:
first row below the main navigation toolbar, above Pay To, with Inputs beside it.
Automatic remains the initial selection.

The previous width/scroll finding and all fifteen original screenshots remain
unchanged in [the multi-node report](mercaweave-multinode-regtest-qualification.md).
They are evidence of the prior layout, not rewritten as successful new results.

## Actual Qt inspection and screenshots

Environment: Debian 13.6 / Linux 6.18.57, GCC 14.2, CMake 4.4.4, Qt **6.8.2**,
Fusion style, **offscreen**. `DISPLAY` and `WAYLAND_DISPLAY` remain unset; cloud
capabilities remain empty. These are native widget grabs from the actual rebuilt
`work/build-native-release/bin/mercatura-qt`, not desktop inspection or mock UI.

The existing three-node fixture and optional probe are reused. Each visual run
starts one Qt process and two daemons, all loopback-bound, with fresh private
regtest datadirs, normal SQLite syncing, native PQ transport and the same
explicit disposable `-mercaweaveheight=110`. Native mining supplies real mature
coins and both real wallet input types. The temporary height is not a public
activation proposal. The inspection-only interactive mode stops after GUI checks
and is **not** counted as full network/consensus qualification.

Matrix sizes are Qt logical pixels, with image dimensions scaled by device-pixel
ratio; these are not physical desktop captures. Main windows **1280×800**, **1366×768**, **1920×1080**, with Coin Control
held at **1000×500**, both list and expanded tree views. Repeat at actual Qt
`QT_SCALE_FACTOR=1.0` and `1.25`; record widget device-pixel ratio. At each size,
assert that the complete type section lies inside the viewport, every type label
fits its section, both native types are populated, and horizontal scroll is zero.
The Qt regression separately increases font metrics by 25% and substitutes longer
labels; this is a stress check, not certification of every translation.

**PASS (offscreen):** all twelve combinations (three main sizes × two views ×
two device-pixel ratios) expose both complete type labels with horizontal scroll
zero. The type section is x=111…213 in list view and x=134…236 in tree view,
inside the 976-logical-pixel viewport. Recorded device-pixel ratios are 1.0 and
1.25, respectively. Native screenshots were visually examined for list/tree and
Send placement. No type clipping or control overlap was observed. Long addresses
can elide within their allocated space; full addresses remain accessible by
existing context actions and the new tooltips. WSLg and native Windows desktop
visual review remain **NOT TESTED / BLOCKED**.

Before: [original clipped Coin Control](mercaweave-multinode-regtest-artifacts/C-coin-control-all-types.png).
New unedited screenshots are in
`/workspace/Mercatura-MercaWeave-upgrade/doc/design/mercaweave-preactivation-ci-ui-hardening-artifacts/`.
There are 18 per scale plus two final confirmation views (38 new screenshots total); originals also remain under
`work/preactivation-hardening/visual-1_0-01-screenshots/` and
`work/preactivation-hardening/visual-1_25-01-screenshots/`.

| View | 100% | 125% |
|---|---|---|
| Full Send, 1280×800 | [Send](mercaweave-preactivation-ci-ui-hardening-artifacts/100-percent/H-send-1280x800.png) | [Send](mercaweave-preactivation-ci-ui-hardening-artifacts/125-percent/H-send-1280x800.png) |
| Expanded dropdown, 1280×800 | [Dropdown](mercaweave-preactivation-ci-ui-hardening-artifacts/100-percent/H-dropdown-1280x800.png) | [Dropdown](mercaweave-preactivation-ci-ui-hardening-artifacts/125-percent/H-dropdown-1280x800.png) |
| List, 1280×800 main / 1000×500 dialog | [List](mercaweave-preactivation-ci-ui-hardening-artifacts/100-percent/H-coins-list-1280x800.png) | [List](mercaweave-preactivation-ci-ui-hardening-artifacts/125-percent/H-coins-list-1280x800.png) |
| Tree, same dimensions | [Tree](mercaweave-preactivation-ci-ui-hardening-artifacts/100-percent/H-coins-tree-1280x800.png) | [Tree](mercaweave-preactivation-ci-ui-hardening-artifacts/125-percent/H-coins-tree-1280x800.png) |
| Full Send, 1366×768 | [Send](mercaweave-preactivation-ci-ui-hardening-artifacts/100-percent/H-send-1366x768.png) | [Send](mercaweave-preactivation-ci-ui-hardening-artifacts/125-percent/H-send-1366x768.png) |
| List, 1366×768 main | [List](mercaweave-preactivation-ci-ui-hardening-artifacts/100-percent/H-coins-list-1366x768.png) | [List](mercaweave-preactivation-ci-ui-hardening-artifacts/125-percent/H-coins-list-1366x768.png) |
| Tree, 1366×768 main | [Tree](mercaweave-preactivation-ci-ui-hardening-artifacts/100-percent/H-coins-tree-1366x768.png) | [Tree](mercaweave-preactivation-ci-ui-hardening-artifacts/125-percent/H-coins-tree-1366x768.png) |
| Full Send, 1920×1080 | [Send](mercaweave-preactivation-ci-ui-hardening-artifacts/100-percent/H-send-1920x1080.png) | [Send](mercaweave-preactivation-ci-ui-hardening-artifacts/125-percent/H-send-1920x1080.png) |
| List, 1920×1080 main | [List](mercaweave-preactivation-ci-ui-hardening-artifacts/100-percent/H-coins-list-1920x1080.png) | [List](mercaweave-preactivation-ci-ui-hardening-artifacts/125-percent/H-coins-list-1920x1080.png) |
| Tree, 1920×1080 main | [Tree](mercaweave-preactivation-ci-ui-hardening-artifacts/100-percent/H-coins-tree-1920x1080.png) | [Tree](mercaweave-preactivation-ci-ui-hardening-artifacts/125-percent/H-coins-tree-1920x1080.png) |

The final full Qt-backed follow-up also retains the actual
[grouped confirmation](mercaweave-preactivation-ci-ui-hardening-artifacts/full-gui-125-percent/F-grouped-send-confirmation.png)
and its [invalidation after a real activation reorg](mercaweave-preactivation-ci-ui-hardening-artifacts/full-gui-125-percent/F-stale-confirmation-invalidated.png).
All **30 checkpoints passed**, including restoration of MercaWeave mode after the
matrix, and independent final validation at height 120, hash
`eafdf7d1885e881a3028d7d884237e023a31eb3c2c58a1517e855b34c8ad6666`.
[Full Qt-backed results](mercaweave-preactivation-ci-ui-hardening-artifacts/full-gui-125-results.json)
and the [execution receipt](mercaweave-preactivation-ci-ui-hardening-artifacts/full-gui-125-receipt.json)
record that final source. Its actual runtime exceeded the launch estimate while
sharing the two-CPU worker with the native suite; no timeout or failure occurred.

The complete screenshot inventory and SHA-256 hashes are in the artifact
manifest. Both scales also retain inactive/activated dropdown, selected grouped
inputs, expanded fee controls and unsupported-wallet screenshots.
[100% measurements](mercaweave-preactivation-ci-ui-hardening-artifacts/visual-1_0-results.json)
and [125% measurements](mercaweave-preactivation-ci-ui-hardening-artifacts/visual-1_25-results.json)
retain actual node identities, safe arguments and widget geometry.

The fixture also exercises inactive/active dropdown gating, Inputs, v2 selection,
v3 restrictions, incompatible selections retained for explicit deselection,
grouped fee preview, expanded fee controls, unsupported-wallet switching and
reopening dialogs. No research sending panel is used. A stop marker shuts down
only each fresh fixture's own nodes through normal framework teardown.

## Regression results and failure history

Original historical execution: full native batch **58 PASS / 1 FAIL** due to the
new test's P2P/RPC rejection-cache race. After its test-only header repair,
a targeted rerun **PASS**ed all 26 headless checkpoints. The preceding report's
59-case coverage combined those invocations and explicitly did not call them a
single clean pass. Those receipts remain unchanged.

**PASS:** this checkpoint completed its own **single clean 59-case run**, with
**59 PASS / 0 FAIL / 0 SKIP**, exit 0. It does not combine the historical failed
batch, repaired targeted rerun, or the separate GUI follow-up.
[Every case and runtime](mercaweave-preactivation-ci-ui-hardening-artifacts/native-clean-summary.json),
[the untouched runner output, gzip](mercaweave-preactivation-ci-ui-hardening-artifacts/native-clean.log.gz),
and [execution receipt](mercaweave-preactivation-ci-ui-hardening-artifacts/native-clean-receipt.json)
identify this invocation. The generic inherited runner disk-space threshold
warning was retained; no disk-space error or test failure occurred.

| Current check | Estimate before execution | Actual | Pass / fail / skip |
|---|---|---|---|
| Incremental native Qt application and test build | 1–4 min | 28.496 s initial / 11.327 s final | PASS |
| Complete Qt executable selection | 4–7 min including focused wallet | 396.774 s wall | 3 / 0 / 0 CTest executables |
| Spend / coin selection / wallet PSBT / activation suites | included above | 103.944 s | 4 / 0 / 0 |
| Actual Qt visual matrix, 100% | 2–4 min | 131.368 s | PASS |
| Actual Qt visual matrix, 125% | 2–4 min | 133.698 s | PASS |
| Full Qt-backed three-node follow-up, final helper | 6–10 min at launch | 867.078 s | PASS, 30 named checkpoints |
| Clean default Mercatura-native suite | 25–40 min | 2217.760 s driver / 2132 s runner | 59 / 0 / 0, single clean invocation |
| Repository lint and diff checks | 1–3 min | 70.614 s initial / 15.123 s final | PASS, 383 source files and repository checks |

Qt details: `test_bitcoin-qt` passed in 309.22 seconds, `test_mining-qt`
in 87.50 seconds and `test_peersecurity-qt` in 1.00 second. They report **49
passed QTest rows, 0 failures, 3 existing skips**. Those skips are the six-worker
mining responsiveness variants exceeding this host's actual CPU/memory budget;
no new skip was introduced. The wallet application's own suites have no skips.
[Qt row totals](mercaweave-preactivation-ci-ui-hardening-artifacts/qt-assertion-summary.json)
preserve the distinction between CTest executable counts and internal QTest rows.

The focused wallet tests are `spend_tests`, `coinselection_tests`,
`psbt_wallet_tests` and `mercaweave_activation_tests`. Qt execution includes the
complete wallet application executable and unrelated existing mining/peer-security
executables. New assertions exercise **populated** Coin Control rows, full type
visibility at 900/1000/1200 dialog widths, larger fonts/long labels, and exact
outpoint persistence through selecting, sorting, filtering and tree/list toggles.
Existing Qt coverage retains actual v2/v3 signing, grouped byte estimation,
lock/unlock, model destruction/unload, wallet switching, activation changes,
confirmation invalidation and guarded unsupported-wallet behavior.

The final native runner selects only Mercatura's native default suite, with
`--jobs=4`; the full inherited Bitcoin functional suite is not substituted.
The real three-node case retains true grouped/mixed relay, exact negative
mempool/block reasons, foreign-genesis rejection, restart, wallet reload/rescan,
reindex, competing-chain reorg, reconfirmation and final independent verification.
All application builds use `-j4`.

The clean native invocation's three-node case passed in **822 seconds** with
all **26 named checkpoints**. It finished at height 120 and common block hash
`2f4c5729f7c39552704adfc02c12f00974c9552546367a913b4d63e59d7085b9`.
Its [fresh checkpoint results](mercaweave-preactivation-ci-ui-hardening-artifacts/clean-native-three-node-results.json)
were captured from that exact invocation before normal successful-fixture
cleanup. The longer runtime includes concurrent suite/Qt activity on this
2-CPU-quota worker; it is not a latency benchmark.

The native run uses the final production C++ source. The only later Python
follow-up changes the optional GUI matrix's selector restoration. Every other
Python AST node is identical, including all default native scenarios; the
[source comparison](mercaweave-preactivation-ci-ui-hardening-artifacts/gui-followup-provenance.json)
records both versions. The affected optional path is exercised by the separate
full Qt-backed follow-up. Its result is not combined with native case counts.

Retained setup history: an initial build command used the output filename
`mercatura-qt` instead of the CMake target `bitcoin-qt`; it exited before compiling.
Source inspection corrected the target. The first successful compilation exposed
a misleading-indentation warning in the new address-tooltip block; braces were
added and the final rebuild was warning-free. Neither setup event is called a
passing final build. Review also caught a state-cleanup gap in the optional screenshot matrix: after
capturing Automatic mode it must restore the previous mode before the existing
stale-confirmation scenario. The helper now restores and asserts the original
index. A full Qt-backed three-node follow-up exercises that final optional helper
and the ordinary confirmation/relay/persistence sequence. This inspection-only
follow-up does not change the default non-GUI native scenarios; the single
59-case invocation remains one run, not combined pass counts. Source diff hashes
and distinct execution receipts preserve when each version ran. No product
wallet defect or consensus defect was involved. Final artifact lint initially rejected trailing whitespace in the native runner's
unaltered output (15.670 seconds). The raw work log is preserved and its review
copy is archived losslessly as gzip, with a verified decompressed SHA-256. No
output content, test result or lint rule was changed. The failed receipt is
retained alongside the successful final rerun. Other failures are not erased.

## Final qualification status

| Gate | Current result |
|---|---|
| Linux affected code and regressions | **PASS**: 3 complete Qt executables, 4 wallet suites, clean 59-case native invocation, 30-checkpoint Qt-backed follow-up, both scaling matrices, lint/diff checks |
| Executed test failures | **0**; setup command and artifact-lint failures were repaired and retained separately |
| Skips | **0** native cases / **0** CTest executables; **3** existing internal six-worker mining QTest skips for host budget |
| Windows MSVC, new unpublished source | **BLOCKED / NOT TESTED**; original published Qt workflow remains **FAIL**, its observed source cause is repaired locally |
| macOS, new unpublished source | **NOT TESTED**, no attached macOS worker; earlier success remains historical |
| Native Windows and WSLg graphical desktop review | **BLOCKED / NOT TESTED**, no desktop worker; offscreen results are separately identified |

[Successful final lint receipt](mercaweave-preactivation-ci-ui-hardening-artifacts/final-lint-receipt.json)
and [final integrity checks](mercaweave-preactivation-ci-ui-hardening-artifacts/final-integrity-check.json)
record the final gates. All owned fixture node processes stopped normally.
The raw runner output can be inspected with `gzip -dc` on its `.log.gz` archive;
its decompressed identity is recorded separately. Final documentation edits fill
measured results and regenerate the artifact manifest; production/test code is
unchanged after its passing executions.

## Scope and security audit

Changed code paths in this checkpoint:

| File | Purpose |
|---|---|
| `src/qt/coincontroldialog.cpp` | Column layout and descriptive tooltips |
| `src/qt/test/wallettests.cpp` | Populated layout and exact selection-persistence regressions |
| `test/functional/mercatura_mercaweave_multinode.py` | Opt-in native GUI layout matrix; default network scenarios unchanged |
| `test/functional/mercaweave_gui_probe.cpp` | Inspection-only header/viewport/DPI metadata and ordinary widget actions |

The new report and its evidence directory are the only additional paths.
The existing Windows exception repair stays in the parent `2d79c33`.
[Scope audit and exact source hashes](mercaweave-preactivation-ci-ui-hardening-artifacts/scope-audit.json)
identify the tested source.

The only production file changed is the Qt display implementation. Wallet
funding/signing/PSBT/key derivation, eligibility/security rules, group bytes and
commitments, v2/v3 authorization and cryptography, network parameters/genesis,
MercaHash, DGW, emissions, monetary rules and PQ P2P source are unchanged.
Existing network and wallet capability checks still guard all grouped paths.
No research backend or test capability becomes reachable from production UI.

The 190 historical protected paths are unchanged by this task. The only mismatch
from the original frozen archive remains the earlier `src/mining/solo.cpp`
integration change, untouched here. Both production and frozen research checkouts
remain at their original clean heads. All 41 preceding report/artifact files,
including the fifteen screenshots, still exactly match `2d79c33`;
[their SHA-256 preservation record](mercaweave-preactivation-ci-ui-hardening-artifacts/prior-evidence-preservation.json)
retains the comparison. The daemon and CLI remain the same binaries; Qt and its wallet
test are rebuilt and receive new explicit identities. No existing public wallet
or blockchain directory is opened or changed.

## Reproduction and handoff

Use this local qualification checkout and its existing SDK/build configuration:

```bash
cd /workspace/Mercatura-MercaWeave-upgrade
/workspace/Mercatura-MercaWeave-v0.1/work/tools/cmake/data/bin/cmake \
  --build work/build-native-release --target bitcoin-qt test_bitcoin-qt -j4
QT_QPA_PLATFORM=offscreen \
  /workspace/Mercatura-MercaWeave-v0.1/work/tools/cmake/data/bin/ctest \
  --test-dir work/build-native-release --output-on-failure -j4 \
  -R '^(test_.*-qt|spend_tests|coinselection_tests|psbt_wallet_tests|mercaweave_activation_tests)$'
python3 work/build-native-release/test/functional/test_runner.py --jobs=4 --timeout-factor=3
```

The default functional suite creates disposable private regtest fixtures. Exact
execution commands, source diff hashes, timestamps and runtimes are retained in
the hardening artifact receipts. Probe compilation follows its recorded argument
array; it is a local Linux `.so`, never linked or installed in a production app.
For visual reproduction, use the existing three-node script with `--qt-binary`,
`--gui-probe-dir`, `--gui-probe-library`, `--gui-layout-matrix`,
`--qt-platform=offscreen` and a newly allocated `--tmpdir` plus separate
`--artifact-dir`. Set `QT_SCALE_FACTOR=1.0` or `1.25` before launch. The full
qualification can run normally; the executed inspection commands instead use
`--interactive=activated`, and their own marker ends only the inspection fixture.
All command arrays and marker paths are preserved in visual receipts.

No new Windows or macOS result is available for this unpublished source. Before
release-candidate qualification, independently run the dedicated MSVC and macOS
matrix on the authorized exact source, then review native Windows and WSLg desktop
layouts. Keep those gates separate from this successful/failed local evidence.
Keep public activation unset. Publication requires separate explicit authorization
for the preceding unpublished `2d79c33` plus this new local checkpoint; neither is
pushed here. Final commit identity and working-tree status are in
`/workspace/Mercatura-MercaWeave-upgrade/work/preactivation-hardening/delivery-receipt.json`
and the final handoff. The preceding unpublished commit requiring authorization is
`2d79c330f5ac246156d758e34763bbba9ff45524`; this report is contained in the new local
child commit. No publication is authorized by this handoff.
