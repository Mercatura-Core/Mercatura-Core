# Standalone MercaWeave v0.1 research

This directory is its own CMake project. Production CMake never includes it.
It reuses Mercatura's actual `CMutableTransaction`, `CTransaction`, `CScript`,
outpoint/output serializers, hash primitives, HKDF and pinned pure ML-DSA-65
wrapper. No live nodes, RPC, ordinary wallet databases, network activation or existing
datadirs are involved.

## Build and test

From the research worktree root, with CMake >=3.22, a C++20 compiler and Python 3:

```bash
cmake -S src/test/mercaweave -B work/build-release -DCMAKE_BUILD_TYPE=Release
cmake --build work/build-release -j4
ctest --test-dir work/build-release --output-on-failure
work/build-release/mercaweave_tests
python3 src/test/mercaweave/reference.py
python3 src/test/mercaweave/crosscheck.py work/build-release/mercaweave_tests
```

The session initially lacked CMake. It was installed only under ignored
`work/tools`, using `python3 -m pip install --target work/tools cmake==4.4.4`.
That installation's executables are `work/tools/cmake/data/bin/{cmake,ctest}`.
No system installation or unrelated dependency is required.

```bash
cmake -S src/test/mercaweave -B work/build-sanitize \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DMERCAWEAVE_SANITIZERS=ON
cmake --build work/build-sanitize -j4
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir work/build-sanitize --output-on-failure
work/build-release/mercaweave_bench > work/benchmarks.csv
```

LeakSanitizer could not run in the session's ptrace-based sandbox. The exact
fatal error and successful ASan/UBSan commands are recorded in the validation
report. Run leak detection separately on a host that supports it.

## API contract

`Sign` and `Verify` require spent `CTxOut` records authenticated by the caller
and supplied in vin order. They enforce basic format/value/duplicate-outpoint
checks, existing per-stack/per-item witness bounds, locked group limits,
canonical reconstruction, per-input ownership and transaction-wide signatures.
They do not establish chain membership, maturity, finality, remaining block
budget, or mempool standardness. Passing invented spent outputs is not proof
that those outputs exist in a blockchain.

The in-memory harness limits serialized buffers/transactions to 32 MiB to
bound these experiments. This is a harness limitation, not a new consensus
limit; larger mixed-input workloads need separately bounded harness support.

Every non-v3 input requires an independently correct `NonV3Verifier`; the
default fails closed. `fixtures.cpp::VerifyV2` is a research adapter reproducing
the audited v2 digest, ownership and signature rules. It is not the production
script interpreter. Production v2 semantics and code are unchanged.

Successful validation returns an immutable record bound to the exact full
transaction bytes, spent context and genesis. `Matches` detects a changed
context; it does not establish chain validity. `Verify` accepts no cached result
and always rechecks ownership. Production flags, reorgs, caches, mempool and
parallel script integration remain separate future work.

`DecodeWitness` is a bounded decoder of the locked anchor/follower grammar;
it checks canonical CompactSize counts and exact item lengths before allocation.
Transaction serialization and complete valid transaction round trips use the
inherited serializers. There is no new arbitrary-transaction network parser.

`Sign` requires an explicit randomness source and fails atomically when it is
missing or fails. Only the named test helper supplies zero randomness. Real
applications must supply fresh cryptographically secure bytes for every group.
Secret keys, temporary signing randomness and the HKDF state are cleansed.

## Fixtures and measurements

`reference.py` contains the locked Section 12 expected values and independently
reproduces all ten values and the three required lengths. `crosscheck.py` parses
actual C++ transaction bytes independently and checks four real-key cases:
two-input/one-key, repeated nonce, noncontiguous multi-group and mixed v2/v3.
It also checks HKDF derivation for all three purposes and compares the complete
deterministic C++ output to permanent `vectors.json`.

All fixture seeds are public, deterministic TEST DATA. The fixture signatures
use test-only zero randomness and are verified through the actual pinned
ML-DSA wrapper. No expanded private key is stored. The synthetic patterned
public key is used only for structural hashing.

`benchmarks.csv` and `benchmark-environment.json` record this session's final
run. Workloads use one warm-up plus five measured repetitions; medians include
complete signing/verification pipelines. Each group uses one real signature;
every member's ownership is checked. Invalid nonce rejection occurs before
ML-DSA. Peak RSS and CPU are cumulative process observations, not isolated
per-transaction memory or CPU percentage. No cold-cache measurement is claimed.

V2 comparison transactions have real signatures verified through the research
adapter, not production consensus validation. Weight-only labels do not certify
relay standardness. Independent-key cases at 500/1000 inputs are skipped because
they exceed 256 groups. A 1000-input one-group transaction is tested.

The 4096-input, 256-group and 1024-member maximum tests are explicitly structural
fixtures without valid ML-DSA signatures. Above-limit cases reject before crypto.
They do not claim production mempool acceptance.

## Work Order 02 qualification

The original benchmark sections above describe Work Order 01 artifacts.
The expanded research project now also builds `mercaweave_native_tests`,
`mercaweave_parser_tests`, `mercaweave_attack_tests` and
`mercaweave_qualification_bench`. CTest runs seven focused targets.

`native_v2.cpp` links unmodified production transaction precomputation,
`ComputeMercaturaPQAuthDigestV1`, the genuine PQ signature checker and
`VerifyScript`. It snapshots transaction/spent/genesis/flags and precomputes
once per session. Native Script verification alone must not authorize v3:
the existing interpreter still has unknown-witness semantics. `VerifyMixed`
always sends actual v3 outputs through the standalone group verifier.
`pq_checker_calls` counts native checker entries; only valid complete contexts
reach actual ML-DSA verification. Production files and normal builds are unchanged.

Research builds now preserve assertions with `-UNDEBUG`, as required by
Mercatura's production policy headers. No assertions were disabled to link them.
The dependency is the already-vendored secp256k1 source, not a new download.

`DecodeTransaction` checks canonical counts, lengths, flags and remaining bytes
before invoking inherited allocating deserialization. It is a bounded research
byte decoder, not a network entrypoint or authorization decision. Persistent
mutation seeds are in `mutation_seeds.h`. No coverage-guided fuzzer is installed;
bounded mutation suites are exercised under ASan/UBSan.

```bash
work/build-release/mercaweave_native_tests
work/build-release/mercaweave_parser_tests
work/build-release/mercaweave_attack_tests
python3 src/test/mercaweave/resource_qualification.py \
  work/build-release/mercaweave_qualification_bench work/security-benchmarks.csv
```

Committed WO02 measurements are `security-benchmarks.csv` and
`security-benchmarks.json`; checkpoint reproduction uses
`work-order-02-baseline.csv`. The new resource runner uses fresh child processes,
three warm-ups and eleven measured repetitions per stage. Whole-process RSS may
include the Python launcher's pre-exec high-water floor; C++ allocation counters
exclude C malloc/realloc and aligned allocations. The separate byte-hashing
diagnostic shows why `4G+N` must not be treated as a complete CPU budget.

See `doc/design/mercaweave-v0.1-security-qualification.md` for the eight-part
threat model, named executable tests, supported Script malleability examples,
limits of the accounting evidence and remaining integration gates.

## Work Order 03 consensus-readiness checkpoint

CTest now has eleven targets. `auth_context.{h,cpp}` provides the research
authorization boundary: `Capture` copies transaction, vin-order authenticated
spent outputs, genesis and all flags into an immutable request; `Prepare` always
performs complete group and genuine native verification before publishing a
privately constructed `ValidatedContext`. No caller-supplied bool, partial
`Result`, arbitrary Mapping or existing Authorization can create that context.
Use `Bind` once for a transaction, then share its immutable `InputChecks` across
per-input workers. `Check(index)` is O(1), independent of anchor execution order.
The convenience `CheckInput` does a full binding comparison per call; do not use
it to perform repeated whole-transaction comparisons in a large input queue.
Without context it rejects v3; non-v3 checks retain the real native path.

The new context adds full flag binding and both transaction identities to the
older offline record. No cross-request signature or whole-transaction cache is
implemented. These objects do not establish UTXO existence, reorg validity,
chain maturity, finality or full block acceptance. Low-level hashing helpers and
the callback-taking `Verify` remain research tools, not proof-minting interfaces.

`block_work.{h,cpp}` uses checked cumulative counters and explicit reference
budgets. It reserves structurally measurable work before verification and
retains charges for invalid authorizations. Failed reservations poison a ledger;
no successful prefix authorizes a denied workload. It uses genuine `CBlock`
serialization with a synthetic coinbase/header and separate full bytes, inherited
weight, witness items, legacy/native cost, `4G+N`, PK bytes and hash-pass counters.
It is a resource/authentication simulator, not production consensus integration.
The current fixed snapshot is 4 MiB, 16384 witness items and 80000 sigop cost;
weight is measured separately. No network configuration is inferred or changed.

```bash
python3 src/test/mercaweave/measure_readiness.py \
  --binary work/build-release/mercaweave_readiness_bench \
  --output-dir work/readiness-reproduction
work/build-release/mercaweave_malleability_tests
cmake -S src/test/mercaweave -B work/build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build work/build-debug -j2
ctest --test-dir work/build-debug --output-on-failure
# After building Release, Debug and sanitizer configurations:
python3 src/test/mercaweave/check_readiness_determinism.py
```

The comparison command checks the saved CSV verdicts/counters, writes the
deterministic proof/check JSON artifacts, and deliberately ignores timing and
allocator/RSS variation. Committed evidence is in `readiness-artifacts/`.
The new measurements include fully signed 4096-input, 256-group and 1024-member
cases; the earlier structural-only description refers to WO01 evidence.

The malleability test links genuine production Solver, IsStandard, IsStandardTx,
EvalScript and native verification. Whole `AreInputsStandard`/`IsWitnessStandard`
coin-view paths are source-audited, not dynamically executed. To link Debug
assertion support it also uses unmodified `util/check.cpp` and `clientversion.cpp`
with generated diagnostic-only metadata from the existing project. Sanitizer
builds exclude only UBSan vptr instrumentation on production `policy.cpp`:
otherwise GCC retains RTTI references for discarded node-dependent functions.
All other checks and all new research code retain full ASan/UBSan instrumentation.

See `doc/design/mercaweave-v0.1-consensus-readiness.md` for the readiness decision,
cache boundaries, measured block matrix, invalid cases and malleability options.

## Work Order 04 isolated interpreter/coin-view integration

The standalone project now has seventeen CTest targets. Only explicitly isolated
targets define MERCATURA_MERCAWEAVE_RESEARCH_TEST_ONLY. Normal production CMake
does not include this project. Two guarded interpreter additions are erased from
normal compilation; there is no runtime flag or network activation path.
Normal and isolated checker ABIs live in separate executables/archives.

`isolated_validation.{h,cpp}` requires the research macro at compile time.
`integration::View` owns a real synthetic CCoinsViewCache behind a lock and
checked epoch. Trusted fixture setup inserts coins; validation itself accepts
only the view and transaction, never unchecked spent-output vectors.
`Engine::Prepare` runs actual CheckTransaction/CheckTxInputs and acquires coins,
then creates a private complete proof through WO03 verification. `Bind` makes
fixed-index immutable ScriptJob objects. Each job invokes real VerifyScript;
the isolated witness-v3 branch consumes matching complete evidence. No context
means rejection, never unknown-version success. Native inputs use genuine
production verification, including the existing PQ-v2 rules.

`Engine::Validate` combines those stages. `ValidateWire` adds bounded canonical
decoding. `Candidate` uses real layered coin caches, checked budgets and atomic
fixture publication. Stale/foreign views, restored coins or changed tips invalidate
old jobs. Candidate receipts never export a staging proof. Candidate is not
ConnectBlock: no PoW/header/merkle/sequence-lock or full chainstate claim.

`Policy::CurrentPolicy` executes actual IsStandardTx, AreInputsStandard and
IsWitnessStandard; it continues to reject unknown v3. `ConsensusFixture` is an
explicit research-only exercise of the guarded interpreter. No relay, fee,
400000-WU standardness or 4MiB network-capacity rule is changed. Unactivated v3
outputs must never be advertised as protected MercaWeave custody.

This uncached wrapper does not execute node CheckInputScripts/CScriptCheck or
their caches/queue. Native v2 is genuinely verified during complete WO03
preverification and again during Script dispatch; both passes and their
precomputation/copy overhead are measured. Group signatures run once per group.
There is no production cache optimization or proof import/export format.

```bash
cmake --build work/build-release -j2
ctest --test-dir work/build-release --output-on-failure
python3 src/test/mercaweave/check_isolation.py work/build-release
# After building/testing all three configurations:
python3 src/test/mercaweave/qualify_integration.py \
  --release work/build-release --debug work/build-debug \
  --sanitize work/build-sanitize --output-dir work/wo04-reproduction
```

Committed results are under integration-artifacts/. They preserve every earlier
qualification file. The new runner compares exact vectors/proof/control outputs
and twenty cross-build workload counter cases; timing, allocator and RSS values
are compared only as measurements. Invalid sample dispatch is unavailable (-1),
never a successful skipped check. See
doc/design/mercaweave-v0.1-isolated-consensus-integration.md for the exact call path,
isolation evidence, security boundaries, commands, measurements and verdict.

## WO05 research wallet and PSBT

The standalone wallet facade reuses the existing protected PQ seed envelope and
native v2 derivation, adds locked grouped derivation and 64-address rotation,
and uses actual native PSBT containers/parser/Merge and coin-selection routines.
It does not instantiate CWallet or change ordinary wallet storage. Funding and
completed PSBT extraction use the WO04 owned synthetic coin view and real native
transaction/input/Script checks. Full-node wallet/mempool lifecycles remain
unqualified. Current input policy still rejects grouped spends.

All three builds now contain 22 CTest targets. After building/testing them with
the commands above, preserve old evidence and collect new evidence with:

```bash
python3 src/test/mercaweave/qualify_wallet.py \
  --release work/build-release --debug work/build-debug \
  --sanitize work/build-sanitize
```

The runner writes only wallet-artifacts/, compares 24 workload rows across three
builds, checks previous evidence against the WO04 HEAD, and requires all 22
tests in each build to pass. Research storage is confined to ignored
work/wo05-wallets; tests use only public fixture seeds and temporary encrypted
snapshots. Production signing defaults to native strong entropy; deterministic
workload signatures explicitly use fixture-only zero entropy.

See doc/design/mercaweave-v0.1-wallet-psbt-integration.md for exact boundaries,
results, fees, PSBT restrictions and remaining gates. Unactivated v3 outputs
are not protected custody: normal unknown-witness permissiveness is unchanged.
No Qt, RPC, native mining, Secure Vault, activation or deployment is included.

## WO06 Qt and mining integration

Normal Qt has optional collapsed panels and a permanently closed grouped-network
gate. The isolated backend, reward planner and positive capability injection are
compiled only by the standalone test project. Ordinary v2 receive/send and native
mining controls retain their existing paths. No grouped broadcaster, background
signer, activation or deployed scheduler exists.

Enable the focused headless suite with `-DMERCAWEAVE_QT_TESTS=ON`. It requires
Qt6 Core/Widgets/Network/Test/Concurrent, Boost headers and libqrencode development
files. Use `CMAKE_PREFIX_PATH` for a workspace-local SDK if needed. Build with at
most four jobs; qualification here used two. The 28-target suite also compiles
the actual affected application translation units without research definitions.
After three complete green configurations, run:

```bash
python3 src/test/mercaweave/qualify_qt_mining.py \
  --release work/build-release --debug work/build-debug \
  --sanitize work/build-sanitize
```

The runner compares locked vectors, unit summaries and all WO05 workload rows,
runs 2×-scale widget checks and verifies earlier evidence unchanged. It writes
only an ignored work/ summary. Do not replace older artifacts or commit logs,
SDKs, wallet snapshots or experiment output. See
doc/design/mercaweave-v0.1-qt-mining-integration.md for boundaries and the exact
qualification matrix. Full linked node/wallet/miner runtime is deferred to WO07.
