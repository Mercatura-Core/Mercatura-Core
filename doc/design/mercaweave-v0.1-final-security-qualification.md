# MercaWeave v0.1 — WO08 final internal security qualification

2026-11-03, America/Los_Angeles; execution timestamps use host UTC. Research
publication is authorized. MercaWeave activation is not authorized on any network.

## A. Executive assessment

**Internal review: PASS WITH RESTRICTIONS. Deployment readiness: NOT TESTED.**
This is an adversarial internal engineering review by the implementing agent,
using four review perspectives and a separate Python serialization/hash route.
It is not an external professional audit, independent human review, security
proof, certification, or network deployment approval.

Five defects were identified and fixed: allocation after durable address
issuance, allocation in the SQLite batch destructor's normal cleanup, premature
PSBT completion publication on finalizer allocation failure, and an uncleansed
temporary HKDF expansion block, and unsafe exception unwinding during nested
PSBT witness construction in the actual instrumented library. No unauthorized spend or cross-build consensus
divergence was demonstrated. No unresolved critical or high finding remains in
the reviewed scope. Restrictions below prevent that statement from becoming a
claim of activated-network safety.

## B. Repository and baseline

Research: `/workspace/Mercatura-MercaWeave-v0.1`, branch
`research/mercaweave-v0.1`. Clean starting commit:
`43158f4eda51cfa5ec7f2fb2fcd50b68061f7080`.
Original: `/workspace/Mercatura-Core`, branch `work`, unchanged clean commit
`4b88b619fb1b08c7f30112a38eb471880b2a6af4`.
Both configured remotes identify `https://github.com/Mercatura-Core/Mercatura-Core.git`.
The starting branch contained 58 research commits and tracked `project/v0.1`.
Publication uses an explicit research-only destination and may change that
branch's upstream; it does not change the original checkout's branch or source.

Before critical edits, Release, Debug and ASan/UBSan each reproduced **35/35
CTest passes**. Starting evidence hashes cover **89 historical files**. Earlier
reports, vectors, CSVs and JSON evidence remain byte-for-byte intact. The
embedded locked WO01 specification and subsequent work orders were consulted
alongside actual source; historical claims were treated as evidence to verify.

## C. Methodology and threat model

| Perspective | Attacker capability | Central invariant |
|---|---|---|
| Consensus reviewer | Arbitrary transactions, witness bytes, group order, forks, cache contents and execution order | Every v3 member has exact ownership and a canonical, complete transaction-wide group authorization; ordinary validation still enforces other consensus rules. |
| Cryptographic authorization reviewer | Replayed signatures, foreign genesis, alternate contexts, reordered members, coordinator-controlled metadata | One pinned standard ML-DSA-65 signature per exact key group commits to the specified body and complete manifest. |
| Wallet/custody reviewer | Malformed PSBTs, missing or corrupt public records, stale coins, failures during signing or persistence | Native seed authority, authenticated issued ranges, genuine signature checks and atomic publication precede complete/owned state. |
| Operational reviewer | Resource pressure, parallel jobs, unavailable sanitizers, publication mistakes | Failures do not become authorization; limits remain enforced; evidence, activation policy and publication scope are explicit. |

The review traced shared engine → GroupContext → actual Script/CScriptCheck →
CheckInputScriptsWithPQGroups → actual ConnectBlock/DisconnectBlock and coins/undo;
and native CWallet → WalletBatch/SQLite → raw signing/FillPSBT → actual PSBT
containers/Merge/finalizers. Existing research facades were tested separately
and never substituted for these production components. Ordinary-node RPC tests
use disposable native-v2 regtest wallets. All positive grouped-v3 execution is
private, isolated, in process, with synthetic coins and public fixture seeds.

Trusted boundaries remain: the caller authenticates spent outputs and chain
metadata; native flag selection, block validation, maturity and coin state are
not supplied by a signature. A low-level group result never proves complete
mixed-input validation. No adverse timing or memory experiment uses real funds.

## D. Locked-specification traceability and invariant matrix

The complete specification's byte layouts, domains, limits and derivation
remain frozen. No normative ambiguity requiring an amendment was discovered.
The source engine is byte-identical to the WO07C starting engine; HKDF's sole
change cleans an already-copied temporary and preserves every output byte.

| Requirement | Authoritative implementation | Evidence and boundary |
|---|---|---|
| ML-DSA-65; seed/randomness 32, PK 1952, SK 4032, signature 3309 | `crypto/mercatura_mldsa.{c,h}`, dependency pin | Compile-time size/parameter guards, genuine signing/verification and negative lengths; no primitive replacement. |
| Pure FIPS 204 context prefix, exact `Mercatura/PQGroupAuth/v1` context | wrapper `build_prefix`, `SignDigest`, `VerifyDigest` | Wrong native-v2 context rejected; context ≤255; no alternate-prehash acceptance. |
| Genesis32 + exact PK + nonce32 tagged SHA256 ownership | `Commitment`, `Output`, `CheckOwnership` | Every member checked, foreign network/key/nonce reject; separate Python reproduces commitments. |
| Native v3 program exactly OP_3 + push32 | `Reconstruct` | Nonempty scriptSig, wrong length, extra stack data and P2SH wrapping reject. Ordinary unknown-witness semantics are separate. |
| Anchor exactly signature/PK/nonce | `Reconstruct` | Three exact fixed lengths; duplicate exact public keys reject. |
| Follower exactly nonce/absolute LE32 index | `Reconstruct` | Prior anchor only; self/forward/outside/follower references reject. |
| Lowest vin anchor, ascending groups/members, exact key equality | `Reconstruct`, `Manifest` | Interleaved groups, duplicate anchors, reordered/moved/added/removed members and unrelated keys exercised. |
| All groups in one canonical manifest | `Manifest` | CompactSize counts, LE32 anchor/member indexes, full PKs/nonces; exact independent byte reconstruction. |
| Five separately tagged SHA3-384 components | `BodyHashes` | Prevouts, authenticated amounts/scripts, sequences and outputs independently recomputed. |
| Version/locktime, LE64 counts, genesis and all components | `BodyHashes` | Exact Htx; body mutations reject. Neither display-endian hashes nor native-v2 digests substitute. |
| Final version byte 1 + genesis + Htx + Hmanifest + anchor + PK | `Digest` | Separate Python recomputation and context/domain negatives. |
| One signature verification per group; ownership for every member | `Verify`, context factory | Counters, limits, late-invalid groups and poison-cache tests; no externally reusable proof input. |
| Mixed inputs retain independent ordinary authorization | actual CScriptCheck/native checker | Genuine v2 signatures in actual wallet/block cases; GroupContext only defers non-v3 to Script. |
| 4096 v3 inputs / 256 groups / 1024 members | reconstruction and actual ConnectBlock preflight | Exact maxima exercised; immediately greater cases reject before ML-DSA. Wallet/PSBT additionally bound supported input counts. |
| Group cost exactly 4G+N plus existing costs | actual ConnectBlock | Structural accounting before crypto; inherited byte/weight/witness/sigop protections remain. |
| Canonical CompactSize and bounded decoding | inherited serializers, bounded research decoders | Truncation, oversized declarations and malformed replay in all three standard builds; allocation bounds are not chain authorization. |
| Amounts/duplicates and no partially usable authorization | `Basic`, `Verify`, context private constructor | MoneyRange and checked totals, duplicate outpoints, staged factory construction and allocation faults. |
| Existing derivation domains, purpose 0/1/2 and LE32 locations | `Derive`, native seed coordinator | Existing derivation vectors and separation tests; HKDF temporary cleanse has no derivation change. |
| Native PQ seed is the only ownership authority | actual CWallet and native seed storage | No classical-key, xpub or caller label can become grouped ownership. |
| Public record version/network/ranges authenticated before ownership | `PQGroupRecords`, `AuthenticatePQGroups` | MAC/version/network/rederived-record negatives, paired presence markers and ordinary-load refusal. |
| Existing native encryption and backups | CWallet/WalletBatch/SQLite | Actual encryption/lock/reload/backup/restore/passphrase rotation; public range recovery only. |
| Separate PSBT namespace, exact roles and fixed lengths | `script/pqgroup_psbt.cpp` | Native parser/containers; conflicting input/global/output data reject atomically; no supplied global manifest. |
| Complete only after genuine verification and publication | `FillPQGroupPSBT` and finalizers | OOM sweeps, partial multi-party signing, entropy failures and actual reorg during signing. |
| Native v2, MercaHash and current network rules unchanged | unchanged consensus/mining/parameters + bounded wallet fixes | Native regressions, MercaHash permanent vectors, source audit and ordinary capability exclusions. |
| No activation through user operations | private capability, closed Qt model | Negative compilation, ordinary symbols/flags and RPC address/import probe; zero v3 funding/broadcast in ordinary nodes. |

## E. Cryptographic authorization

The existing `mldsa-native` v2.0.0 pin remains
`834a90d5e846ffa1e1611bd24e160bb2e9b86d35`, portable ML-DSA-65,
`MLD_CONFIG_CORE_API_ONLY`. The wrapper formats pure-mode messages as
`00 || context_length || context`; it does not invent transaction hash domains.
Signatures remain ordinary independent signatures across unrelated keys.

Every ownership commitment includes genesis, full public key and address nonce.
Group digest and wallet derivation use their distinct locked namespaces. A
native-v2 signature or foreign genesis cannot substitute. Test-only fixed
randomness is explicit; actual wallet signing calls `GetStrongRandBytes` unless
the private test helper supplies its fixture callback. Missing/failing entropy
does not publish a partially signed transaction. Expanded keys, master/child
copies, randomness and HMAC state have cleanup paths; F04 adds the missing
48-byte HKDF temporary cleanup. This review does not establish constant-time
behavior of every compiler/backend, fault resistance or hardware side channels.

## F. Canonicality and digest binding

Exact bytes determine group membership; account labels or abbreviated keys do
not. The first matching vin is the anchor and every follower names an earlier
anchor directly. The manifest commits every group and member, including groups
owned by other signers. Changing amounts, scripts, outpoints, order, sequence,
version, locktime, outputs, membership, key or nonce invalidates the relevant
ownership/signature checks. No caller manifest is authoritative.

The frozen digest deliberately does not include unrelated legacy scriptSig or
witness bytes. Native/legacy compositions can therefore retain the historical
txid/wtxid malleability and child-transaction disruption described in WO02/03.
That is not an authorization bypass demonstrated here. Signing coordinators
must distinguish signed commitments, full transaction identity and descendants.
No silent digest expansion or protocol amendment was made.

## G. Script, CScriptCheck, jobs and ownership

Actual grouped preparation requires witness flags and matching genesis. It owns
immutable transaction/spent context and native precomputation. Private complete
group proof construction follows successful validation; a bool or diagnostic
Result cannot be passed into jobs as authorization. Jobs check exact transaction
and precomputation identity, flags, genesis, index and spent output. Followers
can run before anchors. Foreign pointers, downgraded flags and stale witness
contexts fail. Native inputs still run the genuine native Script path.

The signature cache owner remains an ordinary caller lifetime contract. Actual
queues retain shared group context but do not independently own every inherited
cache object. Tests retain the cache until workers finish; arbitrary destruction
of a caller-owned native cache during its active jobs is outside the API contract.

## H. Caches and stale context

There is no new grouped signature cache. Fresh preparation recomputes ownership,
body/manifest digests and group signatures. The actual grouped validation entry
bypasses both lookup and insertion in the inherited full-script cache. Poisoned
entries keyed to bad wtxid/flags/genesis remain present yet cannot authorize the
bad transaction. This tests the real cache, not a mock cache-key function.

A transaction-local immutable context is reusable only for its exact owned data;
it does not establish that a coin remains live after a reorg. Coin availability
is revalidated at the actual entrypoint. No unimplemented cache hit/eviction
scheme is counted as qualified. Existing native-only cache behavior is retained.

## I. Actual Chainstate and ConnectBlock

The genuine native routines execute private grouped contexts, native PoW/merkle
handling, Script jobs, sigop/byte limits, UpdateCoins, persistent undo and
notifications. Replayed WO07C cases include 1/1, 10/1, 53/1, 100/1, 100/10,
100/100, 1000/1, 1024/1, 256/256 and 4096/4 inputs/groups. Invalid late groups
leave the complete persistent UTXO snapshot unchanged; competing spends and
coins-view reopening are exercised. Maturity, finality and chain membership
come from those native routines, not the group digest.

These disposable component states have test-installed synthetic coins and a
private in-process capability. They are not an activated node, grouped mempool,
P2P relay, multi-node consensus, process restart or grouped reindex qualification.

## J. Actual CWallet ownership and signing

Only native PQ seed authority and authenticated public derivation records
produce ownership. Actual issuance, grouping across multiple nonce addresses,
64-address rotation, account/purpose separation, real coin selection/funding,
change, accounting, multisigner partial PSBTs and raw signing are exercised.
Ordinary CWallet instances cannot issue these records or load a grouped wallet
as ordinary custody. Private fixture wallets refuse relay/submission even for
native-only transactions; a fixture is not a public wallet feature.

Actual fee estimation serializes the full candidate: anchors 5301 witness bytes,
followers 39, native-v2 witnesses 5268, plus transaction/count/output overhead.
Full-byte fee accounting and native standardness limits remain. Per-coin anchor
estimation is conservative; no globally optimal group-aware coin-selection
algorithm is claimed. F01 now allocates the public ownership index before its
durable metadata commit; publication afterward moves/swaps staged state.

## K. Secrets, encryption and persistence

Native CWallet encryption, lock/unlock, seed envelope, WalletBatch and actual
SQLite remain authoritative. Public grouped records are MAC-authenticated by
the seed and rederived on authenticated unlock. Encrypted first load exposes no
provisional grouped ownership; after authentication, lock may retain the public
index while clearing private seed material. Missing either presence marker or
records is corruption. Whole-database rollback is not prevented by a local pair
of records; it is not an external anti-tampering anchor.

F02 removes a heap allocation from SQLiteBatch::Close's normal destructor path.
Error reporting, fatal database recovery and sustained process-wide exhaustion
still follow inherited behavior. No claim of arbitrary malloc failure recovery,
power-loss certification or complete prevention of memory remnants is made.
No private wallet, raw key, credential, expanded signing key or binary enters the
published evidence. Test seeds/passwords are explicitly public synthetic data.

## L. PSBT and partial signing

The actual native parser, PartiallySignedTransaction, input/output containers,
Merge and explicit private-capability finalizers are used. The `MercaWeave`
identifier has ten ASCII bytes. Subtypes 1/2/3/4 are nonce32, follower LE32,
anchor PK1952 and anchor signature3309, with no extra keydata. Exact roles,
UTXO descriptions, version, default sighash, native namespaces and final
witnesses must agree. Unknown compatible metadata is preserved; conflicting
global/input/output data reject without mutating the result. A valid signature
with invalid ownership metadata is insufficient.

F03 keeps `complete=false` through all finalization allocations, then publishes
the staged PSBT and completion flag. Allocation exceptions still propagate to
the caller; they are not converted into success or a complete unsigned PSBT.
In-memory manually corrupted proprietary-key caches can produce native decoding
exceptions rather than a bool failure; ordinary wire parsing rejects malformed
CompactSize keys. This internal exception contract is not a demonstrated
authorization bypass. Aggregate opaque metadata bounds and hostile huge-PSBT
resource behavior remain a future public-coordinator qualification item.

## M. Reorganization, persistence and recovery

The repeated actual Chainstate cases cover single rollback, conflicting histories,
undo restoration, revalidation and coins database reopening. The added actual
wallet case connects two dependent grouped blocks, invalidates both during the
entropy callback of staged signing, then verifies live-coin refresh rejects the
stale PSBT unchanged and incomplete. Notifications reconcile wallet state;
abandonment releases the rolled-back spend, a wallet reload restores the same
balance, reconsideration restores the exact original UTXO history and balance.

Existing ordinary-node native-v2 reorg/restart/mempool persistence tests are
separate evidence. Grouped reload of components and a duplicated wallet database
are not a positive grouped node-process restart/reindex or physical crash test.
Recovery covers ranges actually contained in authenticated metadata/backups.
Automatic discovery of unknown addresses issued after an old backup is not
qualified; shared-key exposure/linkability and metadata backups remain relevant.

## N. Determinism

Fixed serializers use raw hash bytes, explicit LE32/LE64 integers and canonical
CompactSize, with bounded counts before allocation. No display-endian hash or
unordered iteration enters signed bytes. Maps/sets impose canonical order.
Amounts and cumulative resource arithmetic were reviewed for bounds. Debug
assertions, ASan/UBSan and deterministic corpus replay complement source review.
Same-compiler cross-build equivalence is not cross-endian or cross-compiler
qualification. GCC/G++ 14.2 is the available compiler; no alternate compiler or
architecture was substituted.

## O. Resource and denial-of-service review

Reconstruction and duplicate checks use bounded map/set operations; ownership
and body hashing are linear passes over their inputs/bytes, with one ML-DSA
verification per group. Each member still hashes the entire 1952-byte PK in its
commitment; same-key grouping does not remove that work. Structural errors
reject before signatures; late-invalid signatures can consume all prior group
work. Supported bounds, fixed witness shapes and native block limits remain.

`4G+N` is a signature/ownership allowance, not a complete CPU tariff: output
bytes and all authenticated scripts must also be hashed. Earlier measured large
output and maximum-bound attacks remain evidence, and current maximum actual
ConnectBlock cases were replayed. Existing byte/weight/witness/sigop limits are
mandatory. No theoretical maximum is labelled standard relay eligible; e.g.
100/100 is 534255 bytes and 546714 WU, above 400000-WU standardness.

Owned transaction snapshots and PSBT staging introduce bounded copies; process
profiles measure their practical envelope, not a proof of a universal memory
upper bound. The native seed/public-record wallet maximum is 4096 records.
Metadata authentication/rederivation can be expensive at that maximum and does
not claim constant-time loading. No protocol cost coefficient was changed.

## P. Allocation failures and database failures

Faults are test-executable-only global new/delete interposition, with complete
aligned/sized/nothrow forms, a thread-local countdown and one-shot disarming
before throwing. No node RPC, environment variable, production switch or public
fault injector was added. Native libraries retain their ordinary matching ABI.

The SQLite commit hook arms the next C++ allocation only after metadata staging.
Before fixes, durable records advanced to 1 while ownership entries remained 0.
After moving index allocation, the next fault exposed a second inherited
allocation in noexcept cleanup. With both fixes, issuance returns one record,
one owned script and an identical durable record without another allocation.

Each standard configuration runs 256 context-construction failure points and
512 points each in actual PSBT Finalize and Combine. Actual CWallet FillPSBT
with finalization additionally runs 512 points in Release, ASan/UBSan and TSAN.
Context runs observe 189 injected failures and 67 successes. Exact post-F05
fault/success counts are in qualification.json. Failed operations preserve the caller's PSBT and leave it
incomplete. The original completion bug reproduced at 90 failure points before
F03. Complete counts and fault coverage are recorded, not inferred from a facade.

Global wallet allocation injection is **NOT TESTED in the final DEBUG_LOCKORDER qualification**
(the attempted sweeps failed/terminated):
the attempted Debug sweep terminates at point 25 in the inherited allocation-
using diagnostic/locking environment, and leaves later fixtures poisoned after
the abort. Continuing that injection in the same process is unsafe. The
restriction is compiled into the test only; debug lock diagnostics stay enabled,
and actual Debug signing/concurrency and lock-free PSBT allocation sweeps run.
Release/ASan/TSAN retain full wallet injection. Failed Debug attempts and exact
commands remain evidence; no global diagnostic suppression is used. Source
review identifies allocation-using lock bookkeeping in sync.cpp; the precise
throw/unwind origin of that Debug termination is not claimed proven.

C malloc/realloc, secure allocator internals, every encrypted-wallet write,
arbitrary failure deep inside native ConnectBlock/queue unwinding, and sustained
OOM are not exhaustively injected. Existing native commit/rollback failure tests
and invalid-block staging tests give complementary coverage. This is
**PASS WITH RESTRICTIONS**, not universal allocation-failure recovery.

## Q. Concurrency and ThreadSanitizer

A dedicated GCC 14.2 native production-library build uses
`-O1 -g0 -fsanitize=thread -fno-omit-frame-pointer`, headless matching native ABI,
and GNU thin archives solely to fit disk. The race-free control exits 0; a
deliberate two-thread race exits 66 with a real TSAN data-race diagnostic. Tool
startup success alone is therefore not the basis for coverage.

The actual audit cases run under TSAN without suppression: 2400 CScriptCheck
reads shared across four threads, four-thread actual wallet signing/issuance
under cs_wallet, concurrent local PSBT operations, actual SQLite issuance/close,
and two-block reorganization during staged signing. Threads join before owned
context/cache/wallet destruction. Wallet signing/issuance serializes private
state; context/precomputation data are immutable during worker reads. Native
cs_main protects coins/validation and the existing queue controls completion.

This does not qualify arbitrary concurrent wallet unload, physical database
failure/reopen contention, all inherited race paths, or a grouped multi-node
reorg. Dedicated broader TSAN campaigns remain recommended. The compact artifacts
distinguish deliberately raced control diagnostics from production-case results.

## R. AddressSanitizer, UBSan and leak detection

Release, Debug and combined ASan/UBSan builds exercise the existing 35-test suite
and actual native components. Sanitizer failures are not globally suppressed.
Existing standalone policy.cpp's documented vptr-link exception remains; it does
not disable new grouped/context/wallet checks. Native sanitizer libraries use
`-O1 -g0 -fsanitize=address,undefined -fno-omit-frame-pointer`.

Leak detection was independently retried with both `-fsanitize=leak` and ASan
`detect_leaks=1`, using a deliberate 12345-byte leaked allocation. Both fail with
`LeakSanitizer has encountered a fatal error` / `LeakSanitizer does not work under
ptrace (strace, gdb, etc)`, before a usable leak diagnostic. The configured exit
code 23 is not counted as detecting a leak. **Leak coverage: NOT TESTED.**
ASan/UBSan runs explicitly set `detect_leaks=0`, preserving all other checks.
No leak-free or side-channel-free claim is made; compatible-host leak testing is
a mandatory later security qualification item.

## S. Fuzzing, mutations and preserved failures

The new seed `0x574f303841554449` executes 1024 deterministic mutations per native
configuration: group signature, public key, follower nonce, authenticated amount,
sequence, prevout, output value and locktime. All 1024 reject; no authorization
context is returned. Existing native 32-case poisoned-cache matrix, malformed
wallet metadata, role/conflict PSBT tests, limit tests and historical bounded
parsers replay in the standard suites. These are bounded deterministic campaigns,
not libFuzzer/AFL or a coverage-guided security certification.

Minimal allocation reproductions and logs are preserved under the new artifact
directory. Intermediate aborts from the inherited SQLite destructor were real;
subsequent fixture assertions in that same aborted process were cascading test
cleanup effects, not additional independent findings. A superseded Debug run was
stopped before retesting the fixed source. These are not hidden as final passes.

## T. Cross-build and independent recomputation

`security_audit/independent_vectors.py` imports no project serializer, old oracle,
fixture helper or crypto implementation. It separately parses actual wire
transactions and authenticated spent serialization, reconstructs groups and
manifests, and recomputes ownership SHA256, five SHA3-384 components, Htx,
Hmanifest and every final group digest with Python hashlib/struct. It also
rebuilds the locked patterned fixture: all ten fixed values match, with manifest
2030, body 296 and final 2085 bytes. The patterned key is not a signer.

Four genuine signed vectors (2/1, 7/3, 10/1, 100/10) are compared byte-for-byte
across Release, Debug and ASan/UBSan (and the additional TSAN run). Full wire
signatures, spent records, manifest bytes, hashes and group digests agree.
Exact SHA256 values are in `independent-vectors.json`; timing/RSS/allocator
diagnostics are excluded from consensus equivalence. This independent route does
not independently implement or prove ML-DSA itself. Existing derivation/vector
and malformed-encoding checks remain part of the standard 35-test matrix.

## U. Native witness-v2 regressions

The selected ordinary-node suite uses one job because this worker has a two-CPU
cgroup quota and other instrumented tests; four jobs would add avoidable pressure.
It covers PQ reorg/mempool return, mempool persistence/restart, actual encryption,
PSBT signing, rescan, fee/dust policy and invalid authorization. Actual SQLite
backup/restore and native mixed-input signatures also execute in component tests.
The unchanged native mining cases cover destination persistence, controller work,
genuine PoW blocks, reward/fee accounting, 102-block coinbase maturity, competing
tips, worker interruption and CPU-reference GPU test doubles. They are not GPU
hardware qualification. Exact counts, commands, binary hashes and results are
listed in the qualification artifacts. The entire inherited Bitcoin suite is
not represented as passed.

## V. MercaHash V2 noninterference

No MercaHash V1/V2, PoW, activation, difficulty, emission, mining, reward split,
chainparams, genesis or transport source was modified. Existing permanent
MercaHash V1/V2 vector/reuse/buffer tests run through actual native libraries in
the standard native configurations. Native mining uses genuine existing PoW.
The general SQLite cleanup and HKDF temporary cleanup do not choose a PoW
version or alter a derivation/hash output. No activation option was added or
changed by WO08. Separate MercaHash V2 development remains untouched.

**MercaHash V2 activation remains the higher-priority prerequisite. Its separate
activation must occur, followed by appropriate network stability, before the
user considers separately authorizing MercaWeave testnet activation.** No height,
schedule, deployment bit, flag or activation request is introduced here.

## W. Unactivated-network safety

Ordinary nodes still treat unknown witness versions according to existing
mandatory consensus rules, which may accept an unprotected v3 spend. Standard
policy discourages/rejects that unknown authorization. Neither outcome is
MercaWeave protection; no user should fund a v3 address on any current network.
These inherited consensus semantics were not tightened in a way that forks v2.

The ordinary-wallet probe tests default/legacy/p2sh-segwit/bech32/bech32m receive
requests: all generate owned native witness-v2 addresses; change is also v2.
A private-key-enabled wallet rejects raw-v3 descriptor import. Explicit raw-v3
tracking is possible only in a disabled-private-key blank watch wallet; it is
unsolvable, cannot generate a receive address, and cannot become an active
non-ranged descriptor. The generic descriptor RPC reports `ismine=true` and
`iswatchonly=false` for this explicitly imported script; those generic fields
must not be read as protected PQ custody. `private_keys_enabled=false` and
`solvable=false` establish the probe's boundary. It creates **zero v3 funding
outputs and zero v3 broadcasts**. No user operation constructs the private
IsolatedExecution capability or enables grouped Script authorization.

Ordinary Qt still has a closed network capability and native-v2 receive/send/
change/mining flows; research panels cannot attach an isolated backend in a
normal build. Negative compile tests reject the test header, capability
constructor, private wallet issuance and private Chainstate capability access.
Actual ordinary daemon symbols and compile definitions exclude test factories.
Raw transaction/descriptor tools remain generic tools, not a MercaWeave receive
or protection API. **Activation remains zero on mainnet, every testnet, signet
and regtest.** Positive component tests do not create an activation exception.

## X. Findings, fixes and severity

Severity is based on demonstrated reachability and impact. None of the following
demonstrated an unauthorized spend or a remote critical consensus failure.

| ID | Severity | Trigger / observed impact | Fixed behavior / evidence |
|---|---|---|---|
| F01 | MEDIUM | One-shot C++ allocation fault immediately after actual SQLite issuance commit: durable/in-memory record count 1, public ownership entries 0, caller throws without receiving an address. | Build/copy/insert the staged public index before the transaction; commit then move/swap. Actual SQLite post-commit test now returns consistent owned/durable state. |
| F02 | MEDIUM | After F01, the armed fault reaches inherited SQLiteBatch::Close's fixed-list vector allocation in its noexcept destructor, causing process termination after commit. | Use a fixed std::array for five statement handles. Normal cleanup allocates no list; same post-commit regression passes. No broad SQLite refactor. |
| F03 | MEDIUM | Actual FillPSBT finalization throws at 90 swept points after setting complete=true, leaving the source unsigned/unchanged while its completion output remains true. The API throws; no successful return or spend bypass was shown. | Keep completion local until finalization and staged source publication finish. All 512 swept points preserve false completion on exception; successful finalizations remain complete. |
| F04 | LOW | Source review found HKDF Expand32's 48-byte T1 buffer not explicitly cleansed after copying its 32-byte derived output; caller cleanup of PRK/output did not clean that separate temporary. No extraction exploit was demonstrated. | Explicit existing memory_cleanse after the copy. Locked and real vectors/derivation regressions remain identical. No ML-DSA primitive, pin, domain or derivation change. |
| F05 | HIGH, scope restricted | ASan reports stack-use-after-scope while destroying nested conditional initializer-list witness temporaries after an allocation fails inside actual PSBT inspection. This blocks security qualification until fixed; no external unauthorized spend was demonstrated. | Construct directly in the owned witness using reserve/push_back/emplace_back for grouped and native roles. Same actual allocation corpus passes with ASan checks retained. A smaller standalone original/safe construction probe passes both forms and does not independently reproduce the full-library failure; no compiler root cause is asserted. |

F01–F03 are in `671c17158949499329132fa86e1655013d8f209c`.
F04 is in `700929e22ef913a6868a7cbd1d379e72caf89c62`.
F05 is in `0e4a9c78e0cacb076aa69200d002ee218b2b8b74`.
The initial fixed-list edit accidentally changed SetupSQLStatements rather than
Close; the repeated failing probe exposed that mistake and it was corrected
before either production checkpoint was committed. Intermediate failures are
retained. None of F01–F05 was waived; the unresolved Debug injection coverage
restriction remains explicit in P and the outstanding allocation requirement.

## Y. Performance and verification results

The final machine-readable `qualification.json` and attached native summaries
are authoritative for counts, process wall/user/system time, maximum RSS, source
and binary hashes. Profiles use wait4 for the test child alone; compiler or prior
child high-water marks are not mislabelled as test RSS. Host: Debian 13.6,
GCC/G++ 14.2, CMake 4.4.4, three affinity CPUs, two-CPU quota, 8-GiB memory cgroup.
Builds use jobs=1. Measurements include genuine MercaHash, fixture setup, SQLite,
crypto and concurrent host work; they are not isolated throughput benchmarks.

<!-- WO08_RESULTS_BEGIN -->
| Actual native configuration | Passed cases | Assertions | Wall seconds | User + system CPU seconds | Peak RSS KiB |
|---|---:|---:|---:|---:|---:|
| release | 14/14 | 23945 | 84.89 | 29.43 | 308,440 |
| debug | 14/14 | 23267 | 599.90 | 374.45 | 319,568 |
| sanitize | 14/14 | 23945 | 394.92 | 197.93 | 1,255,576 |
| tsan | 5/14 | 2398 | 209.12 | 79.03 | 862,540 |

Release/Debug/ASan each pass all 14 selected native cases. TSAN selects the
five audit cases and skips nine unrelated cases. Debug assertion totals differ
because its global wallet allocation sweep is explicitly unavailable; all
other actual wallet/PSBT/context/concurrency cases remain enabled.

| Standalone configuration | Baseline | Final | Final wall seconds |
|---|---:|---:|---:|
| release | 35/35 | 35/35 | 110.90 |
| debug | 35/35 | 35/35 | 233.72 |
| sanitize | 35/35 | 35/35 | 189.42 |

The unchanged native mining suite passes **8/8 selected cases, 979 assertions**, in 130.11 s wall, 98.39 s CPU and 455,732 KiB peak RSS. Its original 180-second maturity-fixture deadline is retained; WO08 does not extend it.

The final ordinary-node suite passes **7/7**, runtime **894 seconds**, using
the final native daemon. Earlier seven-script execution also passed and is
preserved separately. Final per-script wall durations:

| Ordinary native-v2 functional | Result | Seconds |
|---|---|---:|
| mercatura_fee_policy.py | PASS | 53 |
| mercatura_pq_invalid_auth.py | PASS | 49 |
| mercatura_pq_mempool_persistence.py | PASS | 88 |
| mercatura_pq_psbt.py | PASS | 107 |
| mercatura_pq_reorg.py | PASS | 70 |
| mercatura_pq_rescan.py | PASS | 337 |
| mercatura_wallet_encryption_persistence.py | PASS | 186 |

All four native vector streams have SHA256 `fe2662594401ee195169302580e466aa3b997eabeed12d2be53b11c9a936655c`.
Each contains four full signed transactions; all independently recomputed
commitments/manifest/body/final hashes match.

Fresh Release shared-engine stage measurements (three warmups, eleven measured
samples; median milliseconds; separate from actual Chainstate):

| Inputs/groups | Shape | Valid verify ms | Malformed ms | Early ownership ms | Late signature ms | Process RSS KiB |
|---|---|---:|---:|---:|---:|---:|
| 100/1 | group_bounds | 0.983 | 0.009 | 0.017 | 0.843 | 10,368 |
| 256/256 | group_bounds | 38.219 | 0.023 | 0.153 | 36.881 | 16,520 |
| 4096/4 | group_bounds | 30.475 | 0.471 | 0.610 | 28.906 | 10,368 |
| 1/1 | large_output_1mib | 4.483 | 0.002 | 0.008 | 4.204 | 10,368 |

These stage children assert zero signature calls for malformed/ownership
failures, one ownership check on early ownership failure, all N checks for
late ownership failure, and all G signature calls for late signature failure.
The 1-MiB output row has cost 5 despite hashing many bytes, reinforcing the
need for independent native byte/resource limits. Stage process RSS includes
fixture and attack copies; it is not per-transaction live allocation.
<!-- WO08_RESULTS_END -->

The 35-test suites include historical core, parsers, native-v2 interoperability,
contexts, resources, wallet/PSBT, Qt capability/QR, reward planning, durable store,
actual queue/undo and isolation checks. The wallet erasure baseline was advanced
to the reviewed F01–F03 checkpoint; all guard-count/macro/symbol/negative-compile
assertions remain. One earlier post-edit 34/35 run detected that expected source
checkpoint change and was rerun after updating the pinned baseline. Historical
passing artifacts were not overwritten. Earlier broad runs precede F04/F05 and
include preserved failures. Final native and all 35-test replays run after all
production fixes, with source and binary hashes in their profiles. Qt isolation
permits only the exact reviewed HKDF cleanup blob and retains every other
protected-path check.

## Z. Outstanding requirements before activation consideration

| Priority | Requirement | Status and rationale |
|---|---|---|
| MANDATORY | Separately governed MercaHash V2 activation, then appropriate stability | Not performed or scheduled here; higher priority than any MercaWeave decision. |
| MANDATORY | Explicit later user authorization for any MercaWeave testnet activation work | Absent; no height is requested. |
| MANDATORY | External independent cryptographic/consensus/wallet audit and remediation | NOT TESTED here; the internal agent review is not a substitute. |
| MANDATORY | Separately scoped activated real-node grouped mempool, relay, P2P, block, multi-node reorg, process restart/reindex and wallet UX qualification | NOT TESTED; cannot be completed by silently activating regtest in this order. |
| MANDATORY | Compatible-host leak detection and memory/error-path qualification | LeakSanitizer is ptrace-incompatible here; no leak-free verdict. |
| MANDATORY | Expanded safe allocation/database-failure qualification | Includes the unresolved Debug diagnostic/injection termination; current bounded checks exclude C malloc, sustained OOM, every encryption write and arbitrary deep Chainstate failures. |
| MANDATORY | Operational limits/calibration and hostile resource qualification on intended node configurations | 4G+N alone does not price hashing/copying; existing byte/weight/witness limits must remain. |
| MANDATORY | Separate mainnet security, stability, operator and deployment decision | Mainnet is not authorized or qualified by this research publication. |
| RECOMMENDED | Broader TSAN unload/reopen/real SQLite contention and long-running fork/signing races | Current concrete concurrency cases pass but do not cover every lifecycle. |
| RECOMMENDED | Coverage-guided fuzzing, independent compiler/architecture, side-channel and hardware review | Bounded seeded GCC runs have narrower coverage. |
| RECOMMENDED | Public grouped wallet/PSBT cumulative metadata budgets, recovery discovery, fee/coin-selection usability review | Public flows remain unavailable; authenticated known ranges only. |
| OPTIONAL | Signature-cache optimization and optimal grouped coin selection | No new cache required for correctness; optimize only after an independently reviewed design. |
| OPTIONAL | Hardware-specific GPU benchmarks | CPU-reference device tests establish no hardware throughput claim. |

These classifications identify future gates; they do not authorize activation,
change frozen rules or turn research publication into deployment readiness.

## AA. Explicit verdicts and audiences

| # | Qualification | Verdict | Scope |
|---:|---|---|---|
| 1 | ML-DSA cryptographic integration | PASS WITH RESTRICTIONS | Pinned primitive, genuine signatures and contexts; external cryptanalysis/side channels not established. |
| 2 | Group canonicality | PASS | Exact witness/group/manifest rules and negatives. |
| 3 | Digest completeness | PASS | Frozen intended signing scope, independent exact hashes; malleability boundaries disclosed. |
| 4 | Network replay separation | PASS | Genesis binds ownership, body and final digest. |
| 5 | Witness-v3 structure | PASS | Fixed roles/lengths, references and wrapping checks. |
| 6 | Script integration | PASS WITH RESTRICTIONS | Actual private-capability dispatch; no public activation. |
| 7 | CScriptCheck / parallel jobs | PASS WITH RESTRICTIONS | Owned context, genuine queue/jobs; native cache lifetime remains caller-owned. |
| 8 | Cache safety | PASS | No grouped cache, actual full-cache bypass and poison rejection. |
| 9 | Chainstate / ConnectBlock | PASS WITH RESTRICTIONS | Genuine isolated components, synthetic funding. |
| 10 | Reorg / undo | PASS WITH RESTRICTIONS | Exact persistent snapshots and two-block staged-signing reorg; no activated multi-node test. |
| 11 | Wallet ownership | PASS WITH RESTRICTIONS | Actual authenticated native-seed ranges only. |
| 12 | Wallet signing | PASS WITH RESTRICTIONS | Genuine CWallet raw/PSBT signer, atomic faults and live-coin refresh. |
| 13 | Encryption / persistence | PASS WITH RESTRICTIONS | Actual native encryption/SQLite; no physical crash certification. |
| 14 | Backup / recovery | PASS WITH RESTRICTIONS | Actual backup/restore, known metadata ranges; no unknown-range discovery claim. |
| 15 | PSBT | PASS WITH RESTRICTIONS | Supported PQ subset and native containers; no ordinary grouped RPC enablement. |
| 16 | Mixed v2/v3 | PASS WITH RESTRICTIONS | Genuine independent native/group signatures in isolated actual paths. |
| 17 | DoS / resources | PASS WITH RESTRICTIONS | Bounded tests/current limits; no complete CPU-cost proof. |
| 18 | Allocation failure | PASS WITH RESTRICTIONS | Bounded C++/SQLite probes; Debug wallet injection and other exclusions in P. |
| 19 | Concurrency / TSAN | PASS WITH RESTRICTIONS | Dedicated native instrumented execution and positive control; lifecycle exclusions in Q. |
| 20 | Memory safety / leaks | PASS WITH RESTRICTIONS | ASan/UBSan pass; leak detection explicitly NOT TESTED. |
| 21 | Determinism | PASS WITH RESTRICTIONS | Byte-identical same-GCC builds and independent hash route; no cross-architecture claim. |
| 22 | Native witness-v2 regression | PASS WITH RESTRICTIONS | Selected actual native/node/mining cases, not the entire inherited suite. |
| 23 | Unactivated-network safety | PASS WITH RESTRICTIONS | Zero activation, private interfaces and native receive defaults; generic unknown-v3 semantics remain unsafe for funding. |
| 24 | MercaHash V2 noninterference | PASS | Sources/activation unchanged and permanent vectors/native PoW regressions. |
| 25 | Real-node grouped-v3 qualification | NOT TESTED | Explicitly inactive on every network. |
| 26 | Overall WO08 | PASS WITH RESTRICTIONS | Internal research qualification/publication only; outstanding requirements remain. |
| 27 | Network activation readiness | NOT TESTED | Not ready for deployment; user authorization and preceding gates absent. |

| Audience / readiness | Verdict | Meaning |
|---|---|---|
| A. Internal security review | PASS WITH RESTRICTIONS | Reproducible internal challenge and fixes completed within stated scope. |
| B. Dormant research-code retention | PASS WITH RESTRICTIONS | Qualified as inactive code with documented boundaries. |
| C. Research publication | PASS WITH RESTRICTIONS | Safe-content/remote/history/CI gates required; publication receipt records actual outcome. |
| D. External independent audit | NOT TESTED | No third-party auditor engaged or certification claimed. |
| E. Activated-network qualification | NOT TESTED | Grouped network tests remain outstanding and unauthorized. |
| F. Deployment | NOT TESTED | **NOT READY**; no testnet/mainnet deployment approval. |

## AB. Reproduction, commands and evidence

Run from the research root. Dependencies are the existing local native SDK and
CMake; no system installation, public node, GPU device or real wallet is needed.

```bash
python3 src/test/mercaweave/security_audit/inventory.py
python3 src/test/mercaweave/security_audit/tool_probes.py
python3 src/test/mercaweave/security_audit/build_tsan.py
python3 src/test/mercaweave/security_audit/run_native.py --mode release
python3 src/test/mercaweave/security_audit/run_native.py --mode debug
python3 src/test/mercaweave/security_audit/run_native.py --mode sanitize
python3 src/test/mercaweave/security_audit/run_native.py --mode tsan --suite=pqgroup_final_security
```

Use fresh `--label` values to retain prior datasets. Test runners are not normal
node executables and do not mine a public chain. Exact native compile/link flags
and libraries are preserved per configuration. Test access is the single guarded
production-path friend helper, not the old changed-ABI research library.

```bash
for mode in release debug sanitize; do
  work/tools/cmake/data/bin/cmake --build "work/build-$mode" -j1
  ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
    work/tools/cmake/data/bin/ctest --test-dir "work/build-$mode" --output-on-failure
done
python3 src/test/mercaweave/security_audit/independent_vectors.py \
  --logs work/wo08/final-release.log work/wo08/final-debug.log \
         work/wo08/final-sanitize.log work/wo08/final-tsan.log \
  --output work/wo08/independent-vectors.json
python3 src/test/mercaweave/security_audit/boundary_audit.py \
  --build work/wo07-native-release --output work/wo08/boundary-audit.json
python3 src/test/mercaweave/security_audit/publication_audit.py \
  --output work/wo08/publication-audit.json
python3 src/test/mercaweave/security_audit/collect.py
```

Native functional exact commands, the inactive-wallet probe command and native
mining build/test commands are in the associated evidence. Loopback networking
is required for disposable ordinary-node functionals. Historical data and failed
datasets are retained in verified compressed archives under ignored `work/` to
fit disk. Hash manifests verify archived bytes before removing expanded copies.
No wallet database or executable is copied into published evidence.
New copied text evidence normalizes LF endings, trailing whitespace and terminal
blank lines; hashes of copied pre-normalization text and untouched raw work logs
are retained. No historical evidence file is normalized or overwritten.

## AC. Publication gate and GitHub procedure

The user explicitly authorizes publication, superseding earlier orders' no-push
instructions. Only `origin` at the expected GitHub URL and
`refs/heads/research/mercaweave-v0.1` are authorized. No force push, tags, release,
PR, merge, rebase, deployment, production-branch update or activation is included.

The research-only history and current files are scanned for private-key
envelopes, credential patterns, credential-bearing URLs, binary/wallet magic and
sensitive extensions. The scan includes distinct superseded/deleted history
blobs. Public fixture seeds/passwords and protocol test vectors are reviewed as
such, not disguised real secrets. Pattern scanning is not a universal proof of
secret absence; the inventory supports review. Original checkout and all 89
historical evidence hashes must still match.

All five existing GitHub workflows and their local composite actions were
reviewed. `ci.yml` runs all-branch push build/test/lint; it has no release or
deployment job. GPU/native-mining/PQ-integration push allowlists exclude this
research branch. macOS packaging requires manual dispatch. Default-branch cache
writes are gated; no workflow file is modified. Mutable third-party action tags
remain an inherited supply-chain consideration, not an observed unsafe research
deployment trigger.

After final source regressions/report and clean local commits, fetch metadata
from the expected origin without changing source. If the research branch
exists, require its remote commit to be an ancestor of local HEAD; divergence
blocks publication without automatic merge/rebase or force. Then run exactly:

```bash
git push -u origin HEAD:refs/heads/research/mercaweave-v0.1
git ls-remote origin refs/heads/research/mercaweave-v0.1
```

The post-publication receipt records exact local/remote full SHA, push exit,
branch URL and both clean worktree checks. Authentication/network or remote
divergence blocks must be reported honestly; a local clean commit is not a
successful push. CI triggered after publication is not claimed passed here.

## AD. Final commits and closure record

The full starting and production-fix commits are recorded in B/X. The final
containing audit/evidence commit cannot contain its own hash without recursive
hashing. Resolve it with `git rev-parse HEAD` and the post-publication receipt;
the final out-of-tree handoff `/workspace/WO08-final-summary.md` records the exact
full final SHA and all 37 requested closure items without modifying either
checkout after publication. Research branch URL:
`https://github.com/Mercatura-Core/Mercatura-Core/tree/research/mercaweave-v0.1`.

## AE. Final worktrees and activation policy

Final closure verifies both worktrees clean, original `work` branch and HEAD
unchanged, all historical evidence preserved, expected research branch selected,
all research commits reachable, and the remote SHA equal to the final local SHA
if publication succeeds. Git metadata fetch/upstream updates do not constitute
original source changes. The receipt records actual publication status.

**No MercaWeave activation exists or was added on any network, including regtest.
MercaHash V2 activation and subsequent stability remain the higher-priority
prerequisites. Any later MercaWeave testnet decision requires separate explicit
user authorization after the outstanding security/network qualifications.**
