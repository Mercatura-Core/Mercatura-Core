# MercaWeave v0.1 — post-WO08 CI qualification and research freeze

Date: 2026-11-04 UTC.

## A. Original WO08 commit

`a9c0f03ef997ef7dc3f2a3d11afa1e02c195d07c`, research branch
`research/mercaweave-v0.1`, `/workspace/Mercatura-MercaWeave-v0.1`.
Both worktrees began clean. Original `/workspace/Mercatura-Core` remains on
`work` at `4b88b619fb1b08c7f30112a38eb471880b2a6af4`.

## B. Original Actions run

[CI #388](https://github.com/Mercatura-Core/Mercatura-Core/actions/runs/41769365713).
All eight jobs were complete: two passed and six failed. Full logs for all six
failed jobs were downloaded and inspected before edits, retained under
`work/ci-repair/baseline-<job-id>.log`.

## C. Original failures by job

| Job | Original result | Observed cause |
|---|---|---|
| Windows native x86_64 MSVC | FAIL | Math-header DOMAIN macro expands the local identifier in wallet/pqgroup.cpp. |
| macOS native ARM64 | FAIL | Same macro collision. |
| Windows x86_64 UCRT cross-compile | FAIL | Shared kernel omits grouped implementation objects referenced by validation.cpp. |
| macOS Intel x86_64 cross-compile | FAIL | Missing kernel symbols; wallet DOMAIN collision also appears. |
| macOS ARM64 cross-compile | FAIL | Missing kernel symbols; wallet DOMAIN collision also appears. |
| lint | FAIL | 392 Ruff diagnostics plus filesystem, whitespace, script modes, suite names, dependency cycles, guards, includes and locale conversions. |
| Linux native x86_64, headless | PASS | Its success did not qualify the failing independent kernel consumers. |
| [meta] determine runners | PASS | Runner preparation succeeded. |

## D. Root causes

Three principal groups account for the six failing jobs. A short local name
collides with a platform macro. The self-contained bitcoinkernel builds its own
validation/interpreter objects and had no source/dependency providing the grouped
functions, while the normal node obtained them from bitcoin_consensus. The
research sources also had accumulated repository-style violations, including
real include-graph cycles and an immutable CSV with original CRLF bytes.

The first repair push exposed an additional UCRT warnings-as-errors failure in
the Qt model. Its complete job log was retrieved. The model and panel now split
adjacent control statements onto separate lines; exact non-whitespace source
identity is verified. Four local ordinary/research compiler probes reproduce
the original warnings and pass after repair with misleading indentation fatal.

## E. Source and build-system fixes

Rename DOMAIN to records_authentication_domain, preserving the exact string
`Mercatura/PQGroupCWalletRecords/v1` and all authentication inputs. A controlled
`-DDOMAIN=1` compilation fails on the original source and passes on the repair.

A normal-ABI bitcoin_pqgroup OBJECT target compiles pqgroup.cpp and
pqgroup_context.cpp once per configuration, with PIC for shared consumers.
Both bitcoin_consensus and bitcoinkernel embed those objects and explicitly
depend on their target. The explicit dependency also resolves an initial local
Make scheduling failure. No implementation is copied into validation.cpp, no
unresolved reference is suppressed, and no source is compiled redundantly into
two implementation archives. Local shared-kernel linking uses
`-Wl,--no-undefined`; all four previously missing functions are defined.

Standalone research CMake compiles the shared script/pqgroup.cpp directly;
the old compatibility TU no longer includes a .cpp file. Public record types
move unchanged into pqgroup_records.h, with a compatibility header retained,
breaking wallet include cycles. Research node headers use forward declarations
and explicit consumer includes to break the validation cycle.

## F. Lint fixes

Ruff 0.15.5 formatting produces identical ASTs before/after formatting. Three
unused imports are removed; import bootstrap paths are reordered explicitly.
Scripts with shebangs have executable mode. Headers use the exact BITCOIN_
guards required by the repository; C++ includes use source-root bracket paths.
Shared includes are hoisted from mutually exclusive branches without losing
dependencies. Native fs wrappers and explicit path/stream conversions replace
raw filesystem use in research wallet tests. Diagnostic integer formatting uses
strprintf. A locale-independent decimal-prefix helper preserves the original
base-10 command-line values and exception classes in 25 comparative controls.
Boost suite names are unique and follow their source filenames.

One precise whitespace exception preserves the historical WO07C provenance CSV:
`src/test/mercaweave/production-path-artifacts/build-provenance.csv`. Before
excluding that file, lint requires its complete original blob hash
`f0077af11ea76a72bd16d0aac2360a642d86660d`. This avoids rewriting research
evidence. No directory, lint rule, workflow job or platform matrix is disabled.

## G. Local tests executed

| Check | Result |
|---|---|
| Release standalone CTest | 35/35 |
| Debug standalone CTest | 35/35 |
| ASan/UBSan standalone CTest | 35/35, leak detection disabled for the previously documented ptrace limit |
| Actual native component runner | 14/14 cases, 23,945 assertions |
| Native-v2 functionals | 3/3: PQ reorg, PQ PSBT, wallet encryption/persistence |
| Actual repository Rust lint runner | PASS, including all Python linters; pinned CI Ruff/mypy/ShellCheck/mlc versions installed locally |
| Shared kernel with unresolved symbols forbidden | PASS |
| Forced DOMAIN macro control | Original fails, repaired source passes |
| Decimal CLI grammar controls | 25/25 match original values/exception classes |
| Negative ordinary compilation | 4/4 reject guarded access, capability construction, private wallet issuance and private Chainstate capability access |
| Native chain parameters | 30 rows across main/test/testnet4/signet/regtest match historical evidence |
| Qt follow-up after newline-only repair | 5/5 tests in each of Release, Debug and ASan/UBSan, plus four warning-clean compiler probes |

The functional runner uses two jobs for the worker's two-CPU cgroup quota while
instrumented builds run. No inherited full Bitcoin functional suite is claimed.

Reproduce from the research root:

```bash
for mode in release debug sanitize; do
  work/tools/cmake/data/bin/cmake --build "work/build-$mode" -j2
  ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
    work/tools/cmake/data/bin/ctest --test-dir "work/build-$mode" --output-on-failure
done
cargo run --manifest-path test/lint/test_runner/Cargo.toml
python3 src/test/mercaweave/security_audit/build_native.py \
  --build work/wo07-native-release --output work/ci-repair/native-release
work/ci-repair/native-release/wo08_native_tests --log_level=test_suite \
  --report_level=detailed --color_output=no -- \
  -testdatadir="$PWD/work/ci-repair/reproduction-native-runtime"
```

See `src/test/mercaweave/ci-artifacts/` for configuration options, tool paths,
native profiles, source/negative compile evidence and full-log hashes. The local
lint tool paths must be placed on PATH as recorded there. Use fresh runtime paths.
Existing older qualification scripts
retain their historical scope; do not rerun their collectors into old evidence.

## H. Regression results

105 full CTest executions (35 per configuration), 15 focused Qt reruns after the
follow-up, the 14-case genuine component runner and three ordinary native-v2
functional scripts pass. Local source/private API
checks pass. All 136 historical WO01–WO08 evidence/report files retain their
original SHA256. Existing cryptographic engine/context/PSBT implementation bytes
remain unchanged. Source-erasure tests compare exact bodies and dependency sets
while accounting only for reviewed include hoisting and diagnostic formatting.

## I. Cross-platform results

| Job | Result | Qualification |
|---|---|---|
| [[meta] determine runners](https://github.com/Mercatura-Core/Mercatura-Core/actions/runs/41786009094/job/126428770189) | PASS | Original runner preparation. |
| [Windows native x86_64 MSVC](https://github.com/Mercatura-Core/Mercatura-Core/actions/runs/41786009094/job/126428770442) | PASS | Build, 20 turnover repetitions, 187/187 CTest targets, 45/45 native functionals. |
| [macOS native ARM64](https://github.com/Mercatura-Core/Mercatura-Core/actions/runs/41786009094/job/126428770492) | PASS | Build, 187/187 CTest targets, 45/45 native functionals. |
| [macOS Intel x86_64 cross-compile](https://github.com/Mercatura-Core/Mercatura-Core/actions/runs/41786009094/job/126428808309) | PASS | Complete original cross-build and packaging checks. |
| [lint](https://github.com/Mercatura-Core/Mercatura-Core/actions/runs/41786009094/job/126428808371) | PASS | Complete repository lint job. |
| [Windows x86_64 UCRT cross-compile](https://github.com/Mercatura-Core/Mercatura-Core/actions/runs/41786009094/job/126428808383) | PASS | Complete original cross-build and linkage checks. |
| [macOS ARM64 cross-compile](https://github.com/Mercatura-Core/Mercatura-Core/actions/runs/41786009094/job/126428808389) | PASS | Complete original cross-build and packaging checks. |
| [Linux native x86_64, headless](https://github.com/Mercatura-Core/Mercatura-Core/actions/runs/41786009094/job/126428808465) | PASS | Build, 187/187 CTest targets, 45/45 native functionals. |

All eight source jobs completed successfully; no required job was skipped,
allowed to fail or left running. Matrix definitions and warning policies are
unchanged. The initial repair attempt 389 had four passes, one Qt failure and
three superseded-run cancellations, recorded separately; it is not the final
qualification run.

## J. Deterministic vector equivalence

The four full signed native vector streams remain byte-identical to WO08
Release/Debug/ASan evidence. SHA256:
`fe2662594401ee195169302580e466aa3b997eabeed12d2be53b11c9a936655c`.
The independent Python parser/hash route reproduces all ten locked fixture
values and all genuine-vector commitments, manifests and digests. No permanent
vector or historical measurement is rewritten.

## K. Witness-v2 compatibility

Ordinary native-v2 signing, PSBT, encryption/persistence and reorg regressions
pass. Existing fees, dust, mining, witness-v2 semantics and chain parameters
are untouched. The records-domain identifier repair preserves the exact MAC
transcript. Path repairs affect isolated research-wallet storage utilities.

## L. Test-only authorization isolation

Ordinary compiler definitions contain no research capability macro. The ordinary
daemon has no fixture factory/capability-constructor symbols. Four real negative
compilations reject forbidden headers and private access. Header changes are
guard-only, public-record definitions retain their exact bodies, and positive
grouped tests remain private in-process fixtures. No production bypass exists.

## M. No network activation

No MercaWeave activation on any network, including regtest. No height, timestamp,
deployment bit, enable flag, unsafe receiving API, genesis/network magic, capacity,
DGWv3, emission, reward or public-mining change. MercaHash V2 and its separate
activation/observation work are untouched. Actual native parameter constructors
for all five network modes reproduce all 30 historical rows.

## N. Local commit

Initial source repair: `1ffb0021c2d37b286fca57729218628a34550904`,
`fix: restore MercaWeave cross-platform CI compatibility`.
Final source repair: `95833e792079ee828b85960e2ef1479a0ff01746`,
`fix: separate Qt control statements for warning-clean builds`.
The containing archival report/evidence commit is documentation only and is
identified by final HEAD and the external final CI receipt, avoiding recursive
self-hashing or predicting the Actions run created by this report's own push.

## O. Remote verification

Only `origin` at `https://github.com/Mercatura-Core/Mercatura-Core.git` and
`refs/heads/research/mercaweave-v0.1` are pushed, without force, tags, merge or
rebase. Live remote SHA verification and the subsequent report-only closure's
matrix are recorded in `/workspace/MercaWeave-CI-final-summary.md` and its JSON
receipt. No PR, release, public-node deployment or activation is created.

## P. Actions qualification run

[CI #390](https://github.com/Mercatura-Core/Mercatura-Core/actions/runs/41786009094)
qualifies the complete source repair at the final source SHA in N. The report-only archival
closure is checked again before task completion; its final containing SHA and
newly allocated Actions run URL are recorded in the final receipt and handoff.
The [research branch run list](https://github.com/Mercatura-Core/Mercatura-Core/actions?query=branch%3Aresearch%2Fmercaweave-v0.1)
also identifies that final publication without predicting its future run ID.

## Q. Final CI verdict

**PASS: eight passed, zero failed, zero pending** for the final source repair.
The report/evidence-only publication is verified again before handoff, and its
exact containing SHA and Actions URL are supplied in the external final receipt.

## R. Limitations and warnings

MSVC and Apple toolchains/SDKs are not installed in this Linux worker; their
actual Actions outcomes, rather than Linux-only inference, establish those
platform results. The existing WO08 restrictions remain: no activated grouped
node qualification or external independent audit, and leak detection remains
unavailable under ptrace. No claim of deployment readiness follows from CI.
Local logs retain SQLite imported-target deprecation, optional XKB/Vulkan discovery
notices and GCC 14 inlined std::vector comparison diagnostics. Successful macOS
cross jobs report Qt plugin/framework deployment warnings. MSVC's vcpkg libevent
port reports that a string replacement made no changes; the dependency build and
all subsequent checks pass. These diagnostics are retained for review; no
suppression is added. UCRT misleading-indentation errors are repaired, with
ordinary/research warning-as-error controls passing.

macOS native reports lock-annotation warnings in unchanged witness-resource,
testnet-capacity and wallet test files, plus one linker warning about repeated
consensus/secp256k1 archive entries. These are not duplicate-symbol errors;
the consensus archive contains each grouped object exactly once, the strict
shared-kernel link passes, and the native unit and functional suites pass.
All selected remote diagnostics and full saved-log hashes are archived.

## S. Clean worktrees

Both worktrees are verified clean after final publication. The original `work`
branch and HEAD remain unchanged. Shared fetch/upstream metadata updates are
limited to the authorized research-branch publication.

## T. Main untouched and development freeze

No commit or merge modifies main, master, work or project/v0.1. No tag or release
is created. **FROZEN FOR NOW.** This is a development-priority freeze. MercaWeave
remains shelved pending MercaHash V2 activation and subsequent network stability
observation; no consensus activation or release is authorized by this freeze.

MercaHash V2 remains the active consensus priority, followed by the user's network
stability observation. Real-node grouped-v3 testing, an independent audit and
explicit activation authorization remain separate future requirements. No
additional MercaWeave feature or activation work begins here.
