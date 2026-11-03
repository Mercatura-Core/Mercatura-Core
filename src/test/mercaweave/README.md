# Standalone MercaWeave v0.1 research

This directory is its own CMake project. Production CMake never includes it.
It reuses Mercatura's actual `CMutableTransaction`, `CTransaction`, `CScript`,
outpoint/output serializers, hash primitives, HKDF and pinned pure ML-DSA-65
wrapper. No nodes, RPC, wallets, network activation or datadirs are involved.

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
