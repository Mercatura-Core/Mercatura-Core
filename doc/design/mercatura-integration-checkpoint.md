# MercaHash V2 / PQ P2P preservation checkpoint

This checkpoint follows the October 25, 2026 integration handoff. It preserves
both development histories; it does not certify an integrated release or authorize
public-network PQ deployment. Earlier Stage A evidence stays associated with its
original source revision and is not reclassified as integrated validation.

## Histories and merge

Repository: `Mercatura-Core/Mercatura-Core`. Authorized remote: `project`, URL
`https://github.com/Mercatura-Core/Mercatura-Core.git`. The connected GitHub account
is `Mercatura-Core`; the connector reports push/admin permission. Direct Git
fetch and a push dry run work in this session. `origin` was not pushed to.

Integration branch: `integration/mercahash-v2-pq-p2p-prelaunch`.

| Reference | Verified source commit |
| --- | --- |
| Latest `feature/mercahash-v2-native-mining` | `cc5db1df34d362bdfecd11003404bc307c2219b0` |
| Local PQ `v0.1` | `ea389cbdd8129a9b2a1ead0ca97590b291b771b3` |
| GitHub `v0.1` / common merge base | `631886dd5159614b628ced3fe4fa1e289ce19451` |
| Preserved local `work` | `631886dd5159614b628ced3fe4fa1e289ce19451` |

A separate worktree, `/workspace/Mercatura-Integration`, starts at the verified
native-mining tip. The normal `--no-ff` merge has the mining tip as first parent
and PQ tip as second parent. All thirteen PQ-side commits since the common base
are retained with their original hashes. No squash, rebase, reset, force-push or
source-branch overwrite is part of this checkpoint. The older
`feature/mercahash-revision-v2` branch was not substituted.

The merge has **no conflicts**. The automatically merged crypto/test CMake lists
and functional runner retain both registrations. Review/diff checks confirm the
merge contributes no changes relative to the mining parent in consensus,
validation, wallet, script, chain parameters, proof-of-work, mining implementation,
Qt implementation or `net_processing.cpp`. Existing mining changes, including
its own chain configuration, remain exactly as present at the authorized source
tip; no additional genesis or activation change was introduced here.

## Additional hardening commits included

| Original commit | Validated change |
| --- | --- |
| `6edc6437c8f9ee3a0f9afcdcb26165ddb3623154` | Set/validate owner-only identity-file creation permissions before secret writes; filesystem/recovery regressions |
| `c7cf7b80bb4d1a724def7924627ee29d695a0d11` | Charge silent inbound transports to pending admission quotas |
| `62c75d9b4ae5cbdd844a205802435a13eab21b1e` | Canonical identity/pin/command parsing, deterministic checks and structured guided fuzz infrastructure |
| `184bb13b4a89934c41b78b8a81ae1dec0aaab808` | Exception-safe quota leases; allocation-failure regression/probe |
| `ea389cbdd8129a9b2a1ead0ca97590b291b771b3` | Reject malformed public KEM encodings before avoidable key generation/decapsulation |

These follow the seven commits through `4ae9979`. Their earlier hardening runs
are PQ-line evidence, not a complete validation of the newly combined node.
Full P5/P6/P7 hardening and the requested independent-review package remain
unfinished; preservation/integration superseded that work's immediate priority.

## Unfinished work preserved outside the merge

The original `/workspace/Mercatura-Core` worktree remains on local `v0.1` at
`ea389cb`. It retains the exact original pending changes:

```text
 M src/test/CMakeLists.txt
?? contrib/devtools/pq-sanitizer-probe.cpp
?? test/functional/test_framework/pq_peer.py
```

The CMake addition is an excluded test-only primitive bridge. The Python endpoint
is unfinished and has not been qualified against real nodes; it is not included
in the integration branch. The sanitizer capability probe is also retained
without a completion claim. A binary tracked-file patch, copies of untracked
sources and before/after status records are saved under the original worktree's
ignored `build-pq-hardening-results/integration-preservation/` directory. Byte
comparisons confirmed these files and the pending patch were unchanged.
No automatic stash or discard occurred. Existing logs, build directories,
wallets, blockchain directories and mining worktrees were not deleted.

## Validation actually run on the integrated tree

Host: managed Debian 13 x86_64, GCC 14.2.0, two-core CPU quota, 8 GiB memory
limit. GUI/CUDA/OpenCL hardware qualification is not inferred.

| Check | Result |
| --- | --- |
| Git parent/common-base and source-boundary review | PASS; expected independent histories, no conflicts |
| `git diff --cached --check` | PASS |
| CMake Release configure, GUI/IPC/ZMQ off | PASS |
| Build `pq_stagea_tests`, two jobs | PASS; includes the integrated crypto library with both MercaHash versions and PQ primitives |
| `pq_stagea_tests --log_level=test_suite --report_level=detailed` | PASS 8 cases: five protocol/crypto/packet and three identity cases |
| `pq-primitive-check.py --work-dir build-checkpoint/primitive-check --mlkem-source src/crypto/mlkem_native` | PASS 287 checks, including independent HKDF and wallet/transaction golden vectors |
| `pq-packet-vectors.py` | PASS 1,348 packets, three rekey boundaries per direction |
| Existing `mercahash_v2_tests.cpp` linked to this build's crypto library | PASS 3 cases / 50 assertions: permanent V2 vectors, allocation/canary/size checks and frozen V1 nonce vectors |

The mining unit source was compiled unchanged and linked with the already built
Boost main and integrated `libbitcoin_crypto.a`:

```sh
c++ -std=c++20 -O2 -Ibuild-checkpoint/src -Isrc \
  -isystem /workspace/work/build-deps/usr/include \
  -c src/test/mercahash_v2_tests.cpp \
  -o build-checkpoint/mercahash-v2-checkpoint-tests.o
c++ build-checkpoint/src/test/CMakeFiles/pq_stagea_tests.dir/__/__/contrib/devtools/pq-stagea-test-main.cpp.o \
  build-checkpoint/mercahash-v2-checkpoint-tests.o \
  build-checkpoint/lib/libbitcoin_crypto.a -pthread \
  -o build-checkpoint/bin/mercahash-v2-checkpoint-tests
build-checkpoint/bin/mercahash-v2-checkpoint-tests --report_level=detailed
```

Full combined daemon/Qt builds, ordinary socket networking suites, complete wallet
regressions, hardware CPU/GPU parity, integrated real-node PQ relay, reorg,
endurance and full-adapter sanitizer qualification are **DEFERRED**, not passed
and not labeled as safety-blocked. This is intentionally the lightweight source
preservation checkpoint requested in the latest handoff.

## Interruption and environment ledger

| Observation | Classification and effect |
| --- | --- |
| User-supplied screenshot: “This content can't be shown”; “We take extra care with some cybersecurity requests. If you're doing authorized security work, apply for Daybreak to get broader access.” | Product safety/access message. Exact internal trigger and affected generation cannot be inspected. It does not establish that any particular test ran or failed. No attempt to bypass that restriction is part of this checkpoint. |
| LeakSanitizer reported “LeakSanitizer has encountered a fatal error” and “LeakSanitizer does not work under ptrace (strace, gdb, etc)” in the normal sandbox | Sanitizer runtime/process-inspection restriction on earlier PQ-line runs. Tool-authorized execution outside that sandbox subsequently completed the core and six bounded guided campaigns with leak checking. Those runs precede this integration and are not integrated sanitizer certification. |
| Historical Git proxy failures | Historical network restriction; direct Git fetch and push dry run now succeed. No hidden credential discovery, alternate route or security-control bypass was used. |
| GitHub fetch tool rejected a slash-encoded branch URL with `INVALID_ARGUMENT` | URL validation error, not a test/security failure. The dedicated branch-search tool and official Git reference read verified the branch. |

No listed integrated check was blocked by a safety restriction. Any future
explicitly blocked test must be recorded as **NOT EXECUTED — ENVIRONMENT
RESTRICTION**, with its actual error, rather than inferred from a banner. No
production readiness, independent audit or prolonged soak is claimed.

## Publication and next checkpoint

The intended branch URL is
`https://github.com/Mercatura-Core/Mercatura-Core/tree/integration/mercahash-v2-pq-p2p-prelaunch`.
Publication must be verified after committing through both Git and the authorized
GitHub connection. A remotely created name alone is not evidence that local PQ
objects were transferred. Verify the exact merge SHA, both ordered parents,
representative source/test files and unchanged original branch heads.

If publication cannot complete, an authorized developer can publish this local
branch without rewriting history:

```sh
git remote get-url project
git push project refs/heads/integration/mercahash-v2-pq-p2p-prelaunch:refs/heads/integration/mercahash-v2-pq-p2p-prelaunch
git ls-remote project refs/heads/integration/mercahash-v2-pq-p2p-prelaunch
```

Next: establish a dedicated combined integration-validation baseline before
resuming extended hardening. Retain explicit isolated-regtest PQ opt-in and all
public-network rejection gates. Independent AKE review, platform/hardware tests,
decentralized bootstrap, combined wallet/network/mining/Qt qualification and
explicit deployment approval remain separate gates. This branch does not
implement the final mainnet/testnet activation policies.
