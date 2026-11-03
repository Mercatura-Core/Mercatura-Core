# MercaWeave v0.1 — WO07 full integration qualification

2026-11-03, America/Los_Angeles. Isolated research qualification completed.

## A. Repository

Starting HEAD `6cc3925bd7431bd03a07d9b0b04de07550d95022`, branch
`research/mercaweave-v0.1`, research root `/workspace/Mercatura-MercaWeave-v0.1`.
Original `/workspace/Mercatura-Core` is on `work` at
`4b88b619fb1b08c7f30112a38eb471880b2a6af4`, also `project/v0.1`.
Both started clean; neither branch nor worktree was recreated. No fetch, pull,
push, merge, reset or rebase. No AGENTS.md was found at repository/ancestor paths.
The five WO02–06 reports and the embedded locked specification/directive in
WO01 Parts II/III are the authority for unchanged protocol behavior.

Tested implementation commit:
`8d56b7285974921d1e37d975def3c5421c528baf` — Qualify guarded native script checks
and durable grouped wallet research state. Its 17 files add 990 lines and remove
two lines. The final tested implementation HEAD is
`d45cb5f7f01ad911e37949c52d9aa3318c8c3e81` — Reject unknown durable journal
statuses before writing a checkpoint. That narrow follow-up changes only the
journal transition allowlist and adds two regression assertions; its 53-assert
persistence suite passes directly in all three builds. The full 32-test suites
were run at 8d56b72; the JSON distinguishes that checkpoint from the final
follow-up. The final containing evidence/report/runner commit is the ending
WO07 HEAD, identified by the final response and `git rev-parse HEAD`, avoiding a
recursive self-referential SHA. The complete local range is
`git log --format='%H %s' 6cc3925..HEAD`.

Both worktrees finish clean after that evidence commit; the original branch,
HEAD and project/v0.1 remain unchanged. No downloaded SDK, build product, wallet,
datadir, raw node log, expanded key or secret is committed. All earlier reports,
vectors and measurements are checked against the starting HEAD: **58 files are
byte-for-byte unchanged**. The new machine-readable artifacts are explicitly
confined to `src/test/mercaweave/full-integration-artifacts/`.

## B. Architecture

Preflight reproduced all 28 existing tests in Release, Debug and ASan/UBSan,
and 72 WO05 workload comparisons, six high-DPI runs and 57 prior evidence hashes.
The architecture review traced CheckInputScripts -> ValidationCache ->
CScriptCheck -> CachingTransactionSignatureChecker -> VerifyScript;
ConnectBlock -> script queue/UpdateCoins -> CBlockUndo; DisconnectBlock ->
ApplyTxInUndo; MemPoolAccept policy and consensus passes; CWallet's protected
seed helpers; SQLiteDatabase/SQLiteBatch; WalletModel/MiningSession and the
native MiningController. The old synthetic wrappers did not execute these full
paths. WO07 permits ordinary isolated v2 processes; v3 remains in process only.

New `node_validation` captures an immutable, complete transaction/request and
owns WO04 fixed-index script jobs, the authentic synthetic view owner/epoch and
one native precomputation. A private constructor can only be reached after full
uncached verification and binding. Real CScriptCheck objects retain shared
ownership until queued work finishes. Grouped jobs use the existing isolated
VerifyScript hook; native jobs use the actual original caching checker body.
Binding compares transaction/precomputation identity, all actual coins, amounts,
scripts, height/coinbase metadata, genesis and flags; epoch changes invalidate
old jobs. Queue completion is followed by freshness/binding checks in the test
coordinator. Native queue/ValidationCache lifetime contracts still apply.

The new `MERCATURA_MERCAWEAVE_NODE_RESEARCH_TEST_ONLY` guard changes only named
standalone research targets. A separate
`MERCATURA_MERCAWEAVE_VALIDATION_COMPONENTS_TEST_ONLY` selector compiles selected
unchanged production function bodies without constructing a full Chainstate.
Normal branch erasure reproduces pre-WO07 validation source on every nonempty
line, including the original native cache and script-check bodies. Earlier
interpreter/wallet/UI erasure and ABI/symbol checks remain active.
Ordinary root CMake does not include the standalone project or define any
research guard. No runtime activation mechanism, permissive bypass or normal
positive capability setter is added.

The native SQLite backend, actual parameter constructors and real database
transactions are linked into a separate research store, sharing native seed
protection. This is a research record namespace, not a production CWallet schema
migration. Qt remains the WO06 model/bridge plus ordinary linked native tests;
there is no new experimental GUI/node/wallet activation. The three ordinary
linked binaries (mercaturad, mercatura-cli and test_mining-qt) have no isolated
authorization, grouped wallet or attachIsolated symbols, and all 50 generated
normal target flag files contain no research definition. Binary hashes are in
qualification.json. Default standalone builds keep WO07 dependencies opt-in.

Full grouped Chainstate reuse is deliberately not simulated by mixing ordinary
root archives with the changed test CScriptCheck layout. Full-path work requires
a separate consistently compiled node ABI, new caller/context plumbing and
review of persistent undo and notifications. The present result stops at the
actual guarded script/undo components; the ordinary node is qualified separately.

## C. Consensus

The guarded actual CheckInputScripts branch authenticates coins before dispatch,
requires the unforgeable complete context, and bypasses BOTH script-cache lookup
and insertion. Merely passing cacheFullScriptStore=false would still consult
and erase a matching entry. Native PQ signatures have no signature-cache entry;
SignatureCache caches existing ECDSA/Schnorr only. No production cache key or
optimization changes. Native-only calls without a research context retain the
original cache path. Full-transaction group verification remains authoritative
and uncached; no failed transaction publishes an authorization context.

Coverage includes real inline/queued script execution, mixed ownership, factory
owner destruction, per-input jobs, foreign transaction/precomputation, changed
flags/genesis/amounts, missing context, changed metadata and stale epochs.
One thousand deterministic mutations compare the WO04 engine and cold/warm real
script paths: exactly 900 invalid and 100 unchanged controls, with a per-case
expected verdict and no disagreement. Each configuration executes **2,192 new
script/cache/undo assertions**. Deliberately primed production cache entries
cannot authorize contextless v3. Native-only cache hits still work; changed
native witness/body/genesis miss that cache and retain genuine Script failures. Real UpdateCoins, CTxUndo and ApplyTxInUndo exercise layered
staging, reverse descendant undo, conflicting branches and stale proof rejection.
An abandoned staging cache leaves the parent unchanged on late invalid groups.

Grouped v3 does **not** execute full Chainstate::ConnectBlock/DisconnectBlock,
BlockManager undo-file I/O, validation notifications or real mempool acceptance.
The fixture cannot substitute for headers, merkle/PoW, sequence locks or wallet
balance reconciliation. Normal v2 process tests exercise these actual paths
separately. The seven selected native functionals cover PQ reorg/mempool return,
RBF, packages/ancestor metadata, mempool reload, encryption/restarts, fee/dust
policy and real capacity boundaries, including 65,536 synthetic transactions.
The added synchronous-SQLite native lifecycle test uses genuine wallet-signed v2
parent/child and conflicting transactions. It connects A, disconnects it, replaces
both ancestor and descendant with B, mines B, reconsiders A, returns to A, and
requires the identical hash_serialized_3 UTXO hash. Wallet confirmations/conflicts,
precise missing-input rejection and full node restart/explicit wallet reload
are checked. This is actual native ConnectBlock/DisconnectBlock and notifications,
not a grouped-v3 consensus claim. Current grouped-input relay policy remains rejecting. No v3 network
transaction is created, submitted or broadcast.

## D. Wallet

`durable_wallet` uses the genuine SQLiteDatabase/SQLiteBatch implementation with
normal full synchronous durability, native network application_id/schema checks,
exclusive database ownership, native online backup and bounded record loading.
One atomic reserved-namespace record holds the native encrypted 32-byte seed
envelope, authenticated issuance/rotation metadata and durable reservations.
The journal uses a separate research-only HMAC-SHA3-384 domain under the existing
ownership seed. It is neither a protocol hash nor a second ownership seed.

Loading restores a locked wallet. Explicit successful unlock must authenticate
both wallet metadata and the journal before reservations become available.
Corruption locks/cleanses the wallet and exposes no active journal. Checkpoints
run inside the protected-seed callback so concurrent issuance cannot split the
metadata snapshot. No expanded signing key is stored. New database directories
are atomically claimed under each isolated build's `wo07-wallets/` directory;
existing arbitrary wallets and symlink paths are refused before writes. Backups
are confined there with owner-only permissions. Separate build roots allow
independent Release/Debug/sanitizer database fixtures.

Tests cover rotation beyond 64 issued addresses, restart with exact issued range,
wrong password, native rollback on abandoned batches, injected native commit
failure, abrupt child-process exit before commit, actual SQLite recovery, backup
restore, corruption and eight concurrent duplicate scheduling requests.
There are **53 new persistence assertions per configuration** in the final
follow-up (51 at the full CTest checkpoint). Watch-only native
SQLite reload preserves explicit public records, never owns a seed or reservation,
and uses public integrity checks rather than claiming ownership authentication.
The old facade's derivation/watch-only/recovery tests also pass unchanged.

The local record schema is version 1 and confined to a reserved research prefix.
There is no existing-wallet migration. Owners refuse unknown versions, wrong
genesis, malformed/bounded records or journal authentication failure. The journal
retains at most 256 identities, including terminal entries, and refuses further
reservations on exhaustion; it never silently prunes identities and reschedules
old bodies. Hardware power-loss/fsync failure is not emulated by process exit;
that remains a separate durability gate.

Actual grouped CWallet ownership import, WalletBatch schema migration,
rescan/lookahead against node history, native coin locks and ordinary wallet
transaction tracking remain gaps. Native-v2 CWallet process tests provide
separate compatibility evidence, not proof of grouped ownership integration.

## E. PSBT

All WO05 authentic-prevout construction, mixed signing, multisigner/offline,
combine/finalize/extract and bounded parser tests remain unchanged. New coverage
builds a real grouped proposal, closes the native database, reloads/unlocks it,
then explicitly signs/finalizes/extracts through the old authoritative pipeline.
Spending a reserved outpoint causes stale extraction rejection. Database reload
never restores an authorization proof or automatically unlocks/signs anything.
Native generic PSBT transport is reused, but grouped finalizer/coordinator
interoperability with an ordinary CWallet remains unqualified.

## F. Qt and mining

WO06 normal-gated Send/Receive/Mining panels, async generation cancellation,
QObject lifetime and native QR/clipboard/scaling tests remain active. Native
linked Qt/miner qualification executes 17 selected feature/data cases, plus six
Qt init/cleanup cases: **23 pass, zero fail/skip**. It covers real WalletModel,
MiningSession, MiningController, native encrypted address creation, canceled
unlock, node shutdown, page/wallet destruction, notification/chain-lock
contention, manual/combined worker limits, device discovery and 1x/2x icon layout.
Two actual live Solo rows (one worker, existing pre-v2 and v2 PoW test schedules)
complete three start/stop cycles each, checking UI heartbeat, publication and
shutdown. No MercaWeave activation schedule is supplied.

An additional native Boost probe links the unchanged native main.cpp and
wallet/test/mining_tests.cpp against the ordinary root libraries, with the exact
root-generated flags. It executes **eight** existing wallet/Solo/v2 tests.
The six Solo cases include 102 controller-mined blocks, maturity at depth
100/101, rejected premature spending, a genuine native PQ spend, subsidy plus
fees, stale work/duplicate handling and a competing-tip reorg. The other two
cover native payout persistence/locked reuse and combined v2 work. GPU cases use
explicit CPU-reference device test doubles: they verify controller logic and
native block acceptance, not physical GPU performance. This executable is a
focused Boost unit-test runner, not a restored standalone mining CLI.
No native CPU/GPU algorithm, reward destination, developer fee, MercaMiner CLI,
Secure Vault or ordinary mining path is modified. Native reward default remains
v2 and existing full reward accounting; auto consolidation defaults disabled,
threshold 50 mature rewards per exact public key, with explicit opt-in.

The durable journal commits reservations and unsigned transaction identity before
returning an entry. Duplicate ticks, including after restart or simulated
acceptance, reuse the same ID. Active overlaps fail; cancellation/conflict is
terminal for that body. Reconciliation reads actual owned-view outpoints; spent
inputs retire reservations. It stores no ready-to-run signing job, no live timer,
no proof, no expanded key and no broadcaster. A caller must explicitly reload,
unlock, reconcile and rebuild/revalidate a proposal. The journal is not wired to
a production timer or ordinary native coin-lock manager. Signing/acceptance is
simulated through explicit research APIs only, with no v3 RPC submission.

## G. Performance

Actual host: Debian 13.6, GCC 14.2, x86_64 EPYC 9V74, quota 3 CPUs,
10,207,480 KiB total RAM; preflight available about 9 GiB and disk 4.6 GiB free.
Builds are bounded to two jobs each, at most four concurrent compiler jobs.
No unlimited stress, remote benchmark, hardware upgrade or major toolchain
installation. Extracted development dependencies are confined to ignored work/.

All 14 A–N shapes match across Release, checked-STL Debug and ASan/UBSan:
42 new rows. The three CSVs retain full bytes, weight, witness authorization
counts, median/worst wall time, user/system CPU and process RSS. Each ordinary
transaction row has three samples; near-block K has one complete candidate
sample. Construction/key generation is outside the timed validation sections.
The timings below are this host's final bounded run, not WSL/Ryzen figures.

| Case | Inputs / outputs | Groups / witness signatures | Full bytes / WU | Release median ms |
|---|---:|---:|---:|---:|
| A-one-v2 | 1 / 1 | 0 / 1 | 5364 / 5646 | 0.357 |
| B-ten-v2 | 10 / 1 | 0 / 10 | 53145 / 54534 | 3.966 |
| C-one-v3 | 1 / 1 | 1 / 1 | 5397 / 5679 | 0.392 |
| D-53-one-group | 53 / 1 | 1 / 1 | 9557 / 16235 | 1.254 |
| E-100-one-group | 100 / 1 | 1 / 1 | 13317 / 25776 | 1.364 |
| F-100-ten-groups | 100 / 1 | 10 / 10 | 60675 / 73134 | 3.343 |
| G-100-independent | 100 / 1 | 100 / 100 | 534255 / 546714 | 26.286 |
| H-mixed | 55 / 1 | 1 / 3 | 20175 / 27099 | 1.541 |
| I-100-outputs | 1 / 100 | 1 / 1 | 9654 / 22707 | 0.466 |
| J-near-standard-weight | 1900 / 1 | 2 / 2 | 162581 / 396446 | 22.287 |
| K-near-4MiB-block | 777 / 777 | 777 / 777 | 4193615 / 4413167 | 197.092 |
| L-late-invalid | 100 / 1 | 100 / 100 | 534255 / 546714 | 16.270 |
| M-reorg-warm-cache | 100 / 1 | 1 / 1 | 13317 / 25776 | 1.222 |
| N-100-native-comparator | 100 / 1 | 0 / 100 | 530955 / 543414 | 33.279 |

A single grouped input is slightly larger than native v2. E (100 inputs/one key)
uses 13,317 bytes/one group signature versus N's 530,955 bytes/100 native
signatures, about 39.9 times less witness-bearing transaction data. These are
synthetic validation shapes: G/N exceed the unchanged 400,000-WU standard
transaction limit and are not relayable single transactions. J is 396,446 WU
(99.1% of that limit), still blocked by current grouped-input policy. K contains
777 one-input grouped transactions and a real serialized block envelope:
4,193,615 bytes, within 4 MiB; it uses the existing WO04 candidate fixture,
not full Chainstate or the new CheckInputScripts route for the whole block.
Its 4,413,167 legacy WU is measured rather than substituted for Mercatura's
current full-byte capacity rule.

Native-v2 rows include native preverification in the research factory and a
second genuine native script pass. Grouped signatures verify once during full
proof construction, with indexed Script consumption afterward. Thus witness
signature count is not the number of verification invocations: the pipeline
performs G + 2V cryptographic checks. Do not interpret its native timings as
optimized ordinary-node throughput. M rebuilds after an epoch change with a
reused cache object; grouped entries remain uncached.

Release RSS high-water reaches 16,980 KiB; Debug reaches 17,700 KiB
and sanitizer reaches 356,152 KiB. Full CPU figures are in the CSVs. RSS is process high-water, not isolated live-memory
or worst-case allocation proof; sanitizer overhead is not a production budget.
The unchanged 24 WO05 workload rows per build preserve all non-timing bytes,
fees, signature counts and fixture hashes: 72 comparisons to WO05, including
48 cross-build comparisons. This extends integration evidence without claiming
an unmeasured improvement over WO06.

The unchanged actual parameter constructors produce 30 network/height snapshots
in active-native-parameters.json. At height 200 main/regtest are 4 MiB;
public testnet is 1 MiB until its existing height 8,000 transition; signet and
testnet4 retain 1 MiB at that height. Existing absolute-height doublings and
1-GiB ceilings are read from code, not changed. Four MiB is not a universal
network/height assumption. Existing 400,000 standard WU, 80,000 block sigop cost
and 100-block maturity remain unchanged.

The provisional 4G+N charge does not bound serialization, hashing, ownership
comparison, native-v2 verification, database or all node CPU. Low-end hardware,
long-running concurrency and adversarial aggregate capacity calibration remain
WO07B/audit preparation work; no consensus budget is altered.

## H. Regression evidence

Final results (full CTest checkpoint plus the narrow persistence follow-up):

| Qualification | Passed | Failed | Skipped |
|---|---:|---:|---:|
| Release CTest | 32 | 0 | 0 |
| Debug CTest, _GLIBCXX_ASSERTIONS=1 | 32 | 0 | 0 |
| ASan + UBSan CTest | 32 | 0 | 0 |
| Final 53-assert persistence follow-up, one per build | 3 | 0 | 0 |
| Existing Mercatura native functional selection | 7 | 0 | 0 |
| Added native conflict/reconsideration/restart lifecycle | 1 | 0 | 0 |
| Native wallet/Solo/v2 Boost cases | 8 | 0 | 0 |
| Linked native Qt selected feature/data cases | 17 | 0 | 0 |

Qt additionally reports six passing init/cleanup entries (23 total). The Boost
probe's filtered-suite messages identify cases executed in the other invocation;
all eight file cases are covered, with no hardware pass inferred from a double.
CTest totals are 96, plus three final targeted persistence-program passes, not
including the separately reproduced 84 baseline results.
Final measured CTest elapsed times were Release 51.28 s, Debug 172.83 s and
sanitizer 241.83 s; earlier runs under concurrent compilation were slower.

The unchanged deterministic bounded mutation/parser campaigns remain active,
including 2,000 integrated wire mutations and 1,000 PSBT mutations. The new
1,000-case cache corpus is deterministic, not coverage-guided fuzzing. All nine
wallet/PSBT/Qt/planner/new-integration unit summaries and locked vector stdout
hashes match across builds. No new sanitizer exclusion is added. The inherited
WO03 policy.cpp vptr-only exclusion remains confined to that old malleability
target; all new research code, native SQLite and selected validation bodies are
ASan/UBSan instrumented.

An initial native functional attempt could not bind loopback sockets in the
default sandbox. The completed invocation used networking permission with
connect=0, DNS/fixed seeds disabled and loopback-only temporary nodes. The new
native lifecycle harness initially omitted explicit wallet load after restart;
it was corrected and passed from a fresh datadir. Neither was an authorization
counterexample. SDK configure/CLI header issues were resolved in ignored
workspace-only development files. No production check or test expectation was
weakened. The seven inherited functional fixtures set unsafesqlitesync=1 for
speed; the new lifecycle and new database crash tests explicitly use normal
synchronous SQLite and are the durability evidence.

Reproducible commands (from the research root, using the existing workspace SDK):

```bash
wo07_cmake="$PWD/work/tools/cmake/data/bin/cmake"
wo07_ctest="$PWD/work/tools/cmake/data/bin/ctest"
wo07_sdk="$PWD/work/wo06-tools/qt-sdk/usr"
for mode in release debug sanitize; do
  case "$mode" in
    release) wo07_kind=Release; wo07_san=OFF ;;
    debug) wo07_kind=Debug; wo07_san=OFF ;;
    sanitize) wo07_kind=RelWithDebInfo; wo07_san=ON ;;
  esac
  "$wo07_cmake" -S src/test/mercaweave -B "work/build-$mode" \
    -DCMAKE_BUILD_TYPE="$wo07_kind" -DMERCAWEAVE_SANITIZERS="$wo07_san" \
    -DMERCAWEAVE_QT_TESTS=ON -DMERCAWEAVE_FULL_INTEGRATION=ON \
    -DCMAKE_PREFIX_PATH="$wo07_sdk"
  "$wo07_cmake" --build "work/build-$mode" -j2
  ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
    "$wo07_ctest" --test-dir "work/build-$mode" --output-on-failure
done
# Recorded narrow follow-up after the full checkpoint:
for mode in release debug sanitize; do
  "$wo07_cmake" --build "work/build-$mode" --target mercaweave_durable_tests -j1
  ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
    "work/build-$mode/mercaweave_durable_tests"
done
PKG_CONFIG_PATH="$wo07_sdk/lib/x86_64-linux-gnu/pkgconfig" \
  "$wo07_cmake" -S . -B work/wo07-native-release \
  -DCMAKE_BUILD_TYPE=Release -DENABLE_IPC=OFF -DBUILD_GUI=ON \
  -DBUILD_TESTS=ON -DBUILD_BENCH=OFF -DBUILD_KERNEL_LIB=OFF \
  -DWITH_CCACHE=OFF -DCMAKE_PREFIX_PATH="$wo07_sdk"
"$wo07_cmake" --build work/wo07-native-release \
  --target bitcoind bitcoin-cli test_mining-qt -j2
python3 work/wo07-native-release/test/functional/test_runner.py --jobs=1 \
  --tmpdirprefix="$PWD/work/wo07-functional" \
  --resultsfile="$PWD/work/wo07-functional/results.csv" \
  mercatura_pq_reorg.py mercatura_pq_mempool_persistence.py \
  mercatura_pq_rbf.py mercatura_pq_package.py \
  mercatura_wallet_encryption_persistence.py mercatura_fee_policy.py \
  mercatura_block_capacity.py
python3 src/test/mercaweave/native_lifecycle.py \
  --configfile="$PWD/work/wo07-native-release/test/config.ini" \
  --tmpdir="$PWD/work/wo07-functional/reconsider-lifecycle-resumed" --randomseed=7 \
  > work/wo07-functional/native-reconsider.txt 2>&1
python3 src/test/mercaweave/build_native_probe.py \
  --build work/wo07-native-release --output work/wo07-native-probe
work/wo07-native-probe/wo07_native_unit_tests \
  --run_test=mining_tests_solo --log_level=test_suite
work/wo07-native-probe/wo07_native_unit_tests \
  --run_test=mining_tests_wallet,mining_tests_v2 --log_level=test_suite
```

Native Qt settings/cache/data and all output logs are confined to work/:

```bash
export QT_QPA_PLATFORM=offscreen
export XDG_CONFIG_HOME="$PWD/work/wo07-qt/config"
export XDG_CACHE_HOME="$PWD/work/wo07-qt/cache"
export XDG_DATA_HOME="$PWD/work/wo07-qt/data"
wo07_qt="$PWD/work/wo07-native-release/bin/test_mining-qt"
"$wo07_qt" missingWalletUnlockCancellationAndNodeShutdown \
  soloWalletAndPageLifecycle controllerDefersWalletDestruction \
  encryptedAddressCreation pageLifecycle unrestrictedV2CapacityControls \
  hardwareAwareManualWorkerSelection combinedWorkerSelectionReservesHostVerifiers \
  manualWorkersPersistAndRuntimeFormatting pickaxeFollowsPaletteAtHighDpi \
  -o "$PWD/work/wo07-qt/native-lifecycle.txt",txt
"$wo07_qt" walletNotificationContention transactionStatusChainContention \
  sharedDeviceDiscoveryRefresh actualGpuContextsAfterFallback \
  maximumAutomaticAndReducedManualGpuContexts \
  -o "$PWD/work/wo07-qt/native-contention.txt",txt
"$wo07_qt" liveSoloResponsiveness:height--1-workers-1 \
  liveSoloResponsiveness:height-0-workers-1 \
  -o "$PWD/work/wo07-qt/native-responsive.txt",txt
```

```bash
python3 src/test/mercaweave/qualify_full_integration.py \
  --release work/build-release --debug work/build-debug \
  --sanitize work/build-sanitize --native work/wo07-native-release \
  --functional-results work/wo07-functional/results.csv \
  --qt-logs work/wo07-qt --solo-logs work/wo07-unit-logs \
  --native-reconsider-log work/wo07-functional/native-reconsider.txt
```

The qualifier reads completed logs, runs public-fixture vectors/benchmarks/native
parameter probes and the final native-SQLite persistence suite, checks ordinary
binary symbols/flags, and writes the new JSON and CSV artifacts. The persistence
suite creates isolated fixture wallets, signs a fixture PSBT and exercises child
process crashes. It starts no ordinary node, GUI, native miner, external signing
service or network broadcaster.
Raw temporary logs and native Boost build commands remain ignored in work/;
pass/failure counts and public aggregate evidence are committed. CMake declaration
cleanup after the tested checkpoint preserved every compiler command exactly;
the only subsequent runtime change is the explicitly recorded journal status
allowlist fix, qualified with the final 53-assert persistence suite in all three
builds. The qualifier repeats and records those targeted results separately.
The native full node/Qt build is Release only; standalone research tests cover
Release, Debug and ASan/UBSan. LeakSanitizer is unavailable in this ptrace
container (detect_leaks=0); no leak-free result is claimed. GCC is the only
available compiler. No Clang, coverage-guided fuzzer, TSan, alternate platform,
Windows/WSL, GPU runtime or independent audit result is claimed.

## I. Security

No authorization-path disagreement, unauthorized spend, stale-cache bypass or
ownership leak was observed in completed qualification. Native reorg conflicts
and invalid spends have deterministic expected rejection. The restricted-socket
and missing-wallet-load harness failures above were corrected and rerun; they
are not silently counted as passed attempts.
Native full script-cache entries are deliberately poisoned in tests; grouped
verification still occurs uncached. Native v2 caching assumptions are preserved.
Database corrupt journal authentication, wrong passwords and rollback errors
fail closed. Final self-review found that Store::Mark previously accepted an
out-of-range enum value, which could write a journal its reload rejects. The
follow-up allowlist rejects unknown/reserved transitions before any write and
the new regression requires both exact Metadata error provenance and an unchanged
restorable checkpoint. No actual owner data or deployed wallet was affected.
Diagnostics and committed artifacts contain public fixtures and
aggregate measurements only; encrypted wallet databases stay in ignored work/.

Known WO03/04 generic mixed-scriptSig malleability is unchanged: P2SH scriptSig
mutation can alter txid and invalidate descendants, whereas witness-only changes
preserve txid but can change wtxid. The wallet subset uses native v2/v3 inputs
with empty scriptSig. No universal preconfirmation txid-stability promise,
protocol amendment or policy workaround is introduced.

## J. Remaining restrictions

Full grouped-v3 Chainstate connect/disconnect/undo files, live mempool admission,
package/RBF/ancestor/eviction and wallet reorg balance reconciliation remain gates.
Actual CWallet grouped persistence/seed import and native transaction lifecycle,
grouped Qt wallet process/restart ownership, native global coin reservations,
automatic durable timer resumption and real grouped reward spends remain gates.
The durable journal is a tested research primitive, not a production scheduler.
Normal unknown-v3 consensus remains permissive and normal input policy rejects
such spends; UI gating does not provide protected custody. Do not fund v3 on any
existing network. No activation or deployment is authorized by this report.

WO07B must retain these restrictions while expanding only qualified isolated
paths into bounded automated simulation. WO08 requires the remaining full-path
integration, broader adversarial campaigns, independent audit and environment
coverage before any production decision. No long-duration simulation is claimed.

## K. Readiness verdict

**READY WITH EXPLICIT RESTRICTIONS**

The code is suitable for bounded WO07B automated simulation of the isolated
script/coin-view, wallet/PSBT, durable-journal and Qt presentation components,
plus ordinary native-v2 node/miner lifecycle scenarios. Evidence is materially
deeper than the prior component checkpoints and preserves normal behavior and
all locked bytes. It does not establish full grouped-v3 node/wallet integration,
network custody, deployment readiness or automatic reward spending. WO07B must
label synthetic component and normal-v2 process scenarios separately and retain
all restrictions in J. Independent review and the remaining full-path gates are
required before expanding that scope.
