# Post-WO08 CI repair evidence

These are new qualification records for the initial source repair at
`1ffb0021c2d37b286fca57729218628a34550904` and Qt newline-only follow-up at
`95833e792079ee828b85960e2ef1479a0ff01746`. Historical WO01–WO08 files are
unchanged. See `doc/design/mercaweave-v0.1-ci-qualification.md` for conclusions.

- `baseline-jobs.json`: all eight final results of failing Actions run 388.
- `repair-attempt-389.json`: initial repair results, newly exposed Qt diagnostic
  and superseded-run cancellations; these cancellations are not passes.
- `source-ci-jobs.json`: all eight final results of the repaired source run.
- `source-ci-test-summary.json`: 187 CTest targets and 45 native functionals per
  native platform, plus the 20 Windows turnover repetitions.
- `source-ci-log-review.json`: successful remote job log digests and diagnostic review.
- `local-qualification.json`: local test counts, timings, native profile and boundaries.
- `qt-supplement.json`: warning-clean ordinary/research probes and 15 additional
  Qt executions after the final newline-only source repair.
- `local-log-hashes.json`: full local and baseline log SHA256s. Raw runner logs,
  binaries, runtime wallets and toolchains remain in ignored `work/ci-repair/`.
- `historical-preservation.json`: starting worktree state and all 136 prior file hashes.
- `boundary-check.json`: ordinary compile flags, four rejected access attempts,
  shared implementation/record preservation and five-network parameter equivalence.
- `domain-probe.json`: original-fails/repaired-passes macro collision compilation.
- `decimal-control.txt`: 25 comparisons with the original std::stoull parser.
- `independent-vectors.json`: independent parsing/hash comparison with locked values
  and historical native logs; this is not an independent ML-DSA implementation.
- `native-functional.csv`: a new LF-normalized copy of this run's functional summary.
- `local-warning-review.json`: diagnostics retained from intermediate and final builds.
- `reproduction-configurations.json`: build options, tool versions and command arrays.

Reproduce on a Linux worker with the native project prerequisites and Qt development
SDK installed. The session kept tools under ignored `work/` paths; adapt those
paths to local installations. Full setup instructions remain in the existing
research qualification documents. Configure each standalone build using
`cmake -S src/test/mercaweave -B <build>` with the options in the configuration
record, then build with `-j2` and run CTest. The sanitizer environment is recorded
explicitly, including the existing leak-detection limitation.

The real native component driver is
`src/test/mercaweave/security_audit/build_native.py`; it requires a configured and
built ordinary node tree with unit-test support. The actual Rust lint runner is
`cargo run --manifest-path test/lint/test_runner/Cargo.toml`, with the recorded
tool versions on PATH. Use fresh runtime/result paths for any rerun. Do not run
historical evidence collectors into their archived output directories.

Remote platform results come from actual GitHub jobs, not local cross-platform
simulation. The report/evidence-only containing commit is checked again before
handoff. Its final SHA and allocated Actions URL are recorded in the external
final receipt, avoiding a report that attempts to contain its own commit hash.
