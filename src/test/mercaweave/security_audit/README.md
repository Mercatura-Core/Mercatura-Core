# WO08 internal security qualification

Run these scripts only against disposable local build/test directories. No
ordinary CMake target includes this directory. Positive grouped execution uses
the guarded in-process fixture helper; ordinary daemon/wallet activation stays
unavailable on every network, including regtest.

`build_native.py` refreshes and links the real root native libraries with their
actual flags, adds the existing six production-path cases, five new audit cases
and three unchanged MercaHash vector cases. This 14-case runner differs from
WO07C's six component plus eight mining cases. `run_native.py` bounds execution
and uses wait4 for the test child's own CPU/RSS. Use unique labels to preserve
datasets. `build_tsan.py` supplies a dedicated instrumented native build; GNU
thin archives reduce duplicate build storage without changing compiled code.

`allocation.cpp` interposes C++ allocation only in the audit executable. Faults
are one-shot and thread-local. Actual wallet global allocation injection is
excluded in DEBUG_LOCKORDER builds because the attempted inherited diagnostic
environment terminates; debug checks stay enabled. Actual Debug wallet signing,
concurrency, context and PSBT allocation tests still run. Release, ASan/UBSan
and TSAN retain all 512 wallet failure points. C malloc, sustained OOM and every
native exception path are not qualified by this mechanism.

`tool_probes.py` deliberately produces a TSAN race in a separate control child;
LeakSanitizer positive controls expose this environment's fatal ptrace limit.
`conditional_unwind_probe.py` compares smaller original/safe witness-construction
idioms; both minimal forms pass here, so it does not independently explain the
actual-library ASan exception-unwind failure. Original failing actual-library
logs and corrected allocation regression are retained. No sanitizer diagnostic
is globally suppressed to obtain the final passing results.

`independent_vectors.py` imports no project code. It parses actual transaction
and spent bytes, recomputes commitments, canonical manifest, component/body/final
hashes, verifies ten locked fixture values, and compares four signed vectors
byte-for-byte across builds. It is not a second ML-DSA implementation.

`inventory.py` checks 89 historical files against the immutable starting commit.
`boundary_audit.py` retains WO07C negative compilation and symbol checks with an
exact reviewed HKDF temporary-cleanup exception. `publication_audit.py` checks
original-worktree integrity, allowed production changes, expected remotes and
research-history/current-content secret patterns. It performs no push.

`collect.py` copies only compact text evidence into `final-security-artifacts`;
wallets, build products, executables and raw private state stay under ignored
`work/`. The final design report records limitations, exact verdicts, publication
gates, source checkpoints and remaining activation requirements.

MercaHash V2 activation and subsequent network stability take precedence over
any separately authorized future MercaWeave testnet decision. This work requests
no height, deployment bit, runtime switch, activation tag or network exception.
