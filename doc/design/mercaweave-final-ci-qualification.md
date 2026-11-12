# MercaWeave final CI qualification and local handoff

This is the CI stabilization record. It does not authorize public activation,
merge, release, installers, deployment, or native CPU/GPU miner implementation.
Independent consensus and wallet security approval remains a separate gate.

## Source and publication

The task started on clean branch integration/mercaweave-v0.1-upgrade, local
6738ba8e200b1487a03d90c521dae428373a1995, remote
c93806131232f5df65958102892d29007c5ff773. Fetch and ancestry checks showed
zero remote-only and exactly two local-only commits:

- 1ecc222faa195ef1d9478147f55d94ede7e0777a: preserve coinbase maturity when
  the activation-boundary test rewinds from height 110 to 108.
- 6738ba8e200b1487a03d90c521dae428373a1995: previously completed owner
  regtest helper, optional test-only Qt probe, instructions and public evidence.

Both commits were reviewed and published by normal fast-forward under the new
work order's explicit authorization. Exact remote SHA was checked after pushing.
Their cumulative production src/ and workflow diffs are empty. The test fix
selects a native v2 funding coin with at least 103 confirmations while preserving
the exact inactive-v3 rejection assertion; it changes no node validation.
The helper reuses the existing three-node fixture and real PoW, with no new
production capability. All 34 previous handoff evidence hashes remain intact.

## Original Linux failure and bounded retry

Original source: c93806131232f5df65958102892d29007c5ff773.
[Dedicated run 43043090273](https://github.com/Mercatura-Core/Mercatura-Core/actions/runs/43043090273),
original Linux job 130599807180, step **Linux dependencies**.
The actual decoded job log was retrieved again for this task. The command was:

~~~bash
sudo apt-get update
sudo apt-get install --no-install-recommends -y build-essential cmake ninja-build python3 \
  libboost-dev libevent-dev libsqlite3-dev libqrencode-dev \
  qt6-base-dev qt6-tools-dev qt6-tools-dev-tools
~~~

The Ubuntu 24.04 GitHub-hosted image used
mirror+file:/etc/apt/apt-mirrors.txt; apt tried its Azure Ubuntu archive and
official Ubuntu fallbacks. It repeatedly reported
"Temporary failure resolving 'azure.archive.ubuntu.com'" during both index and
package retrieval. The Microsoft package repository fetched successfully.
Index retrieval ended with warnings; package installation then exited **100**.
Configuration, compilation, unit/Qt and functional tests never executed.
This was not a passing Linux gate and did not demonstrate a source failure.

The workflow has no project-level retry wrapper; apt's log already showed
multiple Ign attempts and mirror fallback. Package names and platform guards
were correct. No untrusted mirror, TLS/signature bypass, compiler relaxation or
test skip was introduced. Exactly one failed-job retry was requested through
GitHub Actions. New Linux job 130888528316, attempt 2 of the same run, used the
unchanged source and dependency operation on a fresh runner. Dependencies and
configuration, build, unit/Qt and native functional tests all passed. CTest
selected 195: 194 passed, zero failed, one existing script-assets skip
(2085.92 seconds). All 59 native functional cases passed, zero failed/skipped
(1251 seconds). Windows/macOS successful results were retained, not rerun.
[Original diagnosis](mercaweave-final-ci-qualification-artifacts/original-linux-diagnosis.json)
and [successful retry evidence](mercaweave-final-ci-qualification-artifacts/ci-job-130888528316.json)
keep source identity and results separate.

Rerunning the identical dependency operation resolved the observed Linux failure.
The evidence supports a transient runner/mirror-resolution incident; no persistent
repository dependency defect was established. No workflow, mirror, retry/backoff,
package list or production code change was needed.

## Qualification on the published handoff source

Source: 6738ba8e200b1487a03d90c521dae428373a1995.
[Standard CI 43129275504](https://github.com/Mercatura-Core/Mercatura-Core/actions/runs/43129275504).
[Dedicated qualification 43129275526](https://github.com/Mercatura-Core/Mercatura-Core/actions/runs/43129275526).

| Job | Build | Unit/Qt selected; passed/failed/skipped | Unit seconds | Native passed/failed/skipped | Native seconds |
|---|---|---|---:|---|---:|
| Dedicated Linux x86-64, 130917332446 | PASS | 195; 194/0/1 | 2098.14 | 59/0/0 | 1254 |
| Dedicated Windows MSVC, 130917332087 | PASS | 193; 192/0/1 | 1759.04 | 59/0/0 | 1118 |
| Dedicated macOS ARM64, 130917332382 | PASS | 193; 190/0/3 | 2301.81 | **58/1/0** | 1239 |
| Standard macOS ARM64, 130889088574 | PASS | 188; 187/0/1 | 2237.93 | 59/0/0 | 1312 |
| Standard Windows MSVC, 130889088605 | PASS | 188; 187/0/1 | 1610.84 | 59/0/0 | 1133 |
| Standard lint, 130889140842 | PASS | Repository lint PASS | — | — | — |

Standard CI passed. Dedicated qualification **failed** because of the single
macOS functional case described below. All full source SHAs were verified in
checkout logs. [Per-job evidence](mercaweave-final-ci-qualification-artifacts/)
records compiler identities, selected cases, durations and existing skips.
Every CTest job skips script_assets_tests because its optional external corpus
is absent. Dedicated macOS also retains the two previously documented
native Qt lifecycle skips: test_mining_app-qt-1 and test_mining_app-qt-2.
No new skip was introduced.

### macOS reindex deadline and test-only repair

The failing case was mercatura_mercaweave_multinode.py, seed
4475444380715087524. It passed native v2/v3 funding, negative authorization
vectors, grouped relay/confirmation, mixed inputs, receiver/validator restarts,
grouped mempool persistence and wallet reload/rescan. During the next node-2
restart with -reindex, the framework failed the predicate
getmempoolinfo()['loaded'] after exactly 60 seconds. The remaining 58 cases
passed. This was neither a build failure nor an observed signature/consensus
rejection. [Failure evidence](mercaweave-final-ci-qualification-artifacts/macos-reindex-failure.json)
preserves the actual traceback. Node debug logs were not part of the workflow's
three uploaded logs, so the original node's eventual replay completion cannot
be established from retained CI artifacts.

The fixture sets rpc_timeout=240, but TestNode.wait_for_rpc_connection() uses a
separate generic 60-second import-completion predicate. A successful RPC
connection therefore still leaves reindex exposed to a shorter readiness limit.
Existing native reindex/storage-recovery tests already separate RPC readiness
from explicit full replay/mempool completion.

The narrow repair follows that pattern for this one reindex restart: stop the
owned validator, start it with the same arguments, connect RPC with the generic
import wait disabled, then explicitly require the saved chain height and loaded
mempool within 240 seconds (scaled only by the existing test timeout factor).
The existing common-tip equality, verifychain(4,0), pending-transaction persistence,
subsequent competing-chain reorganization and final validation assertions remain.
The test still fails if import stalls or state differs; no arbitrary sleep,
ignored error, new skip, shared framework change or production change is used.
The existing reindex checkpoint now records its elapsed reindex/replay time.


All reported checkout identities must match the listed source. Required
platforms may not be skipped. The general cross-packaging matrix intentionally
skips this integration branch to avoid installer creation; it is not counted as
a build pass. Existing script-assets and macOS Qt lifecycle skips must remain
named explicitly in the table/evidence. The three earlier internal mining skips
due to local host resource constraints remain historical limitations, not new
qualification passes. The prior clean 59-case native run and 30-checkpoint
Qt-backed run are preserved as historical evidence, separately from fresh CI.

## Final repair and documentation commit

A commit cannot contain its own SHA or the IDs of workflows triggered after it
is published. Identify this report's delivery commit without relying on a moving
branch name:

~~~bash
git log -1 --format='%H' -- doc/design/mercaweave-final-ci-qualification.md
~~~

The final publication contains the test-only reindex completion-wait repair,
this CI/handoff report and its public evidence. The document-bearing commit must receive the
same complete standard and dedicated CI checks before final delivery. Earlier
rows above remain evidence for their named commit and do not substitute for
those final checks. After that qualification completes, the final delivery response and retained receipt identify
the exact final SHA, workflow IDs, job IDs, counts, runtimes and conclusions.
The [branch Actions index](https://github.com/Mercatura-Core/Mercatura-Core/actions?query=branch%3Aintegration%2Fmercaweave-v0.1-upgrade)
links the immutable source-specific runs; open the runs whose source SHA equals
the command's output. No further change is published after declaring that
commit qualified.

## Scope, local validation and remaining boundaries

The affected complete three-node case passed **all 26 checkpoints** in
**336.367 seconds**, reusing the verified production binaries. It ran with
--jobs=4, the failing macOS PRNG seed 4475444380715087524 and the default
--timeout-factor=1; no broad suite timeout increase was applied. The recorded
reindex plus level-4 replay phase took 63.684 seconds locally. That phase includes
post-import verification, so it is not a measurement of import readiness alone.
The original macOS runner's exact import duration remains unavailable.
[Execution receipt](mercaweave-final-ci-qualification-artifacts/reindex-repaired-native-receipt.json),
[26 named results](mercaweave-final-ci-qualification-artifacts/reindex-repaired-native-results.json)
and [lossless runner log](mercaweave-final-ci-qualification-artifacts/reindex-repaired-native.log.gz)
retain the actual invocation and result. All owned test nodes stopped normally.

Final repository lint **PASS**, 16.482 seconds, covering 384 Python source
files plus repository checks. [Lint receipt](mercaweave-final-ci-qualification-artifacts/final-lint-receipt.json)
identifies the exact staged test blob. Git diff whitespace checks passed.
The earlier published handoff lint passed in 15.201 seconds; it remains separate
evidence for the earlier source.

[Scope and integrity](mercaweave-final-ci-qualification-artifacts/scope-and-integrity.json)
records the exact test SHA-256/git blob, unchanged production/workflows, preserved
previous handoff evidence and original production/research worktree heads.

Production consensus, grouped signature generation/verification, v2 signing,
wallet coin selection, fee estimation, Send/Receive, MercaHash, PQ networking,
genesis, emissions, capacity and monetary rules are unchanged. Public
nMercaWeaveHeight remains unset. Regtest height 110 remains an isolated fixture,
not a public activation proposal. No production research bypass is enabled.
No new three-node semantics or expected rejection is weakened.

The previous targeted local corrected case passed all 26 checkpoints in
382.971 seconds. The working owner helper was exercised against the unmodified
published fixture, actual native Qt v2/v3/mixed sends, independent three-node
relay/confirmation, restart, refusal to overwrite an existing directory and
normal owned-node stop. Those reports preserve interrupted driver attempts;
they are not presented as a fresh uninterrupted GUI suite here.

Interactive Windows/WSLg desktop review remains the owner's manual activity.
This CI task does not claim it, external security approval or public deployment
readiness. No optimized mining implementation was started.

## Synchronize safely on Windows 11 / Ubuntu 22.04 under WSL2

Run these commands inside your existing development checkout, not a deployed
node datadir. First inspect local work; if the output is nonempty, commit it to
your own branch or explicitly stash it before proceeding. Do not reset it away.

~~~bash
git status --short
git branch --show-current
git remote -v
~~~

This block stops on uncommitted work, a different origin or divergent history:

~~~bash
set -e
test -z "$(git status --porcelain)" || { echo 'Save your local changes first.'; exit 1; }
case "$(git remote get-url origin)" in
  https://github.com/Mercatura-Core/Mercatura-Core.git|git@github.com:Mercatura-Core/Mercatura-Core.git) ;;
  *) echo 'Unexpected origin; inspect before updating.'; exit 1 ;;
esac
git fetch origin
MCA_INTEGRATION_BRANCH='integration/mercaweave-v0.1-upgrade'
if git show-ref --verify --quiet "refs/heads/$MCA_INTEGRATION_BRANCH"; then
  git switch "$MCA_INTEGRATION_BRANCH"
else
  git switch --track -c "$MCA_INTEGRATION_BRANCH" "origin/$MCA_INTEGRATION_BRANCH"
fi
git merge-base --is-ancestor HEAD "origin/$MCA_INTEGRATION_BRANCH" || {
  echo 'Local branch contains additional or divergent commits; stop and inspect.'; exit 1;
}
git merge --ff-only "origin/$MCA_INTEGRATION_BRANCH"
git rev-parse HEAD
git status --short
~~~

Compare the printed SHA with the final delivery SHA and its successful Actions
checks. These commands never force-reset, delete directories or stash silently.
For a fresh clone, use a new directory and verify the same final SHA.

The helper and owner guide are now part of the branch:

- [Regtest helper](../../contrib/devtools/mercaweave-owner-regtest.py)
- [Actual Qt walkthrough, screenshots and build recipe](mercaweave-engineering-completion.md#manual-mercaweave-testing--project-owner)

That historical guide pins its earlier source and asks for a single-file helper
copy. After synchronizing here, keep this updated checkout: omit its old clone,
detached-checkout and helper-copy steps. Use its dependency/configure/build steps
with build -j4; this worker's binaries and /workspace paths are not assumed
to exist on your workstation. Ubuntu 22.04 needs GCC >=12.1 and Qt >=6.2.
Allow roughly 30–60 minutes for an uncached local development build.

Check that WSLg supplies a display, then start from your checkout using a **new**
private disposable directory. Initial real-PoW funding normally takes 2–4 minutes.

~~~bash
printf 'DISPLAY=%s\nWAYLAND_DISPLAY=%s\n' "$DISPLAY" "$WAYLAND_DISPLAY"
MCA_OWNER_ROOT="$HOME/mca-owner-$(date +%Y%m%d-%H%M%S)"
printf 'Fixture path: %s\n' "$MCA_OWNER_ROOT"
python3 contrib/devtools/mercaweave-owner-regtest.py start \
  --build-dir build/mercaweave-owner --state-dir "$MCA_OWNER_ROOT"
~~~

Leave the start terminal open. In a second terminal, use its exact printed
fixture path for status, restart, fund, mine, verify and stop as shown
in the walkthrough. Perform Receive/Send, Coin Control and wallet safety through
the real Qt interface. Only fixture setup/mining/verification uses RPC. The
helper refuses an existing start directory, binds loopback peers, and preserves
wallets on normal stop. Do not use any mainnet/public-testnet wallet or chain.

## Recommendation

**B — PARTIALLY QUALIFIED** at report publication. Standard CI and dedicated
Linux/Windows passed on the handoff source. Dedicated macOS had the documented
reindex-readiness timeout. The local repair and its focused results are recorded
above; the repair-bearing commit must pass fresh Linux, Windows MSVC, macOS ARM64
and general CI before classification A can be declared. Older successes do not
qualify that new commit.

The owner requested no polling during CI. After publication, collect completed
results when the session resumes; investigate any failure without weakening
validation. Once the exact final source is fully green and synchronized, it can
be frozen for the owner's local functional review. Native CPU/GPU wallet-miner
integration remains a separate future task. Do not begin it here.
