# MercaWeave v0.1 — Work Order 02 security qualification

Date: 2026-11-02, America/Los_Angeles. Scope: standalone/offline research only.
No production validation, network parameters, wallets, PSBT, Qt or activation
code was changed. This is a qualification checkpoint, not deployment approval.

## A. Repository and checkpoint reproduction

Starting research HEAD: `207e9645955c6e225442f0e615479c5684ffa0a4`.
Branch: `research/mercaweave-v0.1`.
Worktree: `/workspace/Mercatura-MercaWeave-v0.1`.
Original checkout: `/workspace/Mercatura-Core`, branch `work`, HEAD
`4b88b619fb1b08c7f30112a38eb471880b2a6af4`, clean before and after inspection.
No fetch, reset, new branch, rebase, stash, merge or push was performed in WO02.

Read both WO01 audit/validation documents completely, inspected actual sources,
and reproduced the original 9855 assertions and 3/3 CTest results before changes.
Reran the original benchmark matrix into `work-order-02-baseline.csv` without
overwriting WO01 evidence. It uses the starting WO01 source/Release configuration
before assertion configuration was strengthened. The 53/100 single-group cases
remain 9557/13317 bytes against 281432/530955 v2 bytes: 96.60%/97.49% savings.

Validated local code commits:

```text
8a6922cbf356870a176499eeb924a2c7344f7ddd Exercise unmodified production v2 verification in research tests
9ba4ea7a23861d00378649f1a8ec1b6dc2a4400d Bound research transaction decoding before vector allocation
1ccdcf7e20cc3ad78785eca237897a14c48c2e07 Reject hostile follower and coordinator authorization attacks in tests
bb5726b74c6234c59562d79720598b445297ff67 Measure staged verification work and adversarial resource costs
```

A final documentation/artifact commit contains this report. Obtain all new
hashes and final HEAD with `git log --format='%H %s' 207e964..HEAD`.

## B. Threat model, findings and executable invariants

Reviewed `mercaweave.{h,cpp}`, `fixtures.{h,cpp}`, original tests/oracles,
`src/script/interpreter.{cpp,h}`, `src/script/sign.cpp`,
`src/crypto/mercatura_mldsa.c`, transaction precomputation and serialization,
`src/consensus/tx_verify.cpp`, policy flags and the vendored build dependencies.
Low-level Mapping/hash/signing helpers are research tools, not an authorization
boundary. Acceptance must go through `Verify` or the frozen native adapter.

| Adversary | Trust boundary / invariant | Executable coverage |
|---|---|---|
| Malicious transaction creator | All authenticated fields and outputs are committed; no partial success | Original `NegativeTests`, native `Interoperability` |
| Malicious follower | Every member's actual spent commitment must match assigned PK and local nonce | `attack_tests::UnauthorizedFollowers`: real valid malicious signature still rejects Ownership before ML-DSA |
| Malicious coordinator | Validator reconstructs manifest; omitted/reordered members cannot be supplied as authority | `CoordinatorSign`, omitted-member digest rejects Signature; reordered Manifest rejects Reference |
| False spent metadata | Caller must obtain authenticated UTXOs; cryptography cannot establish their existence | Actual victim context rejects; deliberately fabricated self-consistent metadata positive control documents the prerequisite |
| Malformed serialized input | Canonical counts, lengths and remaining bytes checked before allocation | `parser_tests::Tests`, `DecodeWitness`, `DecodeTransaction` |
| Stale context/cache | Record binds full wire/spent/genesis; native precomputation belongs only to its frozen snapshot | Original record tests; attack record tests; native snapshot mutation tests |
| Resource exhaustion | Limits reject before crypto; parser bounds precede allocations; no recursive references | Boundary tests, 12000 parser mutations, 10000 illegal references, staged benchmark |
| Mixed v2/v3 manipulation | Existing v2 and grouped v3 must both independently pass | `NativeSession::VerifyMixed`, production `VerifyScript`/PQ checker, shared-context mutation tests |

No reproducible unauthorized-v3-spend vulnerability or contradiction requiring
a locked cryptographic/serialization amendment was found. This is not an
independent cryptographic audit or proof of security.

**M1: transaction identity depends on permitted input composition.** Genuine
production Script execution accepts the test's non-signature `OP_DROP OP_TRUE`
predicates in bare and P2SH form with changed scriptSig data. Both mixed
transactions retain the same v3 signed commitments and authorizations, while
txid and wtxid change. P2WSH predicate witness changes leave txid fixed and
change wtxid. These remain accepted even under standard SCRIPT FLAGS; full
relay policy is not asserted. Amounts, destinations and spent outpoints remain
signed and unchanged; no unauthorized v3 spending is demonstrated.

This is the locked specification's deliberate exclusion of unrelated scriptSig
and witness bytes. It can disrupt pre-signed descendants, package coordination
or applications that assume a stable preconfirmation txid. Future wallet policy
should distinguish funding compositions; native v2/v3-only inputs have empty
scriptSigs. Do not promise txid stability for arbitrary existing Script inputs.
No signed-message change is made or recommended without separate review.

Both native v2 and v3 ML-DSA signature randomization were demonstrated with
real signatures: txid stays fixed, wtxid changes, and the other authorization
remains valid. Full immutable records still detect the changed witness bytes.
Classical CHECKSIG/CHECKMULTISIG and Taproot key-path authorization continue
to fail in unmodified production code.

**M2: unknown-v3 success is not MercaWeave authorization.** A direct production
Script check can still accept a corrupted v3 signature as unknown witness
semantics under the existing mandatory flags. The research adapter deliberately
routes every actual v3 spent output through group verification and rejects it.
This is expected pre-integration behavior, not activation or production support.
Any future integration must fail closed when transaction-scoped group context
is missing at every entrypoint.

**R1: 4G+N is not a complete transaction CPU budget.** See measured byte-hashing
diagnostic below. Existing separate byte/weight/resource constraints are required.
No claim of conservativeness for all block/input compositions is justified by
this prototype, and the locked formula has not been changed.

## C. Coding changes

- Added `native_v2.{h,cpp}`: test-only immutable transaction/spent/genesis/flag
  session with exactly one genuine `PrecomputedTransactionData::Init` per
  session. Uses actual `ComputeMercaturaPQAuthDigestV1`,
  `MutableTransactionSignatureChecker::CheckMercaturaPQSignature`, and
  `VerifyScript`. No production function was edited or stubbed.
- Added `native_tests.cpp`: pure native-v2, mixed multi-group, wrong network,
  wrong FIPS context, missing context, authenticated-context mutations,
  deliberate malleability and disabled-classical-authorization tests.
- Hardened Reader with a private bounded cursor; added `DecodeTransaction`:
  preflight canonical counts/lengths/flags and available bytes before invoking
  inherited allocating deserialization. It preserves existing consensus bytes
  and the existing 32 MiB research-harness budget. Decoder success does not
  imply authorization or chain validity.
- Added `parser_tests.cpp`, `mutation_seeds.h` and `attack_tests.cpp` for seeded
  byte/reference mutations, malicious ownership/manifest attempts, exact
  membership, reverse-order verification and four-worker deterministic checks.
- Split low-level `BodyHashes` from manifest hashing for stage measurements;
  signed bytes and all permanent golden vectors remain identical.
- Added `qualification_bench.cpp` and `resource_qualification.py` for phase,
  rejection and allocation measurements. Only research CMake is modified.

The production policy headers require assertions and reject NDEBUG builds.
The first native-link attempt therefore failed at compile time with
`Cannot compile without assertions!`. Research CMake now adds `-UNDEBUG`,
preserving production assertion requirements. No check was disabled. Existing
vendored secp256k1 objects satisfy interpreter/pubkey link dependencies; no new
third-party download or compiler installation was needed.

## D. Tests, fuzzing and sanitizers

Exact main commands from the research worktree:

```bash
work/tools/cmake/data/bin/cmake -S src/test/mercaweave -B work/build-release -DCMAKE_BUILD_TYPE=Release
work/tools/cmake/data/bin/cmake --build work/build-release -j4
work/tools/cmake/data/bin/ctest --test-dir work/build-release --output-on-failure
work/build-release/mercaweave_tests
work/build-release/mercaweave_native_tests
work/build-release/mercaweave_parser_tests
work/build-release/mercaweave_attack_tests
python3 src/test/mercaweave/resource_qualification.py work/build-release/mercaweave_qualification_bench src/test/mercaweave/security-benchmarks.csv
work/tools/cmake/data/bin/cmake -S src/test/mercaweave -B work/build-sanitize -DCMAKE_BUILD_TYPE=RelWithDebInfo -DMERCAWEAVE_SANITIZERS=ON
work/tools/cmake/data/bin/cmake --build work/build-sanitize -j2
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 work/tools/cmake/data/bin/ctest --test-dir work/build-sanitize --output-on-failure
git diff --check
```

| Suite | Result |
|---|---|
| Release CTest | 7/7 passed |
| ASan/UBSan CTest | 7/7 passed, no address/undefined-behavior findings |
| Original C++ suite | 9855 assertions; 16 positive, 86 negative, 3 structural maximum cases, 8301 malformed cases |
| Native interoperability | 91 assertions; 14 counted accepted cases, 19 counted rejection cases |
| Bounded parser | 19957 checks; 7523 decoder rejections; 12000 seeded mutations/arbitrary buffers |
| Hostile followers/context | 20067 assertions; 10 named attacks, 10000 seeded illegal references |
| Resource runner | 12 real-key/signature workload rows with exact G verification and N ownership counts |

These counters overlap conceptually and should not be summed as unique tests.
Parser rejections include all 5477 truncated prefixes of a valid full transaction,
in addition to the seeded mutations. Persisted seeds are
`0x574f303250415253` and `0x574f3032464f4c4c` in `mutation_seeds.h`.

Coverage-guided fuzzing is unavailable: clang/clang++, afl-fuzz and honggfuzz
are not installed. No toolchain was installed just to enable one. The bounded
mutation harnesses ran under both sanitizers. They are fuzz-smoke coverage,
not exhaustive fuzzing. LeakSanitizer was not retried because the unchanged
sandbox's ptrace incompatibility was already demonstrated in WO01. Leak
detection remains unavailable and is not reported as passed.

## E. Resource qualification

All rows are research authorization fixtures with synthetic spent outputs and
real ML-DSA keys/signatures, including maximum group/member/input cases.
They do not establish real UTXO availability, maturity, block validity or relay
standardness. Rows exceeding the inherited 400000 weight limit are explicitly
marked in CSV. No global bound was changed to call them production-valid.

Three warm-ups and eleven measured runs per stage, median and worst observed
milliseconds, fresh child process per workload. Full timing includes result
construction/destruction; allocation counters are disabled during timing and
collected in a separate pass. VM scheduling can affect wall-time outliers;
no cold-cache claim or CPU-frequency pinning is made.

| N | G / ML-DSA calls | Ownership checks | Cost 4G+N | Median ms | Worst observed ms | Process RSS KiB |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 1 | 1 | 5 | 0.1330 | 0.1453 | 11,008 |
| 10 | 1 | 10 | 14 | 0.2052 | 0.2698 | 11,008 |
| 53 | 1 | 53 | 57 | 0.4842 | 0.7962 | 11,008 |
| 100 | 1 | 100 | 104 | 0.9825 | 1.2976 | 11,008 |
| 500 | 1 | 500 | 504 | 3.6826 | 3.9883 | 11,008 |
| 1000 | 1 | 1000 | 1004 | 7.3961 | 8.4819 | 11,008 |
| 100 | 10 | 100 | 140 | 2.0680 | 2.2641 | 11,008 |
| 100 | 100 | 100 | 500 | 15.0010 | 20.1326 | 11,008 |
| 256 | 256 | 256 | 1280 | 41.1770 | 44.8860 | 16,620 |
| 1024 | 1 | 1024 | 1028 | 9.3695 | 14.4909 | 11,008 |
| 4096 | 4 | 4096 | 4112 | 30.9591 | 37.5108 | 11,008 |
| 1 + 1 MiB output | 1 | 1 | 5 | 4.0191 | 7.0888 | 11,008 |


CPU: cloud AMD EPYC 9V74, three visible CPUs, Debian 13.6, GCC/G++ 14.2.0,
CMake 4.4.4; portable ML-DSA/SHA256. Both C and C++ core Release builds use
`-O3 -DNDEBUG -UNDEBUG`, with C++20 and warnings. Exact generated flags,
measurement HEAD, executable hash and CSV hash are in `security-benchmarks.json`.
These are separate from the preserved WO01 and reproduced-baseline results,
and are not Ryzen/WSL2 timings.

The CSV additionally contains group reconstruction, independent ownership,
manifest construction, component/body hashing, manifest hashing, final digests,
ML-DSA-only verification, transaction decoding, malformed witness rejection,
early/late ownership failure and final-group signature failure.

For 4096 inputs/four groups, ownership dominates and exactly 4096 commitments
and four signatures are checked. For 256 groups, ML-DSA dominates. New allocation
counts grow approximately linearly with inputs (74 at N=10, 4056 at N=1000,
16490 at N=4096/G=4). Source review confirms bounded map/set operations and
linear hash/ownership passes, not repeated nested full-input searches.

The original research v2 comparison helper recomputes full component hashes per
input, which is quadratic for that comparison tool. The new native session
precomputes production transaction data once and does not do this per v2 input.
Production verification itself was not modified.

Rejection counts are asserted: malformed witnesses execute zero ownership and
signature checks; first bad commitment executes one ownership check and zero
signatures; last bad commitment executes N ownership checks and zero signatures;
final-group signature failure executes N ownership checks and G signature calls.
These invalid cases never return an authorization record.

Peak observed whole-process RSS across the matrix: **16,620 KiB**. Linux
`ru_maxrss` includes fixture construction and attack copies and may retain the
Python launcher's pre-exec high-water floor; identical low-row readings are not
evidence of identical allocation needs. This is not a complete memory upper-bound
proof. Ordinary C++ new/new[] counts exclude C malloc/realloc (including CScript
prevector storage), aligned new, stack and library-internal memory. Requested
bytes are cumulative, not simultaneous live bytes. The tiny UINT32_MAX-input
wire declaration caused a largest observed new request of 74 bytes for error
handling, rather than allocation of the declared input vector.

**Accounting interpretation:** the one-input/group 1 MiB-output case retains
cost 5 but spends several milliseconds hashing output bytes. It exceeds standard
weight and remains below the initial byte-capacity size; it is not a relayable
transaction claim. Thus 4G+N alone cannot price or bound all CPU. The locked spec
already requires independent global byte/weight/resource limits in addition to
this provisional signature/ownership allowance. Those remain mandatory. If a
future design requires one complete computational budget, a separately reviewed,
versioned per-byte work charge is the minimal candidate; no coefficient or rule
change is selected here. Hash/signature vectors would not change solely from a
resource-charge amendment, but consensus resource acceptance could change.

## F. Recommendation and remaining gates

**Not yet ready to wire into consensus validation.** The standalone engine is
a useful qualified reference for a separately authorized integration design.
First resolve fail-closed group-context requirements at every script entrypoint;
bind active flags/semantics and authenticated-view assumptions in real caches;
model block-wide witness/byte budgets and all allowed non-v3 compositions;
define wallet/coordinator txid expectations; and obtain independent security
review plus broader coverage-guided fuzzing and leak checks on a supported host.

No cryptographic, serialization, witness, derivation or group-limit amendment
was required. Malleability is compatible with the explicit locked signing scope.
Resource calibration is a remaining integration qualification gate, not a claim
that this benchmark proves 4G+N conservative for every possible transaction.
No production integration, activation mechanism or schedule is implemented.

## G. Git and evidence preservation

Every code commit passed directly relevant tests, adjacent focused CTest and
diff inspection/checks. Only research sources/CMake and design documentation
changed. No production consensus source, chain parameters, wallet, PSBT or Qt
file was modified. Builds, caches and binaries remain ignored under `work/`.
No private signing keys or real wallet secrets are stored in artifacts.

WO01 `vectors.json` remains SHA256
`ea4978206307ac21d4f544b05e6da3543e094c9a1c4bd97c44f90fad4420c49c`;
WO01 `benchmarks.csv` remains SHA256
`0896a259684d85b4df92299d535c7f292ab237fefbb0a3114c4c8cd7fe83ac28`.
Original evidence is preserved. No push, merge, live-node/datadir operation,
original-checkout edit or activation work was performed. Final statuses are
checked after the report/artifact commit.
