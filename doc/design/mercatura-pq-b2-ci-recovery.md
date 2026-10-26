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

**GitHub CI recovery remains pending until a new run's actual job conclusions
are recorded.** Local results are not substitutes for native Windows/macOS CI.
Extended B2 execution has not started at this checkpoint.
