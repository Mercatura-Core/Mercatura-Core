# MercaWeave v0.1 — Work Order 03 consensus-readiness checkpoint

Date: 2026-11-02, America/Los_Angeles (host UTC measurements are November 3).
Scope: offline research, with the locked witness, digest, tags, derivation and
`4G+N` rules preserved. No production consensus or activation work occurred.

## A. Repository and baseline reproduction

Branch: `research/mercaweave-v0.1`.
Research worktree: `/workspace/Mercatura-MercaWeave-v0.1`.
Starting HEAD: `3efc0e0501ebfcd4d1eed48e63fa1c06ec00fa69`.
Pinned project base and original clean checkout HEAD:
`4b88b619fb1b08c7f30112a38eb471880b2a6af4`, branch `work`,
`/workspace/Mercatura-Core`. The original worktree was inspected read-only.

Read WO01 source-audit/validation and WO02 security-qualification reports and
audited the actual verifier, native interpreter, caches and policy paths.
Rebuilt the starting research tree and reproduced WO02 **7/7 CTest passing**
before making changes. All prior reports and measurement artifacts are preserved.

Validated implementation commits:

```text
6a78dfbb45837f32dbd1e58fad997c0095c80280 Bind complete immutable authorization to flags and per-input proofs
91825b69b051acaeb74d0b9ea047b5ff89eb4882 Account research block work with checked cumulative budgets
7ed2b23827095e836cf3090f8f98dab78cbbacad Measure block and hostile workloads and prove mixed-input child disruption
a3c35fdb50cbf7410cdbead6bba0acd45c3fbcf5 Support Debug assertions and standalone sanitized policy linking
da434cf532370bee8cb90d86fcf690ade144ba74 Emit readiness measurement CSV with repository line endings
```

Current tested C++ implementation HEAD is `a3c35fdb50cbf7410cdbead6bba0acd45c3fbcf5`.
Pre-evidence HEAD is `da434cf532370bee8cb90d86fcf690ade144ba74`, adding only the
validated LF CSV generator fix; no compiled or measured C++ code changes.
A final evidence/documentation commit contains this report, the README update,
the cross-build comparison runner and text artifacts. Its containing commit is
the final checkpoint HEAD, obtainable without a self-referential hash:

```bash
git rev-parse HEAD
git log --format='%H %s' 3efc0e0..HEAD
git status --porcelain=v1
git -C /workspace/Mercatura-Core status --porcelain=v1
```

## B. Threat model, authorization invariants and cache safety

Added hostile validation-entrypoint callers, absent/incomplete authorization,
stale full-wire proofs, flag downgrade/reuse, arbitrary input order and concurrent
proof consumption. Also considered cumulative resource arithmetic, cross-tx
outpoint reuse, invalid work charged as free, and coordinator-induced descendant
failure despite unchanged signed ownership.

`research::Request` is immutable copied data, not a proof. Its spent outputs
must come from an authenticated UTXO view in absolute vin order. `Capture`
retains full transaction/witness, spent amounts/scripts, genesis and exact flags.
`Prepare` requires the mandatory verification flags, reconstructs the actual
mapping, checks every ownership commitment, canonical manifest, component/body
hashes and group signature, and genuinely executes native non-v3 verification.
Only complete success invokes the private `ValidatedContext` constructor.
It accepts no caller bool, Result, group proof or callback. Native computation
is deferred until an actual non-v3 input needs it, then performed once/session.

The immutable record retains canonical groups/members/anchors, ownership proof,
manifest bytes/hash, five components plus Htx, every verified group digest,
absolute-input evidence, txid and wtxid. An input's evidence is private data
produced by completed verification; there is no partially usable group record.
Its local FORMAT=1 is an API semantic version, not a new signed domain or field.

`Bind(request, context)` checks exact flags and full serialized/spent/genesis
equality once. Pointer identity is safe only because the stored request owns
const copies. `InputChecks::Check(index)` then reads immutable evidence in O(1)
and rejects out-of-range indexes. Any order, including followers before anchors,
has the same result. `CheckInput` is a convenience that repeats full binding;
large queues must use Bind once to avoid quadratic comparison work. Without
context, native v3 fails closed and actual non-v3 uses native Script verification.
An invalid v3 transaction cannot gain overall acceptance from a successful
individual v2 check. P2SH wrapping remains rejected by reconstruction/adapter.

The old callback-taking low-level `Verify` remains a trusted research utility;
its public Result is diagnostic and cannot mint the new proof. The new factory
uses only its own genuine native callback and never accepts an external record.
An attacker able to invent supposedly authenticated UTXOs has already crossed
the caller's trust boundary; cryptographic verification cannot prove existence.

**Safe uncached baseline.** No v3 signature cache or cross-request full-result
cache was introduced. Every fresh Prepare rechecks ownership, digests, group
signatures and native authorizations. Identical immutable bound reads reuse only
the completed transaction-local record. Tests show that a previously good
digest/PK/signature cannot bypass changed ownership, flags, witness, spent data,
genesis, mapping or added followers. Losing a context requires complete fresh
verification, with the same verdict/counters. No claim of exercising an actual
v3 cache hit/eviction algorithm is made because none exists.

Source audit: `src/script/sigcache.cpp` caches only ECDSA/Schnorr signature
checks with salted digest/PK/signature entries. Native PQ checking calls the
wrapper directly. `src/validation.cpp::CheckInputScripts`' full Script cache
uses a process-salted wtxid, flags, PQ-context presence and optional genesis.
It explicitly assumes the supplied coin view is authentic. It does not directly
key arbitrary caller-supplied spent outputs, and must not be reused as if it did.
No production cache is called or changed by this research baseline.

Future cache work requires separate reviewed version/scheme/context semantics,
complete `(digest, PK, signature)` signature keys, fresh per-input ownership
before any signature hit, and a distinct full-authorization key binding exact
witness, flags, genesis, vin order, authenticated spent records and manifest.
UTXO existence/reorg validity still belongs to the caller, even with a matching
record. No new cryptographic cache tag is specified or implemented here.

No reproducible unauthorized spend or consensus divergence was found within
these tests. No locked-rule amendment is required by a demonstrated attack.
This is bounded local evidence and a self-review, not a security proof.

## C. Research source changes and APIs

- `auth_context.{h,cpp}`, `context_fixtures.h`, `context_tests.cpp`: immutable
  snapshots, private complete proof factory, exact binding and per-input adapter.
- `mercaweave.{h,cpp}`: retain already-validated mapping/manifest/hashes/digests
  in Authorization; add hash-pass diagnostics. Build the canonical manifest
  once per Verify instead of constructing it again while hashing. Signed bytes
  and acceptance rules are unchanged.
- `block_work.{h,cpp}`, `block_tests.cpp`: checked Add/Multiply; resource Estimate;
  atomic, fail-closed Ledger; VerifyWorkload receipts and real CBlock serialization.
- `readiness_bench.cpp`, `allocation_probe.{h,cpp}`, `measure_readiness.py`:
  real signed block/single-tx workloads, adversarial cases, time/CPU/RSS/allocation
  diagnostics, reproducible CSV and source/binary provenance.
- `malleability_tests.cpp`: real authorization and child UTXO lookup, signature
  randomization, actual native Script and production policy classification.
- `check_readiness_determinism.py`: compare vectors/proofs and 54 verdict/counter
  cases across existing Release/Debug/sanitizer builds.
- Research `CMakeLists.txt`, README, this report and `readiness-artifacts/` only.
  Production sources are linked unmodified; normal production CMake never
  includes this standalone project. No secret key or binary artifact is committed.

Public APIs are documented in the headers/README. Workload.authorized is a
summary, not a mintable per-input authorization. BudgetError/ContextError and
optional core Error preserve failure reasons; failed contexts have no proof.

## D. Test results, sanitizers, fuzzing and determinism

Exact local tools: `work/tools/cmake/data/bin/{cmake,ctest}` 4.4.4. From the
research root (the same configure/build/test commands reproduce the checkpoint):

```bash
work/tools/cmake/data/bin/cmake -S src/test/mercaweave -B work/build-release -DCMAKE_BUILD_TYPE=Release
work/tools/cmake/data/bin/cmake --build work/build-release -j4
work/tools/cmake/data/bin/ctest --test-dir work/build-release --output-on-failure
work/tools/cmake/data/bin/cmake -S src/test/mercaweave -B work/build-debug -DCMAKE_BUILD_TYPE=Debug
work/tools/cmake/data/bin/cmake --build work/build-debug -j2
work/tools/cmake/data/bin/ctest --test-dir work/build-debug --output-on-failure
work/tools/cmake/data/bin/cmake -S src/test/mercaweave -B work/build-sanitize -DCMAKE_BUILD_TYPE=RelWithDebInfo -DMERCAWEAVE_SANITIZERS=ON
work/tools/cmake/data/bin/cmake --build work/build-sanitize -j2
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 work/tools/cmake/data/bin/ctest --test-dir work/build-sanitize --output-on-failure
python3 src/test/mercaweave/measure_readiness.py --binary work/build-release/mercaweave_readiness_bench --output-dir work/readiness-reproduction
python3 src/test/mercaweave/check_readiness_determinism.py
```

| Configuration | Result | Recorded CTest wall time |
|---|---|---:|
| WO02 starting checkpoint | 7/7 pass | about 1.6 s |
| Release | 11/11 pass | 2.19 s |
| Debug | 11/11 pass | 11.48 s |
| Combined AddressSanitizer/UndefinedBehaviorSanitizer | 11/11 pass | 7.39 s |

New tests: fail_closed_context (4052 assertions, 11 named mismatches, 2000 seeded
mixed-context mutations); checked_block_work (5026 assertions, seven overflow
boundaries, 5000 seeded counter cases); block_measurement_smoke; and
malleability_children_and_policy (41 assertions). Existing targets remain:
locked Python vectors, core 9855 assertions, cross-language vectors, native v2
(91 assertions), bounded parsers (19957 checks/12000 mutations) and hostile
followers/context (20067 checks/10000 illegal-reference mutations), plus resource
qualification smoke. The named 29,000 mutation cases include 7000 added in WO03;
original WO01 random/truncation cases also remain. This is not coverage-guided fuzzing.

Four workers synchronize on a barrier, then perform 100 rounds of seven permuted
indexed reads each: 2800 matching decisions, plus invalid-index rejections.
No sleep/timing-dependent ordering or shared mutation is used. Binding tests
cover tx body, witness, amounts, scripts, genesis, flags, references, native
signature, partial group failure, changed ownership and added follower.

Release/Debug/sanitizer vector dumps are byte-identical, SHA256
`17306fd48a53acda5a6425f909904e35da8beac37f25b709919ee68df5e5a6f7`.
The deterministic malleability JSONL is also identical, SHA256
`d8658e03b6424addac83e692526c9176cb325075d8eb70ce3c419fa5a1bfbc09`.
54 additional comparisons match acceptance/rejection reasons, serialized sizes,
resource charges and actual cryptographic/hash counters. These include all 14
invalid cases and eight single-tx cases in Debug and sanitizer configurations,
and all ten full workloads under sanitizers. Timing/allocator/RSS variation is
excluded deliberately. Details: `readiness-artifacts/determinism-checks.json`.

Reviewed fixed-endian serializers/ReadLE32/WriteLE32, bounded lengths before
allocation, explicit uint32 input indexing only after bounds, checked cumulative
unsigned arithmetic and subtraction preconditions, MoneyRange-before-add signed
amount arithmetic, initialized arrays and immutable worker-owned proof lifetimes.
Canonical CompactSize is differentially tested against inherited serialization.
No undefined evaluation-order reliance was identified. GCC14.2 is the sole
compiler (`g++` and `g++-14` are the same toolchain); no Clang, libFuzzer, AFL,
honggfuzz or Valgrind is installed. No toolchain was installed/replaced. Cross-endian,
alternate compiler, TSan and true coverage-guided fuzz evidence remain absent.

One research build defect was fixed coherently: -O0 policy linking required
real production assertion/version support. Debug now links those unmodified
objects with diagnostic-only generated metadata from existing project constants.
GCC's sanitizer registration retains vptr RTTI references for otherwise discarded
node/CCoinsView functions in production policy.cpp. Only that production TU uses
`-fno-sanitize=vptr`; all other ASan/UBSan checks and every check on new research
code remain enabled. This exception does not apply to the group/context/ledger code.

LeakSanitizer was explicitly retried:

```bash
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 work/build-sanitize/mercaweave_block_tests
```

It exits 1: `LeakSanitizer has encountered a fatal error` and
`LeakSanitizer does not work under ptrace (strace, gdb, etc)`.
**LeakSanitizer is unavailable, not passed.** No leak-free claim is made.

Golden `vectors.json` SHA256 remains
`ea4978206307ac21d4f544b05e6da3543e094c9a1c4bd97c44f90fad4420c49c`;
WO01 `benchmarks.csv` remains
`0896a259684d85b4df92299d535c7f292ab237fefbb0a3114c4c8cd7fe83ac28`.
All other WO01/02 tracked artifacts compare unchanged with starting HEAD.

## E. Resource accounting and performance

The simulator is not a production block validator. It uses real signatures,
native non-v3 verification, unique synthetic outpoints, complete proofs and
genuine inherited CBlock serialization with a synthetic header/coinbase. It
does not assert PoW, merkle, maturity, chain membership, finality or mempool
acceptance. Fee sizes are full serialized transaction bytes; no witness discount.
The current fee helper's optional sigop adjustment is separate; at default 20
bytes/sigop even including proposed group cost, full bytes dominate every sample.

Reference budgets are explicit: 4,194,304 full block bytes, 16,384 witness items,
80,000 existing-plus-group sigop cost. These are a fixed research snapshot of
the existing initial-capacity constants, not a claim about a live network's
configuration. Inherited weight is measured, with no default weight cap: current
capacity accounting uses full bytes. A separate optional weight cap is boundary
tested. The transaction standard-weight limit is not applied to these block
authentication experiments; large independent-key cases may exceed relay policy.

Estimate uses checked additions/multiplications, actual legacy/P2SH sigop counts
and production CountWitnessSigOps for native cost. The proposed group cost remains
exactly `4G+N`. Structurally identifiable potential anchors are charged even if
duplicate/invalid; malformed missing anchors do not get fictitious signature work.
All structurally measurable transactions reserve resources before authorization.
Early or late rejection retains the reservation. A denied/overflowing reservation
is atomic and poisons the ledger; a successful prefix never authorizes a denied
workload. Duplicate cross-tx outpoints reject after charging that attempt. Missing
request/spent context fails the whole workload; it cannot skip to an accepted
summary. Raw bounded-decoder failures occur before any transaction is admitted.

Measurements: Debian13.6, GCC14.2, x86_64 AMD EPYC9V74 VM, three visible CPUs,
about 9.7 GiB RAM, no swap. C++ Release -O3 with -UNDEBUG; portable pinned ML-DSA.
Fresh process/case, fixtures and signing outside timing, one warmup, seven wall
samples (median/max), mean per-run user/system CPU, separate untimed ordinary
C++ new/new[] requested allocation volume. RSS is whole-process high-water
including fixture/signing and Python pre-exec floor; not transaction live memory.
Malloc/realloc/aligned new are excluded from the allocation probe. No Ryzen,
cold-cache or multi-host performance claim is made.

| Workload | TXs | All inputs | Groups | Full block bytes | Actual group+native verifies | 4G+N | Median ms | Peak RSS KiB |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Many small 1/1 | 777 | 777 | 777 | 4193615 | 777+0 | 3885 | 131.319 | 11264 |
| 53/1 consolidation | 153 | 8109 | 153 | 1462365 | 153+0 | 8721 | 90.610 | 11392 |
| 100/1 consolidation | 81 | 8100 | 81 | 1078821 | 81+0 | 8424 | 80.750 | 11392 |
| 100 independent keys | 7 | 700 | 700 | 3739929 | 700+0 | 3500 | 119.946 | 11392 |
| Maximum 256 groups | 3 | 768 | 768 | 4102971 | 768+0 | 3840 | 134.524 | 14672 |
| Maximum 1024 members | 7 | 7168 | 7 | 610817 | 7+0 | 7196 | 57.826 | 11392 |
| Maximum 4096 v3 inputs/4 groups | 1 | 4096 | 4 | 348929 | 4+0 | 4112 | 34.321 | 11392 |
| Mixed 53 v3 + 2 v2 per TX | 147 | 8085 | 147 | 2965869 | 147+294 | 8379 | 134.456 | 11520 |
| Early malformed anchor | 81 | 8100 | 0 | 1076148 | 0+0 | 8100 | 2.762 | 11520 |
| Invalid last signature /256 | 3 | 768 | 768 | 4102971 | 768+0 | 3840 | 107.784 | 14100 |

Mixed cost adds 1176 existing native cost to 8379 proposed cost (9555 combined).
Byte budget binds small/independent/max-group samples; witness items bind the
consolidation/member/input/mixed samples. Binding_budget CSV values: Bytes=2,
WitnessItems=4. None reaches the sigop cap. For 4096/4, each TX uses 8196 items;
two exceed the reference witness-item budget despite fitting full bytes.

Single-tx complete-context medians in the same harness (including ledger/adapter,
excluding signing): 1/1 0.151 ms; 53/1 0.538; 100/1 0.880; 100/100 15.530;
256/256 40.975; 1024/1 7.761; 4096/4 32.540; mixed53+2 1.029. Exact full bytes,
weight, fee bytes, CPU, allocation volume and RSS are in transaction-benchmarks.csv.
Its block_bytes includes the synthetic envelope; tx_fee_full_bytes excludes it.

Every valid v3 input hashes its 1952-byte PK again for its distinct ownership
commitment. Ownership-only PK volume is 7,995,392 bytes for 4096/4 and 15,828,768
for the packed 53/1 sample; these are not complete hash-input byte counts. A
4096/4 transaction additionally hashes 340307 component/body payload bytes and
155293 manifest payload bytes. The context counter proves one body pass and
one manifest hash per TX, not one per input/group. Final digests occur once per
attempted group. Mixed v2 requires its own native precomputation under different
domains; v3 hashes cannot replace it. No redundant per-input full-TX hashing is
introduced by indexed proof consumption.

Group-heavy cases are dominated by ML-DSA; follower-heavy cases by ownership
hashing and transaction/proof construction. At 256 groups the separate probe
requests about 13.75 MB per TX (41.25 MB over three TXs), whereas process peak
is about 14.3 MiB: requested volume is not live allocation. Maximum requested
allocation in that workload is about 2.26 MB. Rejection after all signatures
can be cheaper than success because no complete proof is published.

`4G+N` captures counts, not uniform time: 100/100 costs 500 and takes 15.53 ms;
4096/4 costs 4112 and takes 32.54 ms. Their measured ms/cost differ substantially.
Arbitrary signed output bytes can add hashing with unchanged group cost (the
preserved WO02 1 MiB diagnostic already demonstrates that). Current evidence
supports linear member/group accounting together with separate byte/item limits;
it does not establish a universal DoS bound or calibrated coefficient for other
hardware, native Script compositions, larger capacities or concurrent queues.
No change to locked 4G+N is implemented or asserted necessary. Before changing
it, separately review low-end hardware/aggregate limits and a versioned proposal
with signature, ownership-hash and byte-hash terms; numeric amendments are not
justified by this single host. No normative resource amendment is proposed here.

Adversarial per-tx measurements (all deterministic, charged unless raw decode):

| Case | Reason/stage | Group verifies | Ownership checks | Median ms |
|---|---|---:|---:|---:|
| 1000 followers/members, last nonce wrong | Ownership | 0 | 1000 | 6.562 |
| 100 groups, final signature wrong | Signature | 100 | 100 | 15.134 |
| 256 anchors, last duplicates first PK | DuplicateKey / structure | 0 | 0 | 0.311 |
| 500 inputs/10 groups, final self-reference | Reference / structure | 0 | 0 | 0.184 |
| Altered output with valid original signatures | Signature | 1 | 100 | 0.897 |
| Changed authenticated spent amount | Signature | 1 | 100 | 1.032 |
| Changed final authenticated spent script | Ownership | 0 | 100 | 0.625 |
| Negative input amount | Amount / structure | 0 | 0 | 0.031 |
| Extra witness item at final input | Witness / structure | 0 | 0 | 0.033 |
| 257 groups, structural-only | GroupsLimit | 0 | 0 | 0.282 |
| 1025 members, structural-only | MembersLimit | 0 | 0 | 0.373 |
| 4097 inputs, structural-only | InputsLimit | 0 | 0 | 1.424 |
| Truncated serialized TX | Bounded decode | 0 | 0 | 0.003 |
| 11-byte oversized declared input count | Bounded decode | 0 | 0 | 0.003 |

Boundary fixtures over the limits use fake public keys and no valid signatures;
they prove rejection only. In contrast, in-limit maximum workloads above are
fully signed and genuinely verified. Invalid-case RSS ranged 11520–12032 KiB,
including launch floor. The oversized count requests at most 74 bytes in one
ordinary new call (211 bytes total); truncated wire at most 45 bytes. No hang,
unbounded declared-count allocation or sanitizer defect was observed. Raw
decoder failures have no admitted transaction and no signature cost, not a
refunded successful reservation. Full reasons, stage/calls, worst wall/CPU and
memory diagnostics are retained in invalid-workloads.csv.

Artifacts: `src/test/mercaweave/readiness-artifacts/{block-benchmarks.csv,
transaction-benchmarks.csv,invalid-workloads.csv,consensus-readiness-benchmarks.json}`.
Measured source HEAD is 7ed2b23827095e836cf3090f8f98dab78cbbacad; benchmark binary
SHA256 is 746f35040266ecb9cfdf857a259a92fe7ef52595c56dbc8558162f839c8c5873.
The later policy-link fix affects the separate malleability target; this measured
benchmark binary remains byte-identical. JSON records commands, cache flags,
source/binary/artifact hashes, hardware and limitations. Older measurements
remain separate. New CSV artifacts were normalized from CRLF to repository LF
without changing any records; JSON retains original and final hashes. A focused
generator smoke run confirmed LF output for block, single-tx and invalid CSVs.
No performance number is a proof of production-network safety.

## F. Malleability qualification and options

The executable proof uses a real v3-only parent and real child, then mixed
parents with actual bare, P2SH and P2WSH `OP_DROP OP_TRUE` predicates. It changes
one permitted non-signature byte from 0x41 to 0x42. Every accepted parent is
verified anew, including every ownership and native Script check. This is an
already-permitted satisfier of a non-signature predicate; no key ownership,
outpoint, amount or destination is changed.

| Mutation | txid | wtxid | v3 authorization | Original child outpoint |
|---|---|---|---|---|
| Nonempty scriptSig on v3 input | Changes | Changes | Rejects | No valid mutated parent |
| Permitted bare/P2SH predicate argument | Changes | Changes | Remains valid | No longer resolves |
| Permitted P2WSH witness argument | Stable | Changes | Remains valid | Resolves |
| Fresh valid randomized v3 ML-DSA signature | Stable | Changes | Remains valid | Resolves |

Concrete P2SH parent txid changes from
`df160f0e7070cf1ba1698b553b8ac4613533f98bb0fb604838a15263acb2c92d` to
`e9e75d2c625c82c2d11398757556ca9f366df829fd408d6bf00500c4313b0d38`.
The final v3 digests are identical. A child signed for the first parent passes
authorization against that original CTxOut, but its COutPoint is absent from
the map containing only the mutated parent. Passing a copied CTxOut does not
restore chain existence. A coordinator/observer able to mutate and relay the
parent can disrupt pre-signed descendants, package/CPFP preparation or sessions
keyed by the original txid. No theft or altered authorized destination is shown.
Full-wire context reuse rejects these changes even though fresh verification
of the permitted mutated parent succeeds.

Current standardness is a layered result. Actual Solver/IsStandard reject the
bare predicate funding script. They classify P2SH/P2WSH commitments as standard.
The real P2SH predicate passes native Script under STANDARD flags, push-only and
scriptSig size rules, genuine EvalScript redeem extraction and zero-sigop bound.
A native-v2+P2SH example additionally passes actual IsStandardTx. Source audit
of AreInputsStandard/IsWitnessStandard finds no categorical exclusion of this
small P2SH predicate form. Whole coin-view policy functions and mempool admission
were not run; this is not a relay-acceptance demonstration. Current v3 spends
themselves are rejected as WITNESS_UNKNOWN by AreInputsStandard. That current
v3 exclusion must not be mistaken for a future solution to mixed-input txid risk.

| Option | Benefit | Cost / remaining issue |
|---|---|---|
| A: preserve locked digest | No vector/domain change; supports existing Script forms | Must document txid instability; avoid preconfirmation descendant commitments or wait for confirmation; exact-wire records still required |
| B: constrain mixing | Native v2/v3-only funding with empty scriptSigs gives a narrow auditable txid-stability rule; no signed-byte changes for wallet policy | Wallet/coordinator policy limits interoperability; consensus restriction would be a separately approved normative amendment; other witness types need their own precise review |
| C: commit problematic scriptSig data | Could bind additional identity data with a versioned design | Generic scriptSig signature bytes may themselves sign the digest, causing circularity; cannot simply hash every scriptSig; need canonical noncircular projection or restrict forms, new domain/version/spec/vectors and independent review |

Recommendation for future wallets/coordinators: use B as an explicit local
funding policy for workflows requiring stable preconfirmation txids, initially
native v2/v3-only; otherwise apply A's documented constraints. Do not claim
generic mixed-input stability. C is not justified as an urgent fix to an
unauthorized-spend exploit; none was shown. No option changes normative rules
in this checkpoint. No activation height is calculated or proposed.

## G. Readiness decision and remaining gates

**READY WITH EXPLICIT RESTRICTIONS.**

This recommendation is only for a separately scoped, isolated consensus-code
integration exercise in the next work order. Evidence supports complete immutable
proof construction, fail-closed per-input consumption, native-v2 preservation,
checked budgets, canonical vectors and measured bounded invalid paths.

Restrictions/gates: preserve the locked protocol and uncached baseline; obtain
UTXOs from a trusted vin-order view; use one complete context per exact snapshot
and Bind once before queue reads; audit every entrypoint and all cache fast paths
in that separate integration. Retain independent native checks and all existing
resource rules. Establish production-budget composition and low-end/concurrent
resource calibration separately. Independent security review, additional compiler/
architecture testing, coverage-guided fuzzing, supported-host leak checking and
full node-policy/cache/reorg integration tests remain gates before any production
readiness claim. Do not promise arbitrary mixed-input preconfirmation txid
stability; apply the documented workflow restrictions.

This coding agent's self-review does not replace an independent security audit.
No production deployment, activation, wallet, mining or node-data work is
authorized or implied by this decision. No demonstrated normative security
counterexample triggered the work order's stop condition.

## H. Git hygiene and evidence preservation

Every code commit followed relevant tests, full diff inspection and
`git diff --check`. The final evidence commit includes only research source,
Markdown, CSV and JSON/JSONL text; deterministic fixture seeds are public test
data and no expanded keys are written. Scratch scripts, logs, tools and all
builds remain ignored under work/. No production file differs from the original
project base. WO01/02 reports, golden bytes and saved measurements are unchanged.

Final checks: clean research working tree after the evidence commit; clean
original worktree still on work at its original SHA; no push, merge, rebase,
reset or new branch; no nodes, datadirs, wallets, mining instances or chainparams
modified. No activation code or heights. Work stops at the WO03 research checkpoint.
