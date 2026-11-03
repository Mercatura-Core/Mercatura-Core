# MercaWeave v0.1 — Work Order 04 isolated integration

Date: 2026-11-02, America/Los_Angeles. Research only; no network activation.

## Architecture recorded before implementation

Verified starting HEAD `2ff60be0b6c5f58f36110ecba5cb72ca2663126d` on
`research/mercaweave-v0.1` in `/workspace/Mercatura-MercaWeave-v0.1`, clean.
Original `/workspace/Mercatura-Core` remains clean on `work` at
`4b88b619fb1b08c7f30112a38eb471880b2a6af4`. WO03 Release baseline: 11/11 pass.

Intended call path: isolated in-memory authoritative coin view -> genuine
CheckTransaction and Consensus::CheckTxInputs -> vin-order owned snapshot ->
WO03 complete uncached Prepare -> private immutable script jobs -> real
VerifyScript/VerifyWitnessProgram -> research-only checker consumes bound group
evidence. Non-v3 inputs retain genuine native interpreter/PQ verification.

The only production hooks will be guarded by
`MERCATURA_MERCAWEAVE_RESEARCH_TEST_ONLY` in interpreter.h/.cpp. Only explicitly
named standalone research targets define that symbol. The ordinary interpreter
build will contain neither the new checker virtual method nor v3 dispatch; no
script flag, runtime switch, network setting or activation mechanism is added.
Separate normal and isolated binaries prevent differently compiled checker ABIs
from sharing a process. A control executable and build/symbol checks will verify
the inactive path. Normal production CMake will not reference the research target.

This is an uncached dispatch wrapper exercising actual Script and consensus
transaction/coin-view components. It does not claim execution of the node's
CheckInputScripts/CScriptCheck cache/lock/queue orchestration or ConnectBlock.
Those paths depend on node-wide state and will remain a reported limitation.
Private jobs will call the genuine VerifyScript directly with the bound checker.

The synthetic UTXO owner will serialize mutations, assign a checked view epoch,
and capture/validate coins while locked. Proof publication and job execution
must reject changed view epochs. Candidate blocks will use real layered coin
caches and commit only after the entire fixture succeeds. No daemon, chain
parameters, wallet, PSBT, mining or existing data directory will be touched.

An unactivated witness-v3 output must never be presented as protected MercaWeave
custody. Existing unknown-witness permissiveness is not group authorization;
policy discouragement alone does not provide custody protection.

The section above records the pre-implementation architecture. The checkpoint
results below describe the implemented and tested path, including its limits.

## A. Repository verification and commits

Starting research HEAD: `2ff60be0b6c5f58f36110ecba5cb72ca2663126d`.
Branch/worktree and original checkout match the values recorded above. Rebuilt
and reproduced the 11/11 WO03 Release checkpoint before coding (2.18 seconds).
Read WO02/03 reports, the README, engine, context, native adapter, artifacts,
interpreter, transaction checks, policy, coin view and node cache orchestration.

Validated implementation commits:

```text
ff43eba264f627853f88b8951b4d911155bc2c7a Exercise isolated v3 Script dispatch against authoritative coin caches
7322bc634cb71165f0e6255566c3e7336a22dc6d Reserve block envelope bytes even for empty research candidates
535c17f9cd545a233ad84de903f667202e74293b Measure Script and coin-view integration overhead across isolated builds
```

Tested/measured implementation HEAD: `535c17f9cd545a233ad84de903f667202e74293b`.
The final documentation/evidence commit contains this report, the README update
and text artifacts. That containing commit is the final WO04 HEAD; retrieve its
exact hash with `git rev-parse HEAD`, and the entire WO04 list with
`git log --format='%H %s' 2ff60be..HEAD`. The final response records the hash.
Both worktrees are clean at completion; the original branch/HEAD are unchanged.
No push, merge, rebase, reset or new branch was performed.

## B. Implemented architecture and inactive-path proof

Research APIs: `integration::View::{Create,Insert,Spend,SetTip,CoinAt,Epoch}`;
`Engine::{Prepare,Bind,Validate,ValidateWire,Candidate}`;
private `TransactionProof` and fixed-index `ScriptJob::Run`. They live under
`src/test/mercaweave/isolated_validation.{h,cpp}`. Diagnostic booleans and
Prepared/TransactionResult fields cannot mint a proof or job.

Actual call path:

1. Engine::PrepareImpl creates one immutable CTransaction from the submitted
   transaction. Under the owned view's exclusive lock it calls genuine
   CheckTransaction, CheckTransactionWitnessResources, Consensus::CheckTxInputs,
   GetTransactionSigOpCost and the production fee/weight helpers. It obtains
   every CTxOut from actual CCoinsViewCache::AccessCoin in absolute vin order.
2. Optional CurrentPolicy mode additionally executes real IsStandardTx,
   AreInputsStandard and IsWitnessStandard. It retains current unknown-v3
   rejection. The separately named ConsensusFixture mode permits testing the
   isolated interpreter without claiming mempool acceptance or changing policy.
3. It reserves checked resource counters, then calls WO03 research::Prepare.
   This reconstructs every group/manifest, checks every actual ownership
   commitment and group signature, and genuinely verifies every native input.
4. Only complete success creates the private TransactionProof, binding owned
   request, complete group proof, flags, genesis, view identity/epoch and spend
   height. Production transaction precomputation is owned by that proof.
5. Engine::Bind compares exact transaction/witness, flags, genesis, height and
   current view identity/epoch once. It creates private jobs with fixed absolute
   indexes. ScriptJob::Run holds a view read lock and invokes real VerifyScript.
6. In the isolated interpreter only, VerifyWitnessProgram(version=3) dispatches
   to the job's private checker. It requires a native 32-byte program, identical
   program/witness/flags and corresponding complete group evidence. Missing
   context defaults to SCRIPT_ERR_WITNESS_PROGRAM_MISMATCH, without reaching
   unknown-version success. P2SH-wrapped v3 rejects.
7. Non-v3 Script uses real TransactionSignatureChecker; PQ calls delegate to
   its unmodified CheckMercaturaPQSignature and pinned ML-DSA wrapper.

There is no global hook registry, thread-local authorization, external checker
callback, already-verified flag or serialized proof import. Jobs own immutable
transaction/data lifetimes and cannot be retargeted to another input. A whole
validation order must include every absolute index exactly once.

Only two production files change: interpreter.h adds an 11-line guarded checker
method; interpreter.cpp adds a 6-line guarded dispatch branch. Both use
`MERCATURA_MERCAWEAVE_RESEARCH_TEST_ONLY`. No normal CMake, verification flag,
network parameters, cache implementation or v2 authorization code changes.

Compile/link isolation is executable evidence, not only a naming convention:

- Exactly one standalone target defines the macro: mercaweave_native_isolated.
  Its explicit research dependents inherit it. Ordinary native targets do not.
  Compile-command checks distinguish 38 isolated and 56 ordinary translation
  units, including separate interpreter.cpp objects.
- Isolated executables link mercaweave_native_isolated, never the ordinary
  mercaweave_native archive in the same process. All checker ABI consumers use
  consistent definitions. The common primitive/crypto library does not use
  BaseSignatureChecker. Normal root/node/test CMake never includes this project.
- Deleting the two guarded additions yields byte-identical WO03 interpreter
  source/header. Normal builds have no experimental checker method or dispatch.
  Source erasure hashes are recorded in each *-isolation.json artifact.
- nm shows no research checker symbol in the normal control, and its presence
  in the isolated integration binary. It also shows no SignatureCache or
  ValidationCache methods in that integration binary.
- The isolated API header deliberately fails normal compilation without the
  research symbol. A negative syntax-only compile verifies this guard.
- Separately linked control binaries demonstrate current normal v3 success
  under mandatory unknown-witness semantics and rejection under policy flags.
  The isolated contextless path rejects under both. Both execute 20 genuine
  native-v2 verification regressions successfully.

This does not constitute a full node/wallet binary build. The source erasure,
target graph, symbols, actual controls and native regressions establish the
inactive-path evidence available here. No deployed-node runtime switch exists.

## C. Security, authenticated UTXOs and cache boundaries

The authoritative root here is a synthetic fixture-owned real CCoinsViewCache,
not a network-authenticated chain. Engine entrypoints accept the view and a
transaction; they accept no caller-supplied spent amount/script vector. Trusted
fixture funding is distinct from validation. CoinAt returns a copy, and the
mutable cache itself is private. Amounts used by signing alone cannot replace
the authoritative view's amounts during validation.

Every Insert/Spend/SetTip mutation uses an exclusive lock and checked epoch
advance. Capture also uses an exclusive lock because AccessCoin may populate
the cache. The expensive cryptographic pass runs on owned immutable copies;
publication rechecks the epoch. Bind and every job reject stale or foreign
views. Jobs hold read locks across actual Script execution, and Finish rechecks
the epoch before publishing its diagnostic success. A view restored to identical
coin bytes still has a new epoch. Height and coinbase metadata changes therefore
cannot hide behind identical signed amounts/scripts. Caller-owned transaction
mutation after capture does not mutate a job; rebinding a changed transaction
rejects. As with ordinary C++ APIs, the submitted input must not be concurrently
written while it is being copied.

Malformed synthetic coin ranges are checked before invoking the native input
validator's authentic-coin precondition, preventing unsafe signed accumulation
in deliberately invalid fixtures. An attempted insertion of the native spent
sentinel is rejected; the negative-amount fixture uses -2, since -1 means null.
Coinbase maturity and missing/spent rejection execute the genuine native checks.

Candidate uses a real CCoinsViewCache layered over the locked parent cache.
Transactions undergo the same proof/Script path before SpendCoin/AddCoins.
Invalid candidates discard staged writes; valid candidates can flush the staged
cache into the parent and advance its epoch. Old jobs become stale. Staging
proofs are removed from returned receipts so they cannot outlive the parent-lock
scope. Replaced-parent, reordered parent/child, conflict, late-invalid, rollback
and explicit coin restoration/tip changes are executable fixture cases.

These are synthetic reorganization/state-transition tests, not execution of
ConnectBlock, DisconnectBlock, a chain-selection algorithm or persistent UTXO DB.
Candidate excludes coinbase transactions supplied by callers; its synthetic
coinbase is for real CBlock serialization/accounting only. It does not perform
PoW, header/merkle validation, sequence locks, all finality checks, full mining
reward accounting or network relay/mempool admission.

Experimental authorization remains uncached across requests. Each fresh Prepare
reruns group ownership, manifests and all signatures. A retained complete proof
is transaction-local evidence and must pass current view/binding checks again.
No node Script or signature cache is linked/called; no experimental result can
be inserted into a normal-node cache by this path. Native preverification and
subsequent real Script dispatch deliberately verify v2 twice, with both calls
counted. This is qualification overhead, not an optimized production design.
There is still exactly one ML-DSA verification per attempted group.

Production cache audit is unchanged: signature caching overrides ECDSA/Schnorr,
whereas native PQ calls the wrapper directly. CheckInputScripts' whole-Script
cache binds process salt, wtxid, flags and optional PQ genesis, and assumes an
authentic coin view. That cache fast path and CScriptCheck/CCheckQueue node
orchestration are source-inspected but not executed by this dispatch wrapper.
No claim of populating, hitting or evicting an actual v3 production cache is made.
Changed transaction, witness, flags, genesis, view, membership or height cannot
reuse a research proof; a newly appended follower with a correct commitment
still needs a fresh real signature. Native-v2 proofs cannot authorize v3.

No unauthorized-spend or consensus-divergence counterexample was reproduced.
Locked digests, FIPS context, witness format, tags, limits and provisional 4G+N
are unchanged. No specification amendment is required by the observed cases.

## D. Testing, mutation coverage and determinism

Exact commands, from the research root, using local CMake 4.4.4:

```bash
work/tools/cmake/data/bin/cmake --build work/build-release -j2
work/tools/cmake/data/bin/ctest --test-dir work/build-release --output-on-failure
work/tools/cmake/data/bin/cmake --build work/build-debug -j2
work/tools/cmake/data/bin/ctest --test-dir work/build-debug --output-on-failure
work/tools/cmake/data/bin/cmake --build work/build-sanitize -j2
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 work/tools/cmake/data/bin/ctest --test-dir work/build-sanitize --output-on-failure
python3 src/test/mercaweave/qualify_integration.py --release work/build-release --debug work/build-debug --sanitize work/build-sanitize --output-dir src/test/mercaweave/integration-artifacts
git diff --check
```

For fresh builds, configure the standalone src/test/mercaweave project with
Release, Debug or RelWithDebInfo plus MERCAWEAVE_SANITIZERS=ON, respectively,
as in the preserved README. Build parallelism never exceeded four; new native
dependencies and sanitizer work used two jobs. No inherited functional suite ran.

| Build | Passed | Failed | CTest skipped | CTest wall time |
|---|---:|---:|---:|---:|
| Release | 17 | 0 | 0 | 6.09 s |
| Debug | 17 | 0 | 0 | 24.93 s |
| Combined ASan/UBSan | 17 | 0 | 0 | 15.60 s |

The eleven WO03 targets remain green, including the original native v2's 91
assertions, canonical cross-language vectors, parser/hostile mutations and seven
overflow boundaries. New targets are normal_inactive_control,
isolated_missing_context_control, authoritative_coin_view_probe,
isolated_validation_integration, research_compile_and_cache_isolation and
integrated_resource_measurement_smoke.

The integration suite has 2153 assertions, 31 counted rejection/binding cases,
2000 new seeded wire mutations and 2800 barrier-coordinated genuine Script
calls across four workers. Twenty seeded input permutations and forty reentrant
full validations also pass. Wire mutation seed is 0x574f3034494e5447; the exact
final mutation failure counts are Transaction=3, Inputs=26, Group=1971. No
wire mutation publishes a proof. Counts describe bounded cases, not exhaustive
fuzzing or unique totals across overlapping prior suites.

Cases include fully signed in-limit 4096-input/4-group, 256-group and 1024-member
transactions; over-limit structural fixtures prove rejection only. Malformed
stacks, key/signature truncation, native witness-item size/count bounds, program
length, nonempty v3 scriptSig, CompactSize, references, duplicate PKs, wrong flags/
genesis, reordered inputs, invalid preceding v2, altered outputs/coins, partial
group failure, new follower, stale/foreign proof, spent/missing coins, maturity,
budget overflow/bounds, fee size and current 400000-WU policy are exercised.
The new empty-candidate envelope check is separately committed with its test.

The qualifier completed 40 benchmark rows: ten packed plus ten bounded Release
rows, ten Debug bounded rows and ten sanitizer bounded rows. All 20 cross-build
bounded-row comparisons agree on verdict, stage, bytes, charges and actual
verification/hash counters. Bounded means at most two transactions, subject to
the same budgets; the maximum-input case fits only one. The vector dump, full
integration proof/summary and both inactive-control outputs are byte-identical
across all three builds. Vector dump SHA256 remains
17306fd48a53acda5a6425f909904e35da8beac37f25b709919ee68df5e5a6f7;
integration output SHA256 is
10e22b561f37dba524211184e315c03a807e0456dc090e28c47c65343d8013c8.
Sixteen prior evidence files compare unchanged against WO03. The eight invalid
benchmark samples have no valid dispatch proof: their dispatch timings are -1
(unavailable), not successful skipped Script checks.

Important failure provenance:

| Trigger | Stage / reason |
|---|---|
| Missing or spent authoritative coin | Inputs / bad-txns-inputs-missingorspent |
| Immature coinbase | Inputs / bad-txns-premature-spend-of-coinbase |
| Wrong view genesis or missing mandatory flags | Network / wrong-view-genesis; Flags / mandatory-flags-missing |
| Wrong member commitment | Group / Ownership, before ML-DSA |
| Altered output/amount or final signature | Group / Signature; no usable proof |
| Malformed group stack/key/signature | Group / Witness |
| Bad references, duplicate anchors or wrapping | Group / Reference, DuplicateKey or Wrapped |
| Missing context | MissingContext; direct isolated interpreter reports WITNESS_PROGRAM_MISMATCH |
| Exact request/flag/height mismatch | Binding / transaction-flags-genesis-height-mismatch |
| Changed view/tip/restored coins | StaleView |
| Current v3 policy | Policy / inputs-not-standard |
| Large standard transaction | Policy / tx-size |
| Invalid CompactSize/truncated wire | Transaction / bounded-decode |
| Zero-byte empty-candidate budget | Budget / block-envelope-budget |

All new research and isolated native validation code uses ASan/UBSan, including
vptr checks on the newly linked production policy/coin-view target. The older
WO03 malleability-only target retains its narrowly scoped policy.cpp vptr
exception for discarded node RTTI; that exception no longer applies globally.
Leak detection was retried and remains unavailable because LeakSanitizer reports
its ptrace fatal error (exit 1). It is not counted as passed. No leak-free claim.

Only GCC14.2 is installed; g++/g++-14 aliases do not count as two compilers.
Clang, libFuzzer tooling, AFL and honggfuzz are unavailable. No major toolchain
was installed. Alternate compiler and coverage-guided fuzzing are not run;
LeakSanitizer is unavailable. TSan and another architecture are not qualified.
These are separate limitations, not skipped CTest cases counted as passes.

## E. Performance and resource invariants

The fixed research budget remains 4194304 full block bytes, 16384 witness items
and 80000 existing-plus-group sigop cost. The native CheckTransaction stripped
size check also executes. BIP141-compatible weight is reported separately from
Mercatura's undiscounted bytes. The research Ledger still measures weight without
imposing an additional default weight cap, as in WO03; no network limit changes.
CurrentPolicy retains the real 400000-WU standard transaction limit. Its native
v2 fixture passes genuine input/witness policy, while v3 inputs remain unknown
and rejected. The large independent-group fixture rejects with tx-size. Actual
GetTransactionFeeSize equals full transaction bytes in these cases; no witness
fee discount is introduced. Genuine native sigops remain separate from 4G+N.

The layered candidate reserves resources before group verification and never
refunds a failed authorization. An empty candidate now reserves the synthetic
block envelope too. A failed budget reservation cannot authorize the successful
prefix. For a missing coin or malformed transaction rejected before admission,
the candidate fails outright; it cannot silently continue as an accepted block.
The early-invalid fixture stops at its first failed transaction, whereas the
WO03 comparison Workload intentionally evaluates all submitted requests. Its
lower rejection time is not an equivalent-work speedup.

Measurements use fresh processes on Debian13.6, x86_64 AMD EPYC9V74, three visible
CPUs and about 9.7 GiB RAM. GCC14.2 C++ Release uses -O3 with -UNDEBUG. Fixtures,
real key generation/signing and initial UTXO funding are outside timings. Each
phase has one warmup and seven measured samples, reporting median/max and mean
user/system CPU. Different phases are timed independently; these medians are
not an additive decomposition and VM scheduling is not controlled.

Same-run per-transaction medians (milliseconds):

| V3 inputs / groups / native-v2 inputs | WO03 Prepare | Bound real Script dispatch | Complete integrated transaction |
|---|---:|---:|---:|
| 1 / 1 / 0 | 0.150 | 0.000240 | 0.201 |
| 53 / 1 / 0 | 0.533 | 0.007832 | 0.755 |
| 100 / 1 / 0 | 0.908 | 0.014472 | 1.109 |
| 100 / 100 / 0 | 16.974 | 0.028162 | 21.285 |
| 256 / 256 / 0 | 45.118 | 0.069905 | 55.764 |
| 1024 / 1 / 0 | 9.094 | 0.145699 | 11.915 |
| 4096 / 4 / 0 | 39.040 | 0.607103 | 41.601 |
| 53 / 1 / 2 | 1.003 | 0.250727 | 1.711 |

WO03 Prepare is the same uncached research algorithm on the identical captured
request, compiled consistently in the isolated executable. It is not a rerun of
the historical WO03 binary or an assertion that earlier host timings must match.
Complete transaction time includes real transaction/coin checks, acquisition,
copies, precomputation, exact proof binding and actual Script dispatch. Dispatch
time includes per-job read locking and, for v2, genuine additional cryptography.

Packed available candidate-fixture medians (milliseconds). G is submitted
potential groups; verifies counts all attempted group and native PQ calls:

| Case | Submitted TXs | Inputs | G | Full submitted block bytes | PQ verifies | WO03 block | Integrated candidate | Peak RSS KiB |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Small 1/1 | 777 | 777 | 777 | 4193615 | 777 | 140.818 | 211.708 | 25840 |
| Consolidation 53/1 | 153 | 8109 | 153 | 1462365 | 153 | 92.723 | 134.414 | 22588 |
| Consolidation 100/1 | 81 | 8100 | 81 | 1078821 | 81 | 86.492 | 123.694 | 20528 |
| Independent 100/100 | 7 | 700 | 700 | 3739929 | 700 | 124.442 | 170.383 | 30912 |
| Maximum groups | 3 | 768 | 768 | 4102971 | 768 | 130.819 | 225.164 | 51588 |
| Maximum members | 7 | 7168 | 7 | 610817 | 7 | 67.474 | 95.354 | 19308 |
| Maximum inputs | 1 | 4096 | 4 | 348929 | 4 | 32.640 | 45.302 | 27916 |
| Mixed 53+2 per TX | 147 | 8085 | 147 | 2965869 | 735 | 154.552 | 246.129 | 29888 |
| First transaction malformed | 81 | 8100 | 80 | 1078788 | 0 | 77.331 | 0.151 | 13056 |
| Final transaction/signature invalid | 3 | 768 | 768 | 4102971 | 768 | 118.483 | 146.854 | 34388 |

Mixed accounting is 8379 group cost plus 1176 existing native cost, unchanged
from WO03. Its actual PQ work is 147 group calls, 294 native preverification calls
and 294 native dispatch calls. The final invalid fixture charges all three
transactions and attempts all 768 group signatures; it executes Script only for
the first two successfully authorized transactions. The early-invalid fixture
charges one attempted transaction (group cost 100), does zero crypto/ownership
work and stops; the other 80 submissions never gain acceptance.

For valid packed cases, proposed group costs are respectively 3885, 8721, 8424,
3500, 3840, 7196, 4112 and 8379. Existing native cost is zero except the mixed
row. Bytes or witness items bind these samples before the sigop cap. No result
establishes that 4G+N alone bounds CPU. The original WO03 matrix, staged hashing
and oversized-output evidence remain preserved.

Ownership PK hash volume remains 1952 times actual v3 ownership checks: 7995392
bytes for maximum inputs, 15828768 for packed 53/1, and 15208032 for the mixed
row. Every fully authorized transaction has one v3 component/body pass and one
manifest hash; final digests occur once per attempted group. These counters do
not include inherited txid/wtxid hashing, UTXO SipHash or native PQ precomputation.
The wrapper and synthetic serialization construct additional immutable transaction
identities and copies; that overhead is measured, not eliminated by the group
hash-pass counter. Bind is once per transaction, not a full-wire comparison per
input. Native precomputation occurs in both native passes where needed.

Group-heavy CPU is dominated by ML-DSA; follower-heavy CPU includes repeated
1952-byte PK commitment hashing. Acquisition, transaction identity hashing,
copies and layered cache updates account for integration work beyond the small
v3-only Script-dispatch time. Optimization of those copies or duplicated native
passes is deferred; security and inactive-path behavior take precedence here.

Mean candidate user/system CPU is 209.769/0 ms for small, 45.543/0 for maximum
inputs, and 246.483/0.760 for mixed. Maximum-group median/worst wall time is
225.164/343.740 ms, illustrating VM noise. Its peak process RSS is about 50.4 MiB;
ordinary C++ new requests total 80.63 MB across the separate candidate probe.
Requested allocation volume is not live memory and excludes malloc/realloc,
aligned new and stack usage. RSS includes fixtures, simultaneously retained
phase results and the Python launch floor. No cold-cache, Ryzen, broader hardware
or full-node performance claim is made.

Evidence: src/test/mercaweave/integration-artifacts/ contains Release/Debug/
sanitizer CSVs, normal/isolated control JSON, deterministic integration JSONL,
canonical vector output, per-build isolation results, leak diagnostic and
qualification.json. The latter records source HEAD/hashes, binary hashes,
commands, flags, CTest pass/fail/skip counts, timing method, all 40 rows, 20
cross-build comparisons and hashes of the 16 preserved prior evidence files.

## F. Integration-level malleability

The suite drives real Script, coin acquisition and candidate-cache updates for
the preserved P2SH/P2WSH OP_DROP OP_TRUE predicate examples. A permitted P2SH
argument changes from 0x41 to 0x42 while all v3 authorization digests remain valid.
The parent txid changes from
df160f0e7070cf1ba1698b553b8ac4613533f98bb0fb604838a15263acb2c92d to
e9e75d2c625c82c2d11398757556ca9f366df829fd408d6bf00500c4313b0d38.
An actual child signed for the original COutPoint passes after the original
parent is applied to one view, but fails native CheckTxInputs with missing/spent
inputs in the view containing only the mutated parent. It cannot be repaired
by supplying a copied spent-output record to the validator.

The P2WSH argument mutation changes wtxid while txid remains
58139eb9b4815b5dd7f76492f5620f518717b7f64f52686564fb8a95977eaaeb;
the child's actual coin still resolves. Valid randomized ML-DSA signatures for
native witness funding also preserve txid and change wtxid while integrated
verification succeeds. Nonempty v3 scriptSig continues to reject.

No unauthorized ownership or destination change is shown. Transaction identity
malleability can nevertheless disrupt pre-signed descendants, CPFP/package
coordination and wallet/DEX sessions keyed by an unconfirmed parent txid. An
uncooperative coordinator can exploit that disruption without stealing keys.

Options remain conceptual: A preserves the locked digest and accepts documented
identity risk; B imposes a carefully defined wallet/coordinator funding subset
for stable-txid workflows; C requires a versioned noncircular scriptSig commitment
design and new specification/vectors. Generic scriptSig signatures cannot simply
be committed without analyzing circularity. A consensus/relay restriction under
B would also need separate approval. None is implemented. Native v2/v3-only
fixtures are the safe workflow baseline here. Current policy's unknown-v3
rejection is unchanged and must not be represented as MercaWeave custody.

## G. Outstanding risks and explicit limits

- Independent security audit and external review of the macro/ABI isolation.
- Real CheckInputScripts/CScriptCheck cache and queue entrypoints, all fast paths
  and integration with cs_main/authenticated persistent chainstate.
- Actual ConnectBlock/DisconnectBlock, reorganization handling and mempool
  reconciliation. The synthetic epoch/cache fixture is not a full node.
- PoW/header/merkle, sequence-lock/finality and other block-validation gates
  outside this wrapper. No real full-node performance/acceptance is claimed.
- Native duplicate preverification/dispatch work is intentionally unoptimized.
  Removing it later needs preservation of complete proof and native semantics.
- Coverage-guided fuzzing, supported-host leak tests, TSan, alternative compiler/
  architecture and broader low-end hardware/concurrent resource calibration.
- Wallet/coordinator transaction-identity policy, key exposure/linkability and
  broader Script compositions. 4G+N remains an incomplete CPU metric.

## H. Readiness verdict and Git hygiene

**READY WITH EXPLICIT RESTRICTIONS**

This authorizes no deployment or activation. It recommends further isolated
qualification of the actual node cache/queue/chainstate entrypoints under a new
work order. The implemented wrapper safely reaches real production components
and preserves the locked protocol, but it is not release-ready consensus code.
Normal node and wallet compilation retains the current v3 path. Do not use
unactivated v3 outputs for protected custody on any network.

Retain the compile-time target boundary, private complete proof, authenticated
coin-view acquisition, uncached baseline, strict state/version binding and all
existing resource/policy rules. Resolve the outstanding gates before expanding
the scope. This coding agent's self-review does not replace an independent
security audit.

All code/evidence commits follow relevant tests, complete diff inspection and
git diff --check. The final working tree and original worktree are clean. Only
the two guarded interpreter files, research CMake/sources, this report, README
and new text artifacts change in WO04. Earlier reports/vectors/measurements are
byte-for-byte preserved. No private keys, binaries or build artifacts are staged.
No push, merge, original-worktree edit, chainparams change, activation logic,
activation height/schedule, daemon, network, existing datadir, wallet, mining,
PSBT, Qt or receiving-address work occurred. Scratch/builds remain under work/.
