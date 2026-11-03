# MercaWeave v0.1 — WO07B automated simulation

Started 2026-11-03, America/Los_Angeles. Starting research HEAD
`e014a10efa17fa0a8ac053650cbede9960da055f`. Both worktrees verified clean;
original worktree remains on work at `4b88b619fb1b08c7f30112a38eb471880b2a6af4`.
No production source, activation, chainparams, existing datadir or network is changed.

## Production integration gap register (recorded before implementation)

| Gap | Missing call path | Current substitute | Covered / unavailable tests | Security implication and next work |
|---|---|---|---|---|
| 1 — grouped-v3 chainstate | Chainstate::ConnectBlock/DisconnectBlock -> actual complete group context -> CScriptCheck -> persistent undo/notifications/mempool reconciliation | WO04 owned synthetic View/Engine and WO07 guarded actual CheckInputScripts/CScriptCheck/UpdateCoins/ApplyTxInUndo components | Complete immutable proofs, real Script dispatch, cold/warm cache bypass, synthetic branches; full grouped P2P, block acceptance, persistent undo and wallet reconciliation unavailable | Unknown witness permissiveness is not ownership. Require a separate consistently compiled isolated full-node integration, caller/context plumbing, cache audit, invalid-block rollback, persistent undo/restart and differential multi-node tests before WO08 release claims. Affected sources: validation.cpp/.h, blockstorage, txmempool, validationinterface, script/interpreter. |
| 2 — grouped-v3 CWallet | CWallet protected ownership seed -> genuine WalletBatch records -> group derivation/issuance -> native signing/finalizer -> commit/notifications/rescan | WO05 research Wallet/Funding/GroupPSBT, WO06 Qt bridge/planner, WO07 genuine SQLite backend research records | Native encryption helpers, grouped signing/PSBT, atomic SQLite records/crashes/watch metadata; ordinary grouped wallet migration, native coin locks, rescan, balances and grouped Qt process lifecycle unavailable | No normal v3 custody or automatic broadcast is authorized. Separate integration must preserve one protected seed, authenticated schema/migration, locked lifecycle, rescan/issued ranges, actual CWallet PSBT and transactional coin reservations, then qualify reorg/restart/corruption and async Qt ownership. Affected sources: wallet/wallet.cpp, walletdb, spend, scriptpubkeyman, psbt and Qt WalletModel. |

These gaps remain open throughout WO07B. Research v3 validity is never described
as real-node consensus or propagation. No WO08 work is initiated.

## Preflight and intended bounded stages

Read the WO01–WO07 design reports and existing evidence. Reproduce the current
32-test standalone suites in Release, checked-STL Debug and ASan/UBSan.
Actual host: Debian/GCC x86_64, 3 CPUs, 10,207,480 KiB RAM, approximately
3.6 GiB free disk. Reuse existing ordinary node binaries and extracted SDK;
no rebuild/toolchain installation is planned initially. Limit build jobs to four
and native campaign concurrency to the host's memory budget.

Implement a Python package using BitcoinTestFramework/TestNode and actual wallet
RPC signing. Funding will create confirmed fanout inventories, then independent
transaction epochs. Keep off-chain exchange accounting distinct from deposit,
sweep, withdrawal and consolidation transactions. Persist seeded manifests,
per-transaction measurements, independent expected/observed accounting and
bounded checkpoints outside disposable node datadirs. Start with a small smoke
campaign, then 100/500/1000 stages; support larger requested counts and optional
extended modes without claiming unexecuted workloads. Exercise network outages,
partitions, conflicting branches, restart, native wallet/PSBT and bounded soak.
Use existing isolated v3 executables plus a genuine signed research workload
adapter for comparison; never put v3 output scripts into a node-process campaign.

## A. Repository state and local checkpoints

The existing research branch is retained. Local checkpoints are:

- `0b76fe5`: bounded Python framework, confirmed funding and accounting.
- `cf3063c`: signed research fixtures and cross-build replay.
- `0e43b0f`: native lifecycle, credited reorg and confirmed-inventory fixes.
- `bea812b`: selected campaign metrics, failures, replay and preservation evidence.
- `0c3a9f2`: LF summary CSV output and byte-for-byte summary replay verification.

The final qualification-report commit is identified by
`git log -1 -- doc/design/mercaweave-v0.1-automated-simulation.md`. Original worktree,
project/v0.1 and all prior WO01–WO07 design/evidence files remain unchanged.
No remote push, merge, reset, rebase or branch recreation occurs.

## B. Infrastructure and execution

`simulation/core.py` implements validated manifests, exact two-decimal integer
amounts, tracked UTXOs/reservations, an independent customer ledger, atomic metric
checkpoints and process/disk sampling. `native.py` derives from the actual
BitcoinTestFramework and uses TestNode, wallet RPC, authentic node UTXOs, native
serialization/signing, real RPC admission, P2P relay and normal mining.
`lifecycle.py` adds hostile wire, independent-wallet PSBT, credited reorg,
encryption/backup and native-reward operations. `run.py` executes bounded stages,
preserves failures, skips only identical successful manifests on explicit resume,
and forwards interruption to its own child process group. It starts new node
datadirs for retries rather than resuming an in-flight wallet/ledger blindly.

Manifests configure transaction/recipient counts, 2–4 nodes, seeds, custody,
confirmed funding epochs (maximum 1,500 outputs), standard-size batches (maximum
64 recipients), mining intervals/capacity steps, bootstrap maturity, network
operations, lifecycle probes, soak duration/cycle caps and explicit extended mode.
100/500/1000/5000/10000 and optional 50000/100000 are supported sizes; execution
claims refer only to completed stages. Framework configuration disables discovery,
DNS/fixed seeds and auto-connect and binds loopback before starting daemons.
Native scenarios enforce ordinary standard policy and synchronous SQLite.

The gap register above is authoritative: genuine native node components and
research adapters are never represented as the same qualification path.

## C. Funding preparation

A native-v2 mining address receives real coinbases. The independent ledger reads
block contents, checks native subsidy plus independently summed transaction fees,
and tracks maturity separately. Confirmed native fanout transactions create an
explicit inventory of output-index-distinct funding UTXOs before load generation.
Repeated funding scripts still produce independent confirmed UTXOs. Change is not
used in an unconfirmed chain. Miner, customer and exchange wallets are separate.

MCA amounts use integer base units (100 per MCA), rejecting fractional base units.
The regular transaction generator checks every input for the real 3,309-byte native signature and
1,952-byte public key. Every generated transaction remains below 400,000 standard
WU and pays a conservative explicit user fee above the unchanged full-byte floor.
No fee/dust default, consensus bound or native authorization is modified.

Treasury hot-wallet transfers provide operating capital independently of deposit
liabilities. If concentrated custody leaves too few independent confirmed inputs,
the scheduler confirms its pending withdrawals before refreshing change inventory.
It never creates a long unconfirmed change chain or reserves an input twice.

## D. Measured transaction coverage

| Campaign / requested deposits | Model / nodes | Confirmed transactions | Native signatures | Wire bytes | Fees (MCA) | Peak node RSS (MiB) | Wall seconds |
|---|---|---:|---:|---:|---:|---:|---:|
| native-independent / 100 | A / 3 | 105 | 130 | 706,738 | 14.09 | 312.09 | 242.64 |
| native-independent / 500 | A / 3 | 515 | 640 | 3,479,880 | 69.36 | 393.00 | 290.26 |
| native-independent / 1,000 | A / 3 | 1,027 | 1,277 | 6,943,623 | 138.39 | 400.97 | 339.30 |
| native-capacity / 3,000 | B / 2 | 3,080 | 3,830 | 20,825,460 | 415.08 | 492.13 | 494.14 |
| native-lifecycle-fixed / 100 | B / 2 | 188 | 189 | 1,026,856 | 20.82 | 325.34 | 319.84 |
| native-final-lifecycle / 20 | C / 2 | 27 | 97 | 518,737 | 9.97 | 319.50 | 357.88 |
| native-centralized / 500 | C / 4 | 525 | 1,018 | 5,487,318 | 107.26 | 389.82 | 396.86 |

**Total: 5,467 confirmed transactions; 7,181 native signatures; 38,988,612 wire bytes; 42,433,122 WU; 774.97 MCA in explicit test fees.**

Requested deposit counts exclude extra soak deposits and the credited reorg probe. Actual phase counts are retained in `native-campaigns.json`: the reused 100-deposit lifecycle stage has 121 deposits and 60 separate withdrawals after soak/reorg; the centralized 20/500 stages have 21/501 deposits including their reorg probes. Other stages execute the requested half-count withdrawal recipients. Reconsidering a transaction does not count it twice.

The actual completed stage sizes are 20/100/500/1000/3000 requested deposits. 5000/10000 and optional 50000/100000 are **NOT TESTED**; accepting those manifest sizes is not execution evidence.

Counts denote actual generated, wallet-signed, admitted and confirmed transactions;
withdrawal recipient counts are separate. Funding, treasury transfers, custody
sweeps, withdrawals, PSBT payments and reward consolidation all contribute to the
lifecycle total. A transaction is never successful merely because RPC accepted it.
The failed run/attempt register is separate; checkpointed work from failed stages
is retained for diagnosis and excluded from completed-stage totals.

## E. Exchange models and lifecycle costs

Model A issues independent native-v2 child keys for deposit addresses. These are
different private keys under one wallet's master seed, not independent organizations
or independent backups. Model B reuses a deposit address/key. Model C uses distinct
deposit keys and genuine sweeps into an operational hot-wallet inventory. Customer
account IDs are off-chain accounting identifiers and never treated as keys.

Only confirmed deposits create account credits. Withdrawal reservations/debits
are accounted separately from blockchain transactions. Wallet assets reconcile
against the independently tracked UTXO ledger and are checked against outstanding
customer liabilities, with treasury capital covering costs. Repeated and fresh
addresses, change, consolidation/sweeps, deferred custody, hot funding and separate
versus multi-output withdrawal batches are exercised. Operational key concentration
and input inventories differ across models; batching savings are ordinary batching
savings and are not attributed to MercaWeave.

Models D/E are separate isolated signed research scenarios with one or multiple
exact signing-key groups. They never force independent native deposit keys into
one signing group. No real exchange service or customer money is involved.

For the 500-deposit independent-key stage, 250 withdrawal recipients occupy 13
transactions, 138 input signatures, 744,107 bytes and 13.93 MCA of explicit fees.
The centralized 500-deposit stage uses 8 sweeps (500 native signatures,
2,654,940 bytes, 50.08 MCA), then 13 withdrawals (13 signatures, 80,482 bytes,
1.43 MCA) for the same 250 recipients. The entire respective measured stages cost
69.36 / 107.26 MCA and 3,479,880 / 5,487,318 bytes; the latter also includes a
PSBT and reorg probe. Sweeping reduces later withdrawal inputs while adding
its own lifecycle cost and concentrating operational keys. These different
configurations and probe sets are not a controlled fee-optimization experiment.

The implemented strategies cover separate/batched withdrawals, immediate sweeps
or retaining deposits, explicit fee-aware input sufficiency and hot inventory
refresh. A parameter sweep of consolidation thresholds, cold-storage custody
with separate protection, independent hot replenishment strategies, and an
exchange-level failed-withdrawal/retry state machine are **NOT TESTED**.
Native rejection, missing metadata and competing-spend regressions cover related
failure behavior, not all those exchange business operations.

## F. Native mining

All node campaigns use ordinary native mining interfaces and supported v2 reward
scripts, never standalone MercaMiner. Native MercaHash and full reward accounting
are unchanged. Coinbases are tracked through maturity, fees reconcile to block
rewards and mining runs with loaded mempools, after relay restart and after reorg.
The lifecycle test explicitly consolidates 50 mature coinbases with 50 native
signatures; this is a caller-requested test operation, not automatic consolidation.
The research scheduler's 50-reward/locked/duplicate/fee/reorg tests remain in the
regression suite. Automatic consolidation remains disabled and mining fees remain
0%. Physical GPU performance is not tested.

## G. Block capacity

The measured ordinary-PQ loaded blocks include 190/385/580/700/775 transactions
at 1,027,591 / 2,081,958 / 3,136,323 / 3,785,163 / 4,190,688 bytes, respectively:
24.50%, 49.64%, 74.78%, 90.25% and 99.91% of the actual 4,194,304-byte regtest
ceiling. Remaining transactions are confirmed in subsequent blocks. Output-heavy
funding and multi-input sweep/withdrawal compositions are measured separately.

Active template rules are read from native getblocktemplate: at height 161 the
ordinary regtest template reports sizelimit=4,194,304, compatibility
weightlimit=16,777,216 and sigoplimit=80,000. Weight is retained independently;
there is no witness discount in serialized-byte capacity. Native-PQ load here
binds bytes before the sigop/witness-item budgets; transaction batching also
respects the 400,000-WU per-transaction policy. These observations do not generalize
to every network/height. Earlier immutable actual-parameter snapshots remain valid.

The targeted native block-capacity functional regression uses synthetic padding
and OP_TRUE spends, separately from the signed-PQ capacity campaign. It exercises
valid boundary blocks, invalid oversized blocks, large transaction counts, peer
transport fallback and reindex/recovery. No limit is raised. Older simulator block
metrics combine mining/local validation/propagation; later metrics separate the
mining/local-validation RPC interval from chain-tip convergence. Those are observed
end-to-end intervals, not a cold-cache standalone block-verifier benchmark.

## H. Network and mempool

Completed native campaigns use 2, 3 and 4 actual loopback node processes.
The miner/wallet node and relay peers synchronize real transactions and blocks.
Mempool convergence is checked across every node, with observed per-transaction
propagation upper bounds. Admission, signing, construction and confirmation timing
are separate. Partitioning the first edge lets the miner produce two missed blocks;
reconnection requires identical tips and relay restart requires catch-up.

The simulator uses ordinary witness-v2; the event's early "loopback-native-v2"
label refers to the witness scheme, not a claim of BIP324 transport coverage.
No synthetic v3 fixture is treated as P2P propagation or mempool admission.

## I. Reorganizations and recovery

A confirmed deposit is credited, a two-block branch is invalidated on all nodes,
the expected ledger removes the credit, and the real exchange wallet's trusted
balance is checked before reconsideration. Reconsideration restores the identical
UTXO-set hash, tip, confirmation depth and expected credit. Relay and wallet-node
restart paths are separate, using fresh test datadirs only.

Genuine single-conflict RBF replacement and confirmation are additionally
covered by the native RBF regression. Missing/stale input failures and root
ownership/context/reorg mutations remain in the preserved regression suite.
No unexplained negative balance, phantom coin, duplicate spend or consensus-result
disagreement was observed in completed stages.

The credited native scenario disconnects and reconsiders the same two-block
branch. Independently mined competing forks, a separate one-block campaign,
ancestor/descendant package eviction, maturity changes across a competing fork
and restart during an incomplete reorg are **NOT TESTED** by that scenario.
Native block-capacity restart/reindex is a separate recovery regression.

## J. Wallet and PSBT

Genuine native wallets create fresh/reused addresses and sign real payments.
Two independent wallets partially sign one PSBT; verified transaction-wide
witness_utxo metadata is supplied by the coordinator, combined, finalized,
extracted, admitted and confirmed. Without foreign prevout data, native signing
correctly fails if the wallet lacks that transaction history; it can already
know the data in other scenarios. The harness does not weaken that requirement.

Encryption rejects locked signing with RPC -13. Explicit unlock allows signing.
An encrypted native backup is restored to a separate wallet and reconciled to
expected funds. A full wallet-node restart followed by explicit wallet reload
retains balances, lock state and signing ability after explicit authorization.
Only public fixture passphrases are in code; encrypted backups are expendable
work data and no plaintext ownership seed is exported or committed.

Grouped PSBT/native SQLite/watch/corruption/entropy/partial signer regressions
continue in the isolated suite. Actual grouped-v3 CWallet remains unqualified.

## K. Research-only v3 comparisons

`mercaweave_simulation_research` constructs real ML-DSA-65 signatures for
1/10/53/100/1000/1024 inputs in one group, 100 inputs in 10 and 100 groups,
a 53-v3-plus-2-native mixed transaction, 256 groups and 4096 inputs/four groups.
Every row passes the authoritative research Engine against owned synthetic coins,
fails unchanged normal relay policy and records real_node_v3_execution=false.
Every row also has a genuinely rejected late-invalid signature variant.

The 11 canonical wire hashes and all non-timing shapes agree across Release,
checked-STL Debug and ASan/UBSan; seed 7 is replayed and seed 11 produces different
correctly signed bodies. Construction, final signing and verification are timed
separately. Mixed-fixture construction includes preparatory signatures before
the timed final signing pass; pure rows do not. Actual verification calls are
G + 2V for this research pipeline, with duplicate native preverification/Script
passes disclosed. This is not optimized ordinary-node throughput.

The 24 preserved WO05 wallet workload rows additionally cover one/multiple group
keys, independent deposit keys, mixed funding, batched recipients, native savings
and grouped mining construction. Fees, bytes, signature counts and standard-size
splits remain genuine research wallet outputs, not a full native CWallet bridge.

Selected Release research shapes (actual signatures and wire serialization):

| V3 inputs / groups / native inputs | Signatures | Actual verifications | Wire bytes | WU |
|---|---:|---:|---:|---:|
| 1 / 1 / 0 | 1 | 1 | 5,397 | 5,679 |
| 10 / 1 / 0 | 1 | 1 | 6,117 | 7,506 |
| 53 / 1 / 0 | 1 | 1 | 9,557 | 16,235 |
| 100 / 1 / 0 | 1 | 1 | 13,317 | 25,776 |
| 1000 / 1 / 0 | 1 | 1 | 85,319 | 208,484 |
| 1024 / 1 / 0 | 1 | 1 | 87,239 | 213,356 |
| 100 / 10 / 0 | 10 | 10 | 60,675 | 73,134 |
| 100 / 100 / 0 | 100 | 100 | 534,255 | 546,714 |
| 53 / 1 / 2 | 3 | 5 | 20,175 | 27,099 |
| 256 / 256 / 0 | 256 | 256 | 1,367,609 | 1,399,262 |
| 4096 / 4 / 0 | 4 | 4 | 348,785 | 852,758 |

The 100-input/100-group, 256-group and 4096-input boundary fixtures exceed ordinary standard transaction size. Protocol validation, existing consensus components, relay policy and real-node execution remain separate outcomes. Research fixtures do not pay real network fees or create spendable v3 funds. Timings and all three builds are in `research-*.csv`; there is no claim that these different ownership structures provide equivalent custody.

## L. Adversarial and resource findings

Native mutation probes change signature, public key, output value and sequence,
append an oversized signature or duplicate an input. All actual nodes reject,
return repeatable identical reasons, and retain the original UTXO hash. The
JSON retains reasons and validation timing. Research regressions exercise foreign
followers, wrong/duplicate anchors, changed amounts/scripts, added/reordered inputs,
wrong genesis/flags, stale proofs/caches, count bounds, PSBT conflicts and concurrent
verification. The old 2,000 wire and 1,000 PSBT mutation campaigns remain active.

Four earlier campaign attempts exposed harness defects/assumptions: missing
transaction-wide foreign PSBT metadata; insufficient confirmed hot inventory;
and two probes incorrectly requiring missing-metadata failure even when wallet
history already supplied it. Failures and checkpoints are retained, never silently
counted as complete. The fixes supply authenticated metadata unconditionally and
confirm change before reuse. No production consensus or wallet protection changes.

## M. Bounded soak and interruption

The completed lifecycle soak requested 30 seconds and executed two bounded
transaction/mining/disconnect/restart/reconciliation cycles in 35.591 seconds.
Cycle caps and deadlines are explicit; long-running modes are configurable up to
one day per invocation. Checkpoints retain completed epochs and source manifests.
Explicit resume skips only identical successful stages; retries use new directories
and preserve earlier failures. Interruption is forwarded to the runner's own
process group, allowing framework cleanup; failed datadirs remain for diagnosis.
A separate runner-control probe verifies successful-stage resume without changing
the stored stage record and rejects a changed manifest. SIGTERM to the runner
while two own native daemons are live is forwarded, records returncode 124,
preserves its interrupted stage, and leaves neither observed daemon running
(0.820 seconds total probe wall time). An initial monitor used changing process
thread names and failed to detect daemons; its finally-block interruption still
completed and its record is retained. Both interrupted probes are excluded from
successful workload counts. `runner-controls.json` records these distinctions.
Soak loops run transaction/mining/disconnect/restart/reconciliation cycles; PSBT,
encryption and credited reorg execute once afterward when lifecycle is selected.
Repeated reorgs and encryption throughout a long soak are not qualified.
This short soak is not multi-day stability, leak freedom or exhaustive recovery.

## N. Performance and resources

Actual host is Debian/GCC 14.2 x86_64, 3 CPU quota, about 9.7 GiB RAM and initially
3.6 GiB free disk. At most three concurrent single-job research builds were used;
native functional runner concurrency was reduced to two while campaigns ran.
Node RSS/high-water, process CPU seconds, logical disk bytes/growth, transaction
latencies and loaded block sizes are in the machine-readable summaries. Earlier
stage versions had fewer resource sample points; measured values are labeled
observed rather than exhaustive. Process CPU totals do not include unobserved
cleanup time. The overlay write+fsync probe is application latency, not physical
SSD throughput. Contended timings must not be compared to another host's throughput.

Raw per-transaction tables are kept in ignored work storage with hashes in selected
evidence. Committed CSV/JSON summarizes actual confirmed counts, fees, byte/weight
cost, input/output distributions, variability, blocks, resource samples and scenario
manifests. No theoretical target count substitutes for a measured outcome.

Per-stage transaction median signing/admission/confirmation intervals (milliseconds), followed by measured admission min–max:

| Campaign / requested deposits | Signing median | Admission median | Confirmation median | Admission range |
|---|---:|---:|---:|---:|
| native-independent / 100 | 1.139 | 7.031 | 18699.829 | 5.526–18.175 |
| native-independent / 500 | 1.277 | 8.997 | 40754.524 | 5.369–36.573 |
| native-independent / 1000 | 1.229 | 8.228 | 97983.660 | 5.012–292.255 |
| native-capacity / 3000 | 1.776 | 12.317 | 36698.129 | 4.497–1348.067 |
| native-lifecycle-fixed / 100 | 1.392 | 7.203 | 11358.110 | 5.314–21.404 |
| native-final-lifecycle / 20 | 1.301 | 10.234 | 5388.198 | 6.448–40.848 |
| native-centralized / 500 | 1.277 | 8.111 | 60470.606 | 4.944–44.840 |

The propagation timer is an upper bound from submission until the batch is polled across all peers; sequential generation time is included. It is not an individual network-hop latency measurement. PSBT rows use zero construction/signing fields as unmeasured placeholders; those zeros must not be interpreted as zero-cost cryptography. Serialized transaction bytes exclude block headers/coinbases; reported loaded block sizes include them.

## O. Regression, sanitizer and determinism

The WO07 checkpoint was reproduced: 32/32 in each of Release, Debug and ASan/UBSan.
The expanded suite passes 34/34 in each configuration (102 results), including
accounting-oracle tests and signed research scenarios. Native PSBT, RBF and block
capacity functionals pass separately (3/3). No broad inherited Bitcoin functional
suite was run. No new sanitizer suppression is added; leak detection remains
unavailable in this ptrace environment and is explicitly disabled, not passed.
Clang and coverage-guided fuzzers are unavailable; TSan and alternate platforms,
physical GPU, low-end hardware and independent security audit remain unqualified.

Expanded CTest wall times were 35.67 seconds (Release), 194.97 (Debug) and
178.77 (ASan/UBSan). The 24 unchanged wallet workload shapes also match prior
immutable evidence in each build (72 comparisons). Latest manifest-bound checks
pass directly after the earlier full-suite runs. The eight regenerated summary
files reproduce byte-for-byte from retained campaign data; raw transaction-table
hashes and all 5,467 distinct accepted/confirmed native txids were checked.
Build options, target flags, binary hashes and retained test-log hashes are in
`build-provenance.json` and `regression.json`.

All 64 prior design reports, deterministic-vector/evidence files selected by the
immutable-evidence check are hashed against
e014a10 and must remain byte-for-byte unchanged. Native transaction keys/randomized
signatures are genuine fresh wallet material, so native txids are not deterministic
between fresh runs. Workload structure/accounting is seeded; public C++ research
fixtures have exact deterministic wire replay. Those are different guarantees.

## P. Outstanding production gaps

Both entries in the pre-implementation gap register remain OPEN. Neither full
grouped-v3 Chainstate/ConnectBlock nor actual grouped-v3 CWallet is qualified by
WO07B. Genuine supported-v2 node results cannot close either gap. A separate
integration work order must complete ownership, persistent undo, native wallet
records/rescan/signing/finalization, queues, caches and notifications, followed by
full-path reorg/restart/differential tests before a release decision.

## Q. Reproduction

From the research root, after the existing native Release and three standalone
builds are available:

```bash
PYTHONPATH=src/test/mercaweave python3 -m unittest simulation.test_core -v
python3 src/test/mercaweave/simulation/run.py --counts 100 500 1000 --nodes 3 \
  --custody independent --batch-size 20 --work "$PWD/work/wo07b/native-independent"
python3 src/test/mercaweave/simulation/run.py --counts 3000 --nodes 2 \
  --custody reused --batch-size 20 --epoch-size 1500 --capacity-steps \
  --work "$PWD/work/wo07b/native-capacity"
python3 src/test/mercaweave/simulation/run.py --counts 100 --nodes 2 \
  --custody reused --batch-size 1 --lifecycle --soak-seconds 30 --max-soak-cycles 2 \
  --work "$PWD/work/wo07b/native-lifecycle-fixed"
python3 src/test/mercaweave/simulation/run.py --counts 20 --seed 11 --nodes 2 \
  --custody centralized --batch-size 10 --bootstrap-blocks 160 --lifecycle \
  --work "$PWD/work/wo07b/native-final-lifecycle"
python3 src/test/mercaweave/simulation/run.py --counts 500 --nodes 4 \
  --custody centralized --batch-size 20 --max-soak-cycles 2 --lifecycle \
  --work "$PWD/work/wo07b/native-centralized"
```

Use fresh campaign roots; `--resume` explicitly preserves/skips completed matching
stages and creates unique retry directories. Original invocation commands,
manifest/source hashes and actual version differences are retained in evidence.
Each simulation uses --configfile pointing to the ordinary root test/config.ini,
loopback permission and dedicated work/ datadirs. Public network access is unused.

```bash
for mode in release debug sanitize; do
  work/tools/cmake/data/bin/cmake -S src/test/mercaweave -B "work/build-$mode"
  work/tools/cmake/data/bin/cmake --build "work/build-$mode" \
    --target mercaweave_simulation_research -j1
  ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
    work/tools/cmake/data/bin/ctest --test-dir "work/build-$mode" --output-on-failure
  ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
    "work/build-$mode/mercaweave_simulation_research" 7 > "work/wo07b/research-$mode.csv"
done
python3 work/wo07-native-release/test/functional/test_runner.py --jobs=2 \
  --tmpdirprefix="$PWD/work/wo07b" --resultsfile="$PWD/work/wo07b/native-regressions.csv" \
  mercatura_pq_psbt.py mercatura_pq_rbf.py mercatura_block_capacity.py
```

Summarize retained real run data (including failed attempts) with:

```bash
python3 src/test/mercaweave/simulation/summarize.py \
  --campaign work/wo07b/native-independent --campaign work/wo07b/native-capacity \
  --campaign work/wo07b/native-lifecycle-fixed --campaign work/wo07b/native-final-lifecycle \
  --campaign work/wo07b/native-centralized --campaign work/wo07b/native-reused \
  --research work/wo07b
```

These commands specify final validated behavior. Earlier rows were executed
before later optional lifecycle/resource refinements; their retained manifests
and source hashes expose those version differences (the earliest 100-deposit
stage lacks source-hash capture). Fresh commands do not promise identical timing,
transaction IDs or randomized native wallet signatures. Public research seed 7
wire replay is exact. The failure register keeps concise exception excerpts and
hashes of full retained logs.

The earlier WO07 report documents fresh build/SDK options. Optional counts 50000
and 100000 require --extended. They were not run. 5000/10000 modes are implemented
but not claimed as executed in this host's selected campaign set.

## R. Remaining requirements and separate verdicts

| Qualification area | Verdict | Exact scope |
|---|---|---|
| Native v2 real-node stress infrastructure | **PASS WITH RESTRICTIONS** | Real Release nodes, signed independent funding, 2–4 processes, accounting and controlled interruption; selected Linux configurations only. |
| Exchange and mining simulation coverage | **PASS WITH RESTRICTIONS** | Models A/B/C, signed withdrawals/sweeps, native rewards, 99.91% block, reorg/restart/PSBT/backup and short soak; larger sizes and exhaustive strategy/fault matrix unrun. |
| Research-only MercaWeave v3 simulation coverage | **PASS WITH RESTRICTIONS** | 11 real signed research shapes across three builds, seed replay, adversarial regressions; isolated owned synthetic coins only. |
| Real-node grouped-v3 readiness | **NOT TESTED** | Complete Chainstate/ConnectBlock and actual CWallet grouped call paths remain OPEN. |
| Overall WO07B completion | **PASS WITH RESTRICTIONS** | Reusable implemented framework, feasible executed campaigns and preserved evidence delivered within the bounded research scope; no production readiness claim. |

Restrictions include all open production gaps, limited native configuration/build
coverage, short soak, no physical GPU or alternate platform, incomplete exhaustive
package/eviction/network-fault exploration, no hardware power-loss recovery and no
independent audit. No activation height/schedule, runtime enablement, consensus
limit, fee/dust default, native-v2 authorization or deployed datadir is changed.
Both worktrees must finish clean after the final evidence commit; no push occurs.
WO08 has not begun and cannot declare release readiness while either critical
integration gap remains unqualified.
