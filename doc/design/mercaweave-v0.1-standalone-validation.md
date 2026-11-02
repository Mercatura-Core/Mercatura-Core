# MercaWeave v0.1 standalone validation checkpoint

Date: 2026-11-02 (America/Los_Angeles). Work Order 01 research checkpoint.
Outcome: compiled standalone grouped ML-DSA-65 signing and verification,
deterministic vector matching, adversarial tests and initial benchmarks complete.
No production integration or network activation was performed.

## A. Repository

- Original checkout: `/workspace/Mercatura-Core`, branch `work`, HEAD
  `4b88b619fb1b08c7f30112a38eb471880b2a6af4`. Clean before and after work.
- Canonical `project` remote was initially absent; added
  `https://github.com/Mercatura-Core/Mercatura-Core.git`, matching existing `origin`.
- Fetched `project/v0.1`: `4b88b619fb1b08c7f30112a38eb471880b2a6af4`.
  Independently verified against `git ls-remote project refs/heads/v0.1` and
  GitHub's live branch reference. The matching historical SHA was not assumed.
- Research worktree: `/workspace/Mercatura-MercaWeave-v0.1`.
- Research branch: `research/mercaweave-v0.1`.
- Validated engine commit: `84e766a0f9932bdf03c7c380e8c819083b3efbe1`.
- A second documentation/results commit contains this report. Its full SHA
  and final HEAD are available with `git log --format='%H %s' project/v0.1..HEAD`.

## B. Specification

Read the complete self-contained locked specification and November 2 directive.
See `mercaweave-v0.1-source-audit.md` for source paths, symbols, architecture,
cache hazards, policy distinctions and the pinned implementation revision.
Locked protocol domains, serialization, FIPS context, witness formats,
derivation and limits were followed unchanged. No versioned amendment was needed.

The independent standard-library Python oracle matches C0, C1, Hprev, Hamount,
Hscript, Hseq, Hout, Htx, Hmanifest and Dgroup, byte-for-byte. Manifest, TxBody
and FinalBytes lengths are 2030, 296 and 2085. C++ hashes and exact manifest bytes
also match Python; the patterned public key is never treated as a valid signer.

## C. Implementation

New isolated sources under `src/test/mercaweave`: `mercaweave.{h,cpp}`,
`fixtures.{h,cpp}`, `tests.cpp`, `bench.cpp`, `reference.py`, `crosscheck.py`,
`vectors.json`, `CMakeLists.txt` and `README.md`. Results are `benchmarks.csv`
and `benchmark-environment.json`. Root `.gitignore` adds only `/work/`.
No production source or normal build target is modified.

The standalone CMake targets are `mercaweave`, `mercaweave_tests` and
`mercaweave_bench`. They link existing transaction/script serialization,
SHA256/SHA3-384, HKDF and pinned ML-DSA-65 sources directly.

The library implements bounded witness decoding, canonical group reconstruction,
duplicate-key rejection, independent ownership, global manifests, all hashes,
group signing, exactly one verification per group, explicit typed errors,
derivation and wire-size measurement. Successful results own immutable records
bound to full transaction bytes, spent context and genesis. No cached record can
be passed back to bypass verification. Missing/failing entropy sources fail
atomically. Secret keys, temporary randomness and HKDF state are cleansed.

The permanent two-address/two-input transaction is 5477 bytes, uses
one genuine 3309-byte signature and one 1952-byte public key. Both independent
ownership checks pass. The anchor is 5301 bytes, follower 39 bytes, and exactly
one actual ML-DSA verification authorizes both inputs.

## D. Tests and commands

Environment: Debian 13.6 cloud VM, x86_64 AMD EPYC 9V74, three visible CPUs,
about 9.7 GiB RAM, no swap; GCC/G++ 14.2.0, CMake 4.4.4. These are not Ryzen
4600G/WSL2 measurements. CMake was installed locally beneath ignored `work/tools`.
Release flags: `-O3 -DNDEBUG`, C++20, portable crypto. Test checks remain active
independently of `NDEBUG`.

Exact main build/test commands from the research root:

```bash
work/tools/cmake/data/bin/cmake -S src/test/mercaweave -B work/build-release -DCMAKE_BUILD_TYPE=Release
work/tools/cmake/data/bin/cmake --build work/build-release -j4
work/tools/cmake/data/bin/ctest --test-dir work/build-release --output-on-failure
work/build-release/mercaweave_tests
python3 src/test/mercaweave/reference.py
python3 src/test/mercaweave/crosscheck.py work/build-release/mercaweave_tests
work/tools/cmake/data/bin/cmake -S src/test/mercaweave -B work/build-sanitize -DCMAKE_BUILD_TYPE=RelWithDebInfo -DMERCAWEAVE_SANITIZERS=ON
work/tools/cmake/data/bin/cmake --build work/build-sanitize -j4
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 work/tools/cmake/data/bin/ctest --test-dir work/build-sanitize --output-on-failure
work/build-release/mercaweave_bench > src/test/mercaweave/benchmarks.csv
git diff --check
```

- Release CTest: **3/3 passed**. Sanitized CTest: **3/3 passed**, no ASan/UBSan findings.
- C++ summary: **9855 assertions**, **16 positive cases** (15 authorization cases
  plus one derivation suite), **86 negative cases**, **3 structural maximum cases**,
  **8301 malformed-input cases**. No failed assertions.
- Positive cases include 1/2/3/10/53/100/1000 inputs, multiple groups,
  noncontiguous membership, repeated address/nonce payments, genuine mixed
  v2/v3 authorization, deterministic signatures and system-randomized signatures.
- Negatives cover witness lengths/items, reference errors, duplicate keys,
  ownership, network identity, authenticated transaction/manifest mutations,
  wrong key/context/domain, missing tag-length prefix, absent spent context,
  P2SH wrapping, value/format failures, entropy failure and stale-context matches.
- All three derivation purposes are independently cross-checked in Python;
  account/group/network/address separation and invalid purposes are tested.
- Structural maximum tests cover 1024 members, 256 groups and 4096 v3 inputs;
  immediately greater cases reject before ownership hashes or ML-DSA calls.
  These structural fixtures do not contain valid ML-DSA signatures.
- Malformed cases comprise 5301 truncated anchor prefixes, 2000 seeded witness
  mutations and 1000 bounded arbitrary witness buffers. CompactSize boundaries
  include UINT64_MAX, with noncanonical encodings rejected.
- Full application, wallet, daemon and inherited functional suites were not run.
  The selected inherited transaction, script and cryptographic primitives are
  compiled and exercised directly by this isolated suite.

Initial leak-enabled execution failed with:
`LeakSanitizer has encountered a fatal error` and
`LeakSanitizer does not work under ptrace (strace, gdb, etc)`.
This identifies the sandbox limitation. Leak detection is unavailable, not
reported as clean. Address and undefined-behavior checks subsequently passed
with only leak detection disabled.

## E. Benchmarks

All 13 permitted matrix workloads completed. One warm-up and five measured
iterations per workload; median complete-pipeline times. Final CSV was refreshed
after builds and sanitizer checks completed. Timings remain subject to VM
scheduling noise; no cache-isolation or cold/warm-cache claim is made.

V2 comparison bytes come from actual serialized transactions with genuine
ML-DSA signatures verified through the research adapter. Production
`ComputeMercaturaPQAuthDigestV1` and the production script interpreter are not
linked; these are not production-consensus-qualified v2 transactions.

| Inputs | Groups / signatures | Serialized bytes | Median verify ms | Verify calls | V2 adapter bytes | Savings |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 1 | 5,397 | 0.1360 | 1 | 5,364 | -0.62% |
| 10 | 1 | 6,117 | 0.2131 | 1 | 53,145 | 88.49% |
| 10 | 10 | 53,475 | 1.5316 | 10 | 53,145 | -0.62% |
| 53 | 1 | 9,557 | 0.5015 | 1 | 281,432 | 96.60% |
| 53 | 10 | 56,915 | 1.6493 | 10 | 281,432 | 79.78% |
| 53 | 53 | 283,181 | 7.5902 | 53 | 281,432 | -0.62% |
| 100 | 1 | 13,317 | 0.8637 | 1 | 530,955 | 97.49% |
| 100 | 10 | 60,675 | 2.1415 | 10 | 530,955 | 88.57% |
| 100 | 100 | 534,255 | 14.7118 | 100 | 530,955 | -0.62% |
| 500 | 1 | 45,319 | 3.7715 | 1 | 2,654,557 | 98.29% |
| 500 | 10 | 92,677 | 4.8255 | 10 | 2,654,557 | 96.51% |
| 1000 | 1 | 85,319 | 7.9962 | 1 | 5,309,057 | 98.39% |
| 1000 | 10 | 132,677 | 9.0384 | 10 | 5,309,057 | 97.50% |


The 53-input/one-group workload exceeds the 95% wire-byte-reduction target.
Independent-key v3 inputs add about 33 witness bytes per input versus v2;
there is no cross-key aggregation benefit. G=N at 500/1000 is intentionally
not run because it exceeds 256 groups.

The CSV additionally records signing and invalid-ownership rejection medians,
witness/non-witness sizes, BIP141 weight, full-byte fee measure, `4G+N`, actual
signature generation/verification counts, individual ownership checks and
weight-only comparisons. Fees are reported without an extra sigop adjustment;
serialized bytes dominate that adjustment in these fixtures. Witness bytes
include the two marker/flag bytes in `total - stripped`.

Peak measured process RSS: **22,932 KiB**. Per-row RSS is a cumulative process
high-water mark, and CPU milliseconds are cumulative user+system CPU time.
Neither is isolated per-transaction usage. Metadata records exact semantics.

`within_weight_only` does not establish standardness. In particular, the
100-input independent-key v3 case exceeds 400000 weight, and v2 comparisons
from 100 inputs onward exceed it. No policy or block limits were altered.

## F. Git hygiene

Focused tests and diff checks passed before the engine commit. Changes were
inspected; generated binaries, build directories, caches and expanded private
keys were excluded. Public deterministic test seeds in vectors are test data.
The results/report commit is checked separately. The final status is verified
after committing. No push or merge was performed. Original worktree source,
branch and HEAD remain unchanged and clean. Shared Git metadata acquired the
authorized `project` remote, fetched tracking ref/tags and research branch.
No nodes, miners, Qt wallets, RPC, live datadirs or wallet databases were touched.

## G. Limits and next work

This is an offline authorization prototype, not a complete consensus engine or
deployment security certification. It trusts caller-authenticated spent outputs
and a correct independent non-v3 verifier. Chain membership, maturity, locktime
finality, block-wide parse budgets, flags and policy remain external. In-memory
serialization is conservatively capped at 32 MiB for these experiments; this
is a harness restriction, not a proposed consensus rule. Very large mixed-input
workloads need separately bounded harness support.

Remaining qualification includes production-v2 interoperability checks,
independent cryptographic review, broader native fuzzing and leak detection on
a compatible host. Shared-key compromise, public-key linkability, recovery
metadata and manifest coordination remain accepted security trade-offs.

Work Order 02 should begin with independent review and an explicit design for
transaction-scoped immutable authorization, every script-check entrypoint,
cache binding, block resource context and mixed authorization. Production
consensus/wallet/PSBT/Qt changes and all activation work remain deferred and
require a separate work order. No activation schedule or height is proposed.

## Work Order 02 follow-up (2026-11-02)

The original Work Order 01 evidence above is preserved. Current hardening,
native production-v2 interoperability, malleability, staged resource measurements
and 7/7 release/sanitizer CTest qualification are documented in
`mercaweave-v0.1-security-qualification.md`. New artifacts use separate names;
the original vectors and benchmark CSV remain byte-for-byte unchanged.
