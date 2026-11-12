# M2 local evidence

Public summaries only. No wallet datadirs, keys, RPC cookies or binaries are
included. Initial O2 observations are explicitly labeled and retained to explain
the subsequent research-matched O3 build change.

- `final-o3-performance-*`: matched native CPU hash O3 controls and combined runs.
- `cpu-research-protocol-results.json`: archived 6-worker/20-second/default-page
  protocol, including a contemporaneous unchanged frozen controller observation.
- `gpu-research-protocol-60.*`: one warmed, cursor-aligned 60-second CUDA trial;
  this does not establish a 600-second sustained rate.
- `transitions-o3-*`, `wallet-unload-o3-*`, `v2-native-o3-*`: final production
  setting native/Qt/PQ and independent-block evidence.
- `archive-*-protocol.json` and `*-result.json`: corpus commands and full-pad parity.
- `*-summary.txt`, sanitizer logs and host-runtime logs: regression evidence.
- `*-identities.json`, `*-preservation.json`, source diffs and compiler commands:
  provenance and scope checks.

The benchmark build/runner scripts expect the repository as their working
directory, an existing configured `build-m2`, and a writable `work/m2`. They
write only there. Build controls from the pinned starting Git commit with
`build-baselines.py`, then build the matched hash-O3 control with
`build-o3-control.py`. The latter prepares `work/m2/benchmark-native-o3.py`.
Benchmark runners need physical GPU access; their ordinary-page policy is local
to the benchmark process. Frozen inputs remain external and read-only.

The complete report describes historical failed harness/environment attempts and
their successful resolutions. GitHub CI remains owner-managed.
