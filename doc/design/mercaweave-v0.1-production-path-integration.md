# MercaWeave v0.1 — WO07C production-path integration

## A. Executive summary

WO07C connects the locked grouped authorization engine to genuine production
Script, CScriptCheck, Chainstate, CWallet, WalletBatch, SQLite and PSBT components.
Positive grouped operations require an in-process capability constructed only by
a separately guarded C++ fixture. Ordinary nodes, RPCs and wallets have no such
factory, setting or persisted capability. No network activation is implemented.

This report distinguishes actual component execution from activated network
qualification. It does not describe unknown-witness permissiveness as ownership
protection. Real-node grouped-v3 acceptance remains **NOT TESTED**.

Both original production-component gaps are closed **with the isolated execution
restrictions in X**. Release, Debug and combined ASan/UBSan each passed 35/35
standalone CTests and 14/14 actual-library Boost cases (22,476 assertions per
native run). Seven selected ordinary-node v2 scripts and the final wallet
persistence follow-up passed. No activated-network readiness is implied.

## B. Repository state and starting checkpoint

Research root `/workspace/Mercatura-MercaWeave-v0.1`, branch
`research/mercaweave-v0.1`, clean starting HEAD
`0de90cccbb33c033edd5c3578a02d53967791682`.
Original `/workspace/Mercatura-Core` is clean on `work` at
`4b88b619fb1b08c7f30112a38eb471880b2a6af4`, also `project/v0.1`.
No additional commits need reconciliation. No AGENTS.md applies.

Read the complete supplied WO07C, the embedded locked WO01 specification,
WO01 source audit/validation, and WO02–WO07B reports. Existing Release,
checked-STL Debug and ASan/UBSan standalone builds reproduce **34/34 each**
before critical edits (61.70 / 211.20 / 164.01 seconds under concurrent load).
Leak detection remains unavailable in this ptrace environment, not passed.
The existing native functional runner is
`work/wo07-native-release/test/functional/test_runner.py`.

## C. Authoritative specification references

Read the complete supplied WO07C and locked WO01 Part III, all 13 sections,
including serialization, commitment, manifest, digest, limits, derivation and
PSBT rules. The supplied WO01 document is 77,910 bytes. WO02–WO07B reports and
their source audits, readiness limitations, wallet, Qt and integration findings
provide historical evidence; they do not replace the locked protocol.

`script/pqgroup.cpp` is the former WO01 implementation moved out of test/.
`production_path/audit.py` compares it byte for byte with the initial engine,
normalizing only its include path. There is no new ML-DSA or SHA3 implementation.
The previous deterministic vectors and all earlier research evidence remain
immutable and are checked against the full starting commit.

Protocol remains OP_3/push32; commitment tag `Mercatura/PQGroupKey/v1`;
1952-byte public key, 3309-byte signature, 32-byte nonce; lowest-vin anchor;
strict prior absolute LE32 follower references; exact-public-key grouping;
unchanged transaction-wide SHA3-384 hashes, manifest and signing context
`Mercatura/PQGroupAuth/v1`; limits 4096 grouped inputs, 256 groups, 1024 members;
additional cost `4G+N`. Full bytes remain fee bytes. Native v2 remains independent.

## D. Initial production-path map and gap register

| Path | Starting classification | Existing implementation / exact missing work |
|---|---|---|
| Wire, transaction/witness parsing | IMPLEMENTED / RESEARCH ONLY | Native transaction decoder plus bounded grouped research parser. Shared grouped grammar still lives under test/. |
| Group ownership, manifest, digest, ML-DSA | RESEARCH ONLY | `mercaweave::{Reconstruct,CheckOwnership,TransactionHashes,Verify}`; exact frozen vectors. Move the same implementation into a reusable production component, without duplicating cryptography. |
| Native Script dispatch | PARTIAL | Guarded WO04 checker hook; ordinary unknown-v3 remains permissive under mandatory flags. Add dormant typed group-context consumption, preserving that existing inactive behavior. |
| CheckInputScripts / CScriptCheck / caches | PARTIAL | WO07 selected function bodies use research View/Engine. Need authentic complete vin-order coins, immutable owned jobs, exact binding and uncached grouped route in production implementation. |
| Full Chainstate::ConnectBlock / DisconnectBlock | MISSING | Generic production connect/undo exists but has no grouped caller. Need isolated direct real routines, fee/resource checks, persistent undo, rollback, coins/reload and competing-history tests. |
| Mempool / block P2P acceptance | MISSING FOR V3 | Remains deliberately inactive. No activation, policy change or positive v3 node-process scenario is permitted. Native v2 tests are separate. |
| CWallet seed, encryption, native v2 signing | IMPLEMENTED | `InitializeMercaturaPQWallet`, `WithMercaturaPQMasterSeed`, EncryptWallet/Unlock/Lock, WalletBatch and SQLite. Reuse this authoritative seed and lifecycle. |
| CWallet v3 ownership / addresses / coins | MISSING | Only the WO05 facade owns grouped records. Need real wallet metadata loading, nonce/key derivation and IsMine/coin enumeration under isolated capability. No ordinary v3 receive flow. |
| CWallet grouped signing / construction | MISSING | Real SignTransaction currently accepts v2-only PQ input composition. Need complete group coordinator, atomic witness publication and mixed native signing. |
| Genuine wallet PSBT | PARTIAL | Native containers/parser/Merge; WO05 research orchestration. Need actual FillPSBT and strict grouped combine/finalize/extract with complete trustworthy prevouts. |
| Wallet persistence / recovery | RESEARCH ONLY FOR V3 | WO07 Store uses genuine SQLite in a separate namespace. Need actual CWallet/WalletBatch metadata, native encrypted seed, backup/reload and recovery, with no second ownership seed. |
| Ordinary Qt/RPC receiving/broadcast | IMPLEMENTED CLOSED | Existing gates and independent-key v2 defaults must remain closed for grouped custody. No new public enable switch or raw-script consensus prohibition. |

GAP 1 (full grouped chainstate) and GAP 2 (actual grouped CWallet) begin **OPEN**.
Neither will be closed on the strength of a renamed fixture or adapter alone.

## E. Final architecture and modified production files

The existing locked engine is shared from a `script/` component, keeping
research includes as compatibility wrappers and preserving every vector. A
private, nonserializable in-process fixture capability is constructed only in a
guarded test header. No runtime option or deployment can mint it.
An immutable group-only authorization snapshot precedes independent real Script
jobs; native inputs retain genuine checks. Grouped operations bypass both whole
script-cache lookup and insertion. No new cross-transaction signature cache.

The suite exercises actual Chainstate methods using disposable native C++ test utilities,
real coins/undo/database components and caller-owned staging. Actual
CWallet methods and WalletBatch records use the existing native protected PQ
seed and encryption. All positive grouped issuance/signing stays capability-bound
to isolated fixture wallet objects. Ordinary node/wallet/Qt behavior remains
inactive; unknown-witness permissiveness is never described as protection.

Builds use the existing GCC/SDK, three CPUs, about 9.7 GiB RAM and 3.2 GiB free
disk at the start. Concurrency is bounded and earlier evidence and failed
diagnostics are preserved. WO07B is not repeated to inflate transaction counts. No chainparams,
activation, deployed datadir, original-worktree change, push or WO08 work.

| Production files | Purpose |
|---|---|
| `src/script/pqgroup.{h,cpp}` | Same locked engine, shared by production and compatibility wrappers. |
| `src/script/pqgroup_context.{h,cpp}` | Private capability and immutable, owned, group-only authorization snapshot. |
| `src/script/interpreter.{h,cpp}` | Optional typed group result; ordinary checker returns no result and preserves existing dispatch. |
| `src/script/sigcache.h` | Const native precomputation reference for safely owned jobs. |
| `src/validation.{h,cpp}` | Bound CScriptCheck jobs, uncached grouped input entry, private dormant ConnectBlock branch. |
| `src/script/pqgroup_psbt.{h,cpp}` | Strict grouped PSBT inspection and capability overloads for native combine/finalize/extract. |
| `src/wallet/pqgroup.{h,cpp}` | Public issued records, authenticated native-seed derivation, transactional issuance, exact group size model. |
| `src/wallet/wallet.{h,cpp}` | Real ownership, rescan filter, birthday, signing, FillPSBT and unlock hooks; fixture broadcast refusal. |
| `src/wallet/walletdb.{h,cpp}` | Bounded records, actual WalletBatch loading and writing after the native seed. |
| `src/wallet/spend.cpp` | Conservative per-coin selection estimate and exact completed-group fee/weight estimate. |
| `src/CMakeLists.txt`, `src/wallet/CMakeLists.txt` | Compile shared production components without experimental ABI definitions. |

No chainparams, deployment, activation height, network option, environment switch,
RPC, mining algorithm, difficulty, reward, units, maturity, dust, fee rate,
standard weight, witness resource limit, crypto primitive or standalone miner
was changed. The older test-only ABI adapters remain isolated for their previous
regressions; the new actual-library Boost runner uses none of those ABI macros.

## F. Actual consensus integration

`GroupContext::Prepare` receives the real transaction and full vin-ordered spent
outputs acquired by `CheckInputScriptsWithPQGroups` from its actual
`CCoinsViewCache`. It checks witness flags and exact network binding, reconstructs
all groups, checks every ownership commitment, computes complete hashes once and
verifies each group once. Non-v3 inputs are deliberately not certified by this
group-only result. They execute their original native Script path afterward.

The context owns an immutable transaction, spent outputs, flags, genesis,
authorization, membership and native precomputed data. It stores no coins-view,
wallet or caller-owned transaction pointer. An index, witness, program, flags or
spent-output mismatch cannot be converted into an authorized job. There is no
caller-supplied boolean proof. No new long-lived group-signature cache exists.

ConnectBlock acquires inputs through its normal staged view. With a private
fixture capability it enforces the unchanged grouped cost before ML-DSA and
uses the grouped helper and actual script queue. Assume-valid cannot suppress
these fixture checks. Without that capability it takes its previous route.
All inherited header/PoW, merkle, finality, sequence, money, fee, reward, resource,
UTXO staging, block indexing and undo rules still execute.

## G. CScriptCheck and cache evidence

Each grouped job retains a shared const context. Its transaction and native-data
addresses must be exactly the addresses owned by that context, with matching
index, spent output, flags and genesis. It then runs genuine VerifyScript with a
checker derived from the native caching checker. Only the optional v3 dispatch
consumes group authorization; native v2 uses its original implementation.

The grouped entry bypasses **both lookup and insertion** of the legacy whole
script cache. Native signature-cache lifetime still follows the caller contract;
no equivalent lifetime claim is made for a destroyed native signature cache.
The negative suite inserts an apparent success for every bad transaction into
the real legacy cache. Every grouped validation rejects and the inserted entry
remains present, demonstrating that this route neither consumes nor removes it.

The direct queue suite reverses jobs so followers run before anchors, uses two
workers and destroys the original transaction and view before executing jobs.
All jobs retain their complete immutable snapshot. Body/manifest statistics
show one pass each and exactly G group verifications. Native mixed input work is
G+V, replacing the older research path's redundant G+2V checks. Statistics are
group-context measurements, not a new production telemetry interface.

## H. Chainstate and ConnectBlock evidence

`production_path/native_fixture.h` derives from the real `TestingSetup`, with
actual ChainstateManager, disk-backed block index/coins database, block files,
native block assembler, merkle commitments, genuine MercaHash PoW and validation
signals. Networking is disabled. It installs trusted synthetic non-coinbase
funding coins at height 1 in a disposable fixture; this is explicitly a funding
substitute, not a claimed real-network receive flow.

Blocks pass `AcceptBlock`, `ActivateBestChain`, `ConnectTip` and the genuine
`Chainstate::ConnectBlock`; the fixture does not replace ConnectBlock. It checks
input removal, exact output/value/height, persistent `BLOCK_HAVE_UNDO`, and
complete serialized UTXO-map equality after invalidation. Shapes include
1/1, 10/1, 53/1, 100/1, 100/10, 100/100, 1000/1, 1024/1, 256/256 and 4096/4.
The 100/100, 256/256 and 4096/4 shapes exceed the unchanged standard 400,000-WU
transaction policy limit; their block-consensus execution is **not relay proof**.

The failure block contains a valid earlier transaction and a later bad group.
The tip and full persisted coin map remain unchanged and the block is marked
invalid. No partially processed outputs survive. Mixed wallet transactions also
connect through this same actual block path.

## I. Reorganization and persistence

Tests invalidate each valid grouped block, compare the entire restored coin
database, reconsider through the native best-header recalculation and reconnect.
They connect competing spends A and B on separate histories, disconnect B and
restore A, then compare the whole UTXO map. `ResetCoinsViews`, real disk
`InitCoinsDB`, `InitCoinsCache` and `LoadChainTip` reopen persisted coins and
recover the identical map. Undo is the unchanged native undo implementation.

This is a direct component close/reopen, not a process restart/reindex claim for
grouped v3. Ordinary-node v2 restart/reorg regressions are listed separately.
An object capability is neither serialized nor recovered from block or wallet
metadata; a new isolated object must receive explicit fixture attachment.

Actual CWallet notifications move the mixed transaction to `TxStateInactive`
after disconnection. Its unconfirmed spend reservations remain, as in native
wallet behavior. Tests verify restored chain coins, retained wallet state,
native abandonment releasing balances, and reconnection restoring confirmation
and the final 5900/0 balances.

## J. Actual CWallet ownership

Grouped custody is implemented in real CWallet, not the WO05 research facade.
The authoritative protected 32-byte Mercatura PQ master seed remains the only
essential secret. Shared derivation uses its frozen grouped namespace, network,
account, purpose, group and address. Public records contain those locations,
1952-byte key and 32-byte nonce. No expanded private key or separate seed is
persisted. Purpose 0/1/2 and contiguous groups of 64 are checked on loading.

Records are versioned, bounded to 4096 issued addresses, network bound and
HMAC-SHA3-384 authenticated with the native seed under the wallet-local domain
`Mercatura/PQGroupCWalletRecords/v1`. This local metadata MAC is not a protocol
hash change. Unlock authenticates and rederives every record. A public script
index drives real IsMine, balances, coin enumeration, rescan filter and signing.

New issuance updates the real wallet birthday; loading ranges conservatively
sets birthday zero for complete recovery. Actual CreateTransaction selects two
owned grouped coins, generates native change, signs, and conserves input/output
fees. The completed size model measures one anchor per exact key group and
followers, plus independent native witnesses; selection uses a conservative
per-coin anchor bound. It does not claim optimal group-aware coin selection.

There is no new public descriptor language or ordinary receiving API. Existing
native witness-program decoding supplies script recognition and this private
public-record index supplies ownership in isolated CWallet objects.

## K. Encrypted key and wallet database evidence

Actual WalletBatch stores the records in the same wallet database, after loading
the native seed and before loading transactions. A separate versioned presence
record commits in the same transaction; loss of either member of the pair is a
corrupt load, checked before ownership publication. This prevents a missing
range record from restarting issuance at address zero. A normal wallet without the
private capability refuses such fixture records rather than acquiring grouped
ownership. Corrupt version, genesis, MAC, duplicate ranges, nonce derivation and
key ownership fail closed. Bounds are checked before reserving record arrays.

Real SQLite wallets issue 65 addresses and rotate from group 0/address 63 to
group 1/address 0. Native EncryptWallet/Lock/Unlock and existing encrypted-seed
records are used. Tests verify absence of the plaintext native seed record,
valid encrypted seed structure, wrong-password rejection, locked issuance and
signing refusal, restart, backup, restore, identical next address and signing
after recovery. Encrypted records expose no provisional ownership until first
successful native unlock; subsequent locking retains authenticated public
ownership while clearing private material.

The SQLite test denies COMMIT with the real connection's authorizer after the
record write. Native rollback preserves both memory and disk, and the next
successful issuance consumes the expected next address. Another test corrupts
encrypted-wallet metadata and demonstrates that native unlock refuses ownership.
Begin-failure injection through the existing mockable database is separate.
This does not claim power-loss durability or physical secure-erasure assurance.

## L. Grouped signing coordinator

Actual `CWallet::SignTransaction` and `CWallet::FillPSBT` use a private complete
group coordinator only on capability-bearing fixture objects. It collects every
authentic prevout, resolves canonical public records, checks all present fields,
derives matching keys under `cs_wallet`, and signs once per exact group.
Normal signing calls `GetStrongRandBytes`; deterministic randomness is confined
to guarded fixture injection. RAII cleans temporary master/child seed copies and
randomness; the shared KeyPair cleans its expanded secret. Native v2 calls the
unchanged native signer. There is no classical ownership fallback.

All modifications are staged before publication. An injected entropy failure
after a first group signature leaves the caller's PSBT unchanged. Changed body,
authentic prevout mismatch, bad existing signature or incomplete ownership
cannot silently be repaired by overwriting contradictory fields. Already valid
fully signed raw transactions are independently verified and returned unchanged.
Actual FillPSBT also rechecks its live coins snapshot before setting complete or
publishing signatures. A deterministic native test removes a real input during
the entropy callback: the original implementation failed three assertions by
publishing a complete modified PSBT. The corrected path rejects with complete
false and leaves caller bytes unchanged. Raw SignTransaction's explicit coins
argument retains the native caller-authenticated snapshot contract. Every
transaction still needs normal admission validation after signing; this API
cannot promise that the chain will remain unchanged after its last query.
The suite also calls actual `wallet::FundTransaction`, constructs the native
PSBT from its funded transaction, then fills/signs/finalizes/extracts it through
the actual wallet coordinator. One CWallet signs two independent account groups
without collapsing their keys.

The fixture CWallet refuses every mempool/relay submission. No capability can be
requested by an ordinary wallet, RPC, Qt event, file or environment variable.

## M. Actual PSBT integration

The coordinator uses actual PartiallySignedTransaction/PSBTInput/PSBTOutput,
native serialization and real CWallet::FillPSBT. New explicit capability
overloads of FinalizePSBT, FinalizeAndExtractPSBT and CombinePSBTs use the shared
strict inspector; ordinary global functions and RPC behavior are unchanged.
Combine invokes actual native Merge after conflict checking and preserves opaque
proprietary fields that native output/global merging would otherwise drop.

The frozen `MercaWeave` subtypes remain 1 nonce, 2 follower LE32 reference,
3 anchor public key and 4 anchor signature, with no optional keydata or global
manifest field. Canonical CompactSize, cached-key consistency, field lengths,
exact anchor/follower roles, native namespace separation, trusted witness UTXO,
full non-witness transaction identity/index/output, fixed sighash and complete
unsigned transaction body are checked. Present signatures are actually verified;
final witnesses must match verified metadata. Unknown-key and proprietary-field
bounds and contradictory values are checked at global, input and output scope.
Non-opaque output script/derivation metadata must agree across sources; broader
classical/taproot derivation combinations are outside this PQ-only coordinator.

Two actual wallets partially sign an interleaved six-input transaction: two
inputs per grouped owner and one independent native input per owner. Each signs
three inputs; neither partial result extracts. Combining verifies two groups and
two native signatures, finalizes, extracts and connects the actual block.

## N. Mixed witness-v2/v3 qualification

The direct production queue covers v3-only, 1+1, 53+2 and two-groups+one-native
shapes. Native CWallet covers interleaved two groups/two native inputs,
native/grouped outputs, grouped change, partial signers, restoration and real
block accounting. Changing a native signature rejects the whole direct entry,
as does changing any group. A signature made under the native v2 context cannot
authorize the grouped digest. Independent native regression tests never use
fixture grouped capabilities. Neither path introduces ECDSA or Schnorr custody.

## O. Negative security matrix

The committed `production-path-artifacts/security-matrix.csv` maps all 50 WO07C
requirements to execution layers, evidence and restrictions. Thirty-two named
production-entry mutations test key/signature/nonce sizes, commitments, references,
anchor roles, missing/duplicate membership, all transaction/spent bindings,
reordering, late failures and witness shape with poisoned real cache entries.
Additional actual wallet, PSBT, context, limit and boundary tests cover the
remaining safely executable cases. Unexpected acceptance is a failure, never an
expected outcome counted as a pass.

Production allocator exhaustion and process-crash recovery are not established
by the SQLite failure test. The matrix explicitly identifies unavailable or
restricted cases. Inherited permissive unknown-witness consensus is recorded
without claiming a newly enforced v3 spending restriction.

## P. Test-only isolation and activation safety

`IsolatedExecution` has a private constructor and no serialization or normal
factory. Only `ProductionPathTestAccess` in the guarded test header can construct
it or attach it to private Chainstate/CWallet fields. Native libraries compile
without any MercaWeave research/test definition. Only focused test translation
units receive `MERCATURA_MERCAWEAVE_PRODUCTION_PATH_TEST_ONLY`; there is no ABI
layout conditional for this new mechanism.

Negative compile tests reject normal inclusion of that helper, direct capability
construction, direct wallet issuance and direct chainstate context mutation.
The ordinary server has genuine integrated component symbols but no fixture
factory/constructor symbols. Existing source-erasure tests are updated to the
reviewed dormant production checkpoints, preserving their legacy ABI and header
guard checks; they no longer incorrectly demand that authorized production
integration be byte-identical to the pre-integration source.

No activation control, deployment, height, chainparams or runtime flag was added.
Normal mandatory Script still treats unknown v3 permissively; standard policy
rejects it. This is inherited behavior, not MercaWeave protection and not a
reason to send value to v3 outputs. Positive network authorization/propagation
is intentionally unqualified. Ordinary wallet destinations remain native v2.

## Q. Build, unit, functional and sanitizer results

Final machine-readable execution results are collected in
`src/test/mercaweave/production-path-artifacts/qualification.json`.
Configuration results, historical failures and their corrections follow below.

| Configuration | Standalone CTests | Actual-library Boost cases | Status |
|---|---:|---:|---|
| Release | 35/35; 159.55 s | 14/14; 22,476 assertions | PASS |
| Debug, checked STL | 35/35; 509.75 s | 14/14; 22,476 assertions | PASS WITH RESTRICTIONS: instrumented fixture deadline |
| Combined ASan/UBSan | 35/35; 147.07 s | 14/14; 22,476 assertions | PASS WITH RESTRICTIONS: fixture deadline and leak detection excluded |

Each configuration contains 34 existing standalone tests and one new production
context test, plus eight unchanged native Boost cases and six new actual-library
cases. Mutation-loop iterations and UTXO assertions are not counted as distinct
test cases. Baseline executions are not added to final post-change totals.
This is 105 final CTest executions and 42 native Boost-case executions across
three configurations, including three executions of the new context test and
18 executions of the six new native cases. The final cleanup guard affects the
normal CWallet body excluded from the standalone research primitive build;
Release and Debug source-erasure checks were repeated against its final
checkpoint. All actual-library runs include the corrected normal CWallet body.

The initial native Debug run passed 13/14 cases but exhausted the unchanged
mining maturity fixture's 180-second deadline while compilation shared the
two-CPU quota (only 21 blocks had connected). The instrumented runner generates
a copy changing **only** `std::chrono::seconds{180}` to 1800 for that fixture;
the tracked native test, every mining implementation, accepted-block count,
maturity and fee assertion remain unchanged. Exact source hashes and the sole
transformation are recorded in the per-mode mining-budget artifacts. Release
retains the original 180-second budget. Instrumented mining qualification uses
this disclosed extended operational deadline, not a weakened acceptance check.

A focused UBSan probe and the initial full native sanitizer run both found the
failed encrypted-metadata unlock cleanup passing null empty-vector buffers to
`memory_cleanse`. The native sanitizer stack identifies actual CWallet::Unlock.
The fix guards empty buffers and keeps the cleansing primitive unchanged; all
nonempty secrets are still wiped. The failed run and stack trace are retained.

Seven selected ordinary-node scripts passed: reorg (168 s), mempool persistence
(189 s), encrypted-wallet persistence (163 s), PSBT (108 s), rescan including
missing filters/unreadable blocks/pruning (887 s), fee policy (106 s), and invalid
authorization (165 s). The runner's total wall time was 1790 s. A separate final
source follow-up of encrypted-wallet persistence also passed with seed 7 after
the last wallet hardening. It does not increase the seven distinct scripts.

The full native Debug/ASan source builds use GUI off, IPC off and the existing
SDK; the normal Release server/CLI use the existing GUI-enabled configuration.
Native project C/C++ libraries are instrumented in the sanitizer configuration;
external precompiled SDK/system libraries and assembly are not claimed to be
fully instrumented. Debug uses `-O0 -ftrapv -g0 -D_GLIBCXX_ASSERTIONS=1`.
Native and final standalone sanitizer builds use `-O1 -g0`, address/undefined
instrumentation and frame pointers to fit the available disk. The earlier
standalone sanitizer configuration/cache and successful pre-edit baseline are
preserved. Removing debug symbols does not disable sanitizer checks.

Leak detection is **NOT TESTED** (`detect_leaks=0`), following the preserved
ptrace limitation. UBSan halts on errors. No Clang or AFL executable is available;
bounded mutation campaigns are identified as such. No TSAN result is claimed.
Native GUI executables were not rerun after the CWallet ABI extension; ordinary
Qt controls, actual UI compile targets and existing QR/lifetime tests remain in
the standalone regression suite. No GUI activation or receiving flow was added.

`failed-attempts.csv` is the complete unexpected-failure register. It includes
compile/configuration mistakes, the reserved-height undo fixture, the genuine
wallet birthday defect, native reconsideration orchestration, the wallet reorg
expectation and the genuine stale-live-input publication defect. The latter was
reproduced as three failed assertions before the fix, then passed in a six-case
focused run with **21,497/21,497** assertions and the final Release full run.
Original failures remain under the listed `work/wo07c/` paths. The final verdicts
do not conceal these failures or count expected negative rejections as bugs.

## R. Performance and memory measurements

Direct context JSONL records exact bytes, weight, G and inline validation time.
Actual ConnectBlock rows record connection wall time, bytes, weight, persistent
undo and complete UTXO restoration. Runs use this constrained shared worker,
including concurrent compilation, native MercaHash and disk flushing; timings
are not isolated algorithm benchmarks or deployment throughput claims.
Construction, signing and PSBT execution are included in actual wallet case
durations. They are not separated microbenchmark claims. The native runner
records build/test child CPU, wall time and maximum process RSS where collected.

The final Release 14-case native process measured independently with wait4 used
103.42 s wall, 113.86 s user CPU and 5.09 s system CPU, with peak RSS
457,212 KiB (446.50 MiB) and zero major page faults. This peak includes native
mining regressions and real PoW, not just group authorization. Debug/sanitizer
driver RSS fields are explicitly cumulative child high-water marks that include
prior compilers; they are not presented as isolated test RSS. The machine has
three affinity CPUs, a `200000/100000` cgroup CPU quota (two CPUs), and an
8-GiB cgroup memory limit despite the larger visible host memory figure.

| Final native process | Wall seconds | User + system CPU seconds | Peak RSS KiB |
|---|---:|---:|---:|
| Release | 103.42 | 118.95 | 457,212 |
| Debug | 1215.86 | 1623.67 | 468,060 |
| ASan/UBSan | 622.85 | 832.22 | 1,356,832 |

All three independent final profiles recorded zero major page faults. The
longer Debug and sanitizer mining allowances retain genuine MercaHash work and
all 102-block maturity/fee/proof assertions; no fake hashes or reduced maturity
were used. Completed temporary databases, including failure datasets, are
preserved in verified compressed archives below `work/wo07c/` with manifests to
fit the worker disk; diagnostic logs remain at their recorded paths.

Under concurrent worker load the final Release direct-context 100-input cases
measured 9.841 ms for one group, 12.536 ms for ten groups and 120.501 ms for
100 groups. These single measurements include view/context/job preparation and
must not be interpreted as stable throughput or pure ML-DSA microbenchmarks.

The unchanged 100-input/one-group fixture is 13,317 bytes and 25,776 WU;
100/10 is 60,675 bytes/73,134 WU; 100/100 is 534,255 bytes/546,714 WU.
The latter is consensus-fixture evidence only, exceeding normal standard weight.
1024/1 is 87,239 bytes/213,356 WU; 4096/4 is 348,785 bytes/852,758 WU.
Compression is key-group-dependent. Mixed native witnesses keep their full
independent signatures. No aggregate-signature or reduced-byte-fee claim is made.

## S. Fuzz and malformed-input execution

The new direct production context checks 1000 deterministic signature mutations
with seed `0x574f303743435458`. The actual-library PSBT inspector checks 250
deterministic nonce/ownership mutations with seed `0x574f303743505342`.
Prior bounded parser, serialized-transaction, allocation-bound and mutation tests
remain part of the existing standalone suite. These are bounded campaigns,
not coverage-guided libFuzzer/AFL qualification. Sanitizer and leak restrictions
are stated separately in Q; no unavailable tool is counted as passed.

## T. Regression comparison with WO07B

Starting baseline: 34/34 standalone CTests per Release, Debug and combined
ASan/UBSan, reproduced before critical edits. Earlier WO07B campaign counts are
preserved and are not rerun merely to inflate totals. New evidence adds a shared
production snapshot/queue test and actual-library chainstate/CWallet/PSBT suites.
Normal v2 functionals and eight unchanged native wallet/mining Boost cases cover
the affected ordinary behavior. A moved engine is not counted as new crypto.

The baseline's two principal gaps were full grouped ConnectBlock execution and
actual grouped CWallet custody. H–M show the component-level evidence replacing
the old substitute boundaries. Neither close implies ordinary-node activation,
network qualification, public wallet receiving support or WO08 authorization.

## U. Remaining blockers and limitations

Real-node grouped mempool admission, relay, P2P propagation, ordinary-node block
group authorization and production RPC/Qt receiving flows remain untested and
inactive by instruction. No public activation mechanism was added. Component
coins-view reload is not a grouped-node process restart/reindex test. Test
funding uses synthetic trusted coins; CPU-reference mining tests are not GPU
hardware qualification. Group-aware optimal coin selection, public descriptors,
watch-only/public derivation, physical crash recovery, production bad_alloc
injection, TSAN and a deployment security audit are not established here.
Wallet recovery covers authenticated issued ranges included in the backup;
automatic discovery of unknown ranges issued after an old backup is not tested.
The local presence pair detects missing individual metadata records; it is not
an external tamper-proof anchor against replacement of the entire database.

The proposed WO08 remains blocked for deployment/network readiness. A future
separately scoped review may examine the dormant implementation and remaining
qualifications; this work schedules no activation and begins no WO08 work.

## V. Reproduction and seeds

All commands run from `/workspace/Mercatura-MercaWeave-v0.1`, with disposable
outputs below `work/wo07c/`. Existing native Release configuration uses the
local SDK in `work/wo06-tools/qt-sdk/usr` and CMake in
`work/tools/cmake/data/bin`. The native build driver records exact compiler and
link commands in each output directory's `build-commands.json`.

```bash
PKG_CONFIG_PATH="$PWD/work/wo06-tools/qt-sdk/usr/lib/x86_64-linux-gnu/pkgconfig" \
  work/tools/cmake/data/bin/cmake --build work/wo07-native-release \
  --target bitcoind bitcoin-cli test_util -j1
python3 src/test/mercaweave/production_path/build_native.py \
  --build work/wo07-native-release --output work/wo07c/native-release
work/wo07c/native-release/wo07c_native_tests --log_level=test_suite \
  --report_level=detailed --color_output=no -- \
  -testdatadir="$PWD/work/wo07c/native-runtime-release-final"
python3 src/test/mercaweave/production_path/run_native.py --mode debug
python3 src/test/mercaweave/production_path/run_native.py --mode sanitize
python3 src/test/mercaweave/production_path/audit.py \
  --build work/wo07-native-release --output work/wo07c/boundary-audit.json
for mode in release debug sanitize; do
  work/tools/cmake/data/bin/cmake --build "work/build-$mode" -j1
  ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
    work/tools/cmake/data/bin/ctest --test-dir "work/build-$mode" --output-on-failure
done
python3 work/wo07-native-release/test/functional/test_runner.py --jobs=1 \
  --tmpdirprefix="$PWD/work/wo07c/native-functional" \
  --resultsfile="$PWD/work/wo07c/native-functional-results.csv" \
  mercatura_pq_reorg.py mercatura_pq_mempool_persistence.py \
  mercatura_wallet_encryption_persistence.py mercatura_pq_psbt.py \
  mercatura_pq_rescan.py mercatura_fee_policy.py mercatura_pq_invalid_auth.py
```

Use new native testdatadir paths to preserve failed attempts. Functional tests
need loopback network permissions; no public node or private wallet is used.
Fuzz seeds are in S; native fixtures use public fixed seeds and explicit test
randomness. Production signing uses strong randomness. Build/test concurrency
is bounded for the three-CPU worker; selected functionals use one runner job
while instrumented libraries compile. This replaces the order's four-job
default because available resources and concurrent compilation warrant reduction.

## W. Local commits and repository closure

Consensus checkpoint `326fd09` shares the unchanged engine and adds dormant
Script/CScriptCheck/Chainstate integration. Wallet checkpoint `f12e705` adds real
CWallet/WalletBatch/PSBT integration. Full hashes, later test/evidence commits and
final clean-state verification are recorded with final qualification artifacts.
No push, merge, reset, rebase, publication or external message is part of WO07C.

| Commit | Local change |
|---|---|
| `326fd09208ca9673cfb2f09ab8830e74bb5947a6` | Shared engine and dormant consensus path. |
| `f12e70510a833f649db69216f8e6639294dcfec6` | Actual CWallet and PSBT integration. |
| `f353b4efa5f3585fa45131893cae6c11f5a1348d` | Paired metadata presence and live-prevout publication hardening. |
| `9a21f80e24f1f08e25d3d2f32fbd6c97b8bf3f2e` | Actual-library fixtures, boundary audit, qualifier and all-50-case matrix. |
| `9d7d0e4d008f5524231a3a3257258aea57cde757` | Guard empty key buffers in failed grouped-metadata unlock cleanup. |
| `a56d51e7c4ddaa1a28cb71e526b72cdd503a7b46` | Preserve sanitizer evidence and disclose instrumented fixture deadlines. |
| `e64abd0eff6fbcf2ecaf928f1124bca1559723e2` | Make independent final process profiles part of reproducible native runs. |
| `d6de958557ffc62229099696cad4822cd5de2ecd` | Link initial native failures to their preserved logs. |

Final evidence/report-only commit is identified by repository HEAD and the final
response; it cannot embed its own hash in this file. Production source is frozen
at the above checkpoints, with exact source/build/binary hashes in the artifacts.

## X. Explicit readiness verdicts

All final native configuration runs passed. Real-node grouped-v3 readiness is
**NOT TESTED**, regardless of these component outcomes.

| # | Qualification | Verdict | Scope and restriction |
|---:|---|---|---|
| 1 | Pure grouped-v3 authorization | PASS | Locked vectors and shared engine; no cryptographic primitive changes. |
| 2 | Production Script integration | PASS WITH RESTRICTIONS | Actual dormant dispatch under private in-process capability. |
| 3 | CScriptCheck integration | PASS WITH RESTRICTIONS | Actual owned jobs and queues; caller keeps native signature cache alive. |
| 4 | Authorization cache safety | PASS | Grouped full-cache lookup/insertion bypass; exact binding and poisoned-cache rejection. |
| 5 | Chainstate/ConnectBlock | PASS WITH RESTRICTIONS | Genuine complete routines and disk coins/undo; disposable synthetic funding and private capability. |
| 6 | Reorg and persistence | PASS WITH RESTRICTIONS | Actual undo/history/coins-view reopen; no grouped node-process restart/reindex qualification. |
| 7 | Actual CWallet ownership | PASS WITH RESTRICTIONS | Native seed and public records/index; authenticated ranges only; encrypted first load requires unlock. |
| 8 | Actual CWallet grouped signing | PASS WITH RESTRICTIONS | Actual raw signing/FillPSBT; atomic staging and live-prevout recheck; private fixtures only. |
| 9 | Actual encrypted storage/backup | PASS WITH RESTRICTIONS | Native encryption and real WalletBatch/SQLite; no physical crash/power-loss claim. |
| 10 | Actual wallet PSBT | PASS WITH RESTRICTIONS | Native FundTransaction/containers/Merge and explicit finalizer overloads; PQ subset and no ordinary RPC enablement. |
| 11 | Mixed v2/v3 | PASS WITH RESTRICTIONS | Genuine independent native signatures plus exact grouped signatures through actual wallet/block paths; no network activation. |
| 12 | Existing v2 regression safety | PASS WITH RESTRICTIONS | Ordinary-node regressions and native cases; disclosed longer instrumented mining fixture deadline. |
| 13 | Unactivated-network safety | PASS WITH RESTRICTIONS | No public capability or activation control; inherited mandatory unknown-v3 permissiveness remains and is not ownership protection. |
| 14 | Real-node grouped-v3 readiness | NOT TESTED | Explicit activation and positive grouped network scenarios are outside the authorized scope. |
| 15 | Overall WO07C | PASS WITH RESTRICTIONS | Actual isolated components qualified; deployment, network and unavailable allocator/TSAN/leak checks remain outside these results. |

GAP 1 is **CLOSED WITH RESTRICTIONS within isolated genuine Chainstate/ConnectBlock components**;
GAP 2 is **CLOSED WITH RESTRICTIONS within isolated genuine CWallet/WalletBatch/PSBT components**.
Neither closure is an
activated node, ordinary receiving API or production deployment qualification.
