# B2 CI recovery ledger

Starting branch: `hardening/pq-p2p-b1-identity-lifecycle`.
Starting SHA: `10107ecdf837b16bf74f046b71dbae9df1f82f7a`.
Working branch: `hardening/pq-p2p-b2-ci-resilience`, created from that exact B1
checkpoint. The original branches/history are preserved. No Round A branch was
available at the initial recheck. Pushes use `project` only.

## Original run

[CI run 40277679083](https://github.com/Mercatura-Core/Mercatura-Core/actions/runs/40277679083)
was the latest B1 run at inspection, at the exact starting SHA. GitHub reported
Windows native MSVC, macOS native ARM64 and lint failed. Linux native headless,
Windows UCRT cross-compile and both macOS cross-compiles passed. Native Windows
and macOS completed all 184 CTest entries successfully (the existing optional
script-assets entry was skipped) before their functional failures.

| Job / evidence | Root cause and reproduction | Correction / verification |
| --- | --- | --- |
| Windows native MSVC, job `121424051113`; four positive PQ functional variants | Copied build-tree scripts used `Path(__file__).parents[2]`, looking for a source-only JSON fixture under `build-win-msvc/src/test/data`. The log records `FileNotFoundError`. Reproduce with copied functional scripts and no build-tree vector copy. | Load the unchanged authoritative fixture through generated `environment.SRCDIR`. Copied-layout V2 passed; the complete V1/V2/transition/B1/negative matrix is tracked below. |
| macOS ARM64, job `121424051440`; `feature_pq_transport.py` | Full V1 `verifychain(4, 0)` hit the framework's 30s HTTP timeout after persistent rotation/restart, with three functional tests running. No node-debug artifacts were uploaded, so the historical failing endpoint/progress cannot be recovered conclusively. | Use a bounded 180s proxy for these full-validation calls only, scaled by the existing timeout factor. Validation level/depth, tip matching and assertions are retained. Local targeted V1 validation completed on node 0 in 91.453s and node 1 in 104.401s under compile contention; debug logs showed advancing verification progress and no database inconsistencies. New logs identify endpoint, height, schedule and elapsed validation time. |
| Lint, job `121424085955`; dependency cycle | `net.cpp` included the derived PQ adapter, which included `net.h` to derive from `Transport`: `net -> net_pq -> net`. | Move the unchanged context/work declarations to `net_pq_context.h`; the network layer calls a factory whose implementation owns the adapter dependency. Pin selection, source grouping, shared ownership, worker bounds and shutdown semantics remain unchanged. Dependency-cycle lint has no new exclusion. |
| Lint; direct `std::filesystem` | Identity loader bypassed the repository path abstraction. | Use `util/fs.h`, `PathFromString` and `PathToString`; retain descriptor-relative POSIX checks/publication and Windows fail-closed persistence. |
| Lint; four Python E402 findings | Directly executable developer scripts bootstrap the source framework path before importing it. | Document the necessary bootstrap and apply E402 exceptions to precisely those four imports. Other import lint stays enabled. |
| Lint; locale-dependent formatting/parsing | Project-owned developer/test code used `fprintf`, `std::to_string` and `stoi`. | Raw stderr text output, classic-locale `util::ToString`, and bounded strict `from_chars` parsing. Frozen primitive/packet fixtures remain unchanged. |
| Lint; eight Markdown links in pinned ML-KEM README | The vendored source subset retains upstream references to omitted examples/assembly. The Python exclusions already covered ML-KEM, but the Rust lint registry omitted it. | Register exactly `src/crypto/mlkem_native` alongside the existing external-source exclusions. Project wrappers and Mercatura documentation remain checked; no vendor files are rewritten. |

Detailed original job logs are retained locally under
`/workspace/work/b2/evidence/ci-original-<job-id>.log`. They are not bulk-published.

## Qualification status

Local recovery validation passed on Linux (GCC 14.2, existing Release build,
two-CPU quota, 8 GiB memory ceiling):

| Run | Actual result |
| --- | --- |
| Full repository lint runner | Passed, including all Python lint scripts, Markdown, dependency cycles and filesystem abstraction checks; CI-pinned Ruff/MLC/Mypy/Vulture/ShellCheck versions used |
| Focused B1 | 41 enabled cases, 115,536 assertions; exit 0 |
| Adjacent networking/crypto/wallet/script/V2 | 82 cases, 609,272 assertions; exit 0 |
| ASan/UBSan/LSan | 41 cases, 115,710 assertions; exit 0, leak checking enabled, no suppressions |
| TSan | 41 cases, 115,659 assertions; exit 0, no suppressions |
| Copied-layout real nodes | V1, V2-from-genesis, height-20 transition, B1 lifecycle and existing negative scenarios all passed |
| V1 at three-test concurrency | Both nodes verified all 103 blocks at level 4; 48.170s and 66.061s validation calls; full test 405.683s |
| Qt native V2 CPU mining over PQ | CTest 1/1 passed in 31.427s, including restart/tip verification |
| Primitive/packet checks | Frozen primitive checks passed including native ML-KEM/ML-DSA; packet derivation checked 1348 packets without writing fixtures |
| Excluded fuzzer entry point | Rebuilt successfully; no new fuzz campaign |
| Requested-heap probe | Exit 0; context, partial M1 and four-session teardown each retained zero tracked requested bytes |

Focused sanitizers instrument the first-party PQ wrapper/adapter/protocol/storage
and listed tests, not the entire node or linked native crypto dependencies.
They ran in the explicitly authorized compatible non-ptrace execution context.
Local tests preceded the individual fix commits, with the same source changes
present in the working tree; the published code snapshot is `98da2fd2ea03a0fbfc0352d74808e0bae4f03fe4`.

The final B2 evidence JSON identifies commands, exits, source boundaries and
log hashes. A cache-preparation attempt on this cloud
host separately hit a generic `generatetoaddress` RPC deadline; it was not a
fixture-discovery failure. Direct clean-chain copied-script execution avoids
requiring that unrelated cache and preserves the original failed log. The first
concurrent matrix also overlapped a daemon relink with B1's process restart,
causing `PermissionError` at exec. The stable-build B1 rerun passed (40.687s);
the failed orchestration log is retained. Builds and restart tests are now
sequenced, rather than treating this as a transport defect.

Coherent recovery commits:

- `fef08e64b6bf7f0d0928eff0dac168946909740d`: source-config fixture lookup.
- `2589b1dbe76408ebba0adec12df269efc89f3acf`: scoped full-verification timeout.
- `19c0689fe95b31caaef42ae6af800fdc9dd0c84a`: adapter/context factory boundary.
- `f1642450d4c026b7f14884c36592a526d43ca030`: repository filesystem paths.
- `990c7a17cae484e72354963ad3ea17a01ff29da6`: deterministic tool conversions/import lint.
- `98da2fd2ea03a0fbfc0352d74808e0bae4f03fe4`: pinned-vendor lint registry alignment.

The initial checkpoint above preceded native GitHub verification. The completed
recovery gate and its exact source revision are recorded below.

## First recovery run: newly exposed Windows platform contract

[Run 40290213197](https://github.com/Mercatura-Core/Mercatura-Core/actions/runs/40290213197)
at `9c6ab92c8361b57329c6b3fd9f5e7e11a54e7b65` completed with Linux, macOS native,
all three cross-compilers and lint successful. Native macOS full functional
validation therefore passed with the scoped verification timeout. Windows again
passed all 184 CTest entries, then failed the four positive PQ functional cases.

Windows job `121463229425` confirms that fixture discovery is fixed: transaction
relay, block relay, reconnect and malformed-preface checks now complete. The
next operation tries POSIX persistent identity creation, and correctly receives
`Stage A persistent PQ identity storage is not yet supported on Windows; use
ephemeral identities`. This platform boundary was previously masked by the
fixture error. Native Windows persistent storage is intentionally a later task;
it must not be enabled merely to make these tests pass.

The functional test now asserts Windows load/create requests fail closed and
create no identity file, while retaining actual ephemeral reconnect/restart,
wrong-pin rejection/recovery, wallet/chain persistence, full `verifychain(4, 0)`
and exact tip validation. POSIX continues executing the complete persistent
pinning/rotation path. A test-only `--ephemeral-only` option exercises that
restart path on Linux without claiming to emulate Windows ACL/storage behavior.
The ephemeral V2 path and full POSIX B1 V2 path both passed locally after this
change. The next native Windows CI run verified the platform-specific
fail-closed assertions themselves, as recorded below.

## Verified recovery gate

[Run 40298481160](https://github.com/Mercatura-Core/Mercatura-Core/actions/runs/40298481160)
at `c0cd6f3f0091d2e8c6e119cf7b07e3ef1661525e` completed successfully.
GitHub's run and job conclusions were checked at 2026-10-26 10:25:09 UTC.
All seven required jobs passed; the runner-selection metadata job also passed.

| Required job | Job ID | Actual conclusion |
| --- | --- | --- |
| Linux native x86_64 headless | `121490328033` | Success |
| Windows native x86_64 MSVC | `121490262517` | Success |
| macOS native ARM64 | `121490262888` | Success |
| Lint | `121490327983` | Success |
| Windows x86_64 UCRT cross-compile | `121490328040` | Success |
| macOS Intel x86_64 cross-compile | `121490328101` | Success |
| macOS ARM64 cross-compile | `121490328207` | Success |

All three native jobs completed the 184-entry CTest catalog and all 49 selected
functional cases, including ordinary PQ, V2-from-genesis, height-20 transition,
B1 lifecycle and negative transport tests. The existing optional script-assets
CTest entry remains an explicit skip, not an executed pass. Ordinary PQ test
durations were 156s Linux, 134s Windows and 152s macOS. Full validation remains
level 4 over all blocks. Windows confirms fail-closed persistent storage plus
ephemeral recovery; POSIX confirms persistent identity/pin/rotation behavior.
Native Windows persistent storage remains a separate platform qualification.

Commit `c0cd6f3f0091d2e8c6e119cf7b07e3ef1661525e` adds these explicit Windows
contract assertions and the ephemeral restart test path. No storage restriction
was removed to obtain green CI. The prior CI audit/ledger commit is
`9c6ab92c8361b57329c6b3fd9f5e7e11a54e7b65`.

The verified gate receipt and decoded Linux/Windows/macOS job logs are retained
locally with SHA-256 identifiers in the final B2 evidence. A 120-second
three-node attempt before this gate was rejected by automatic approval review
and did not execute. Sustained B2 workloads started only after the successful
gate was recorded; the rejected action was not bypassed.

## B2 source confirmation

After the B2 tooling/test commits were published,
[run 40311265804](https://github.com/Mercatura-Core/Mercatura-Core/actions/runs/40311265804)
at `61d7009edc1950a8294ac0133349fd7e75bf2fef` also completed successfully.
All seven required jobs passed, as did runner selection. The run and job
conclusions were independently retrieved after completion. Native Linux,
Windows and macOS again passed their 184-entry catalog (one optional
script-assets skip) and all 49 selected functional cases.

| Required job | Job ID | Actual conclusion |
| --- | --- | --- |
| Linux native x86_64 headless | `121532778656` | Success |
| Windows native x86_64 MSVC | `121532742825` | Success |
| macOS native ARM64 | `121532742181` | Success |
| Lint | `121532778646` | Success |
| Windows x86_64 UCRT cross-compile | `121532778738` | Success |
| macOS Intel x86_64 cross-compile | `121532778526` | Success |
| macOS ARM64 cross-compile | `121532778605` | Success |

The B2 fault/barrier/capacity runners remain excluded Linux-only developer
targets; their normal and sanitizer results are local qualification, not
additional Windows/macOS CI coverage. Ordinary cross-platform PQ and lifecycle
functional coverage remains enabled. No CI job or validation assertion was
disabled to obtain these results.

B2 commits after the recovery gate:

- `0c27687fc5ca7da23babbc197f8323ba0c463dfd`: admission fairness and worker/queue capacity cases.
- `76f27806a038e44c72ecd6323c841ad7e1e5c6f7`: bounded churn and per-cycle allocation accounting.
- `1b11cdb6b7ddd487329b2c85db1dd39cc6f70bea`: bounded multi-node endurance/recovery workload.
- `b79debb7dba36cca0babc34a5894a96f35a3c769`: verified recovery ledger and resource model.
- `61d7009edc1950a8294ac0133349fd7e75bf2fef`: native-funded payment, mining-time reconnect and explicit validation-thread budget.

The final validation/evidence documents are a documentation-only descendant of
that qualified source. Git history and the separate publication receipt identify
the exact publication SHA without attempting a self-referential evidence hash.
