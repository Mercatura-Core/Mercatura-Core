# MCA-A-004: approved undo-data budget

This implementation follows the project's approval after the investigation in
`README.md`. The original report and standalone fixture are preserved as the
record of the pre-fix investigation; their "pending approval" status describes
that earlier stage.

## Consensus rule and activation

An active block must satisfy:

```
GetSerializeSize(CBlockUndo) <= 2 * GetMaxBlockCapacityBytes(params, block_height)
```

The measurement includes the outer transaction-undo count, every input-undo
count, and every byte serialized by `TxInUndoFormatter`. The disk header and
checksum are not part of the payload. Equality is allowed. All arithmetic uses
fixed-width uint64 integers and checked subtraction before addition. The existing
capacity calculation clamps to 1 GiB, so multiplying its result by two cannot
overflow and the maximum payload is 2 GiB, below UINT32_MAX. The capacity schedule
and block-capacity ceiling are unchanged.

| Network | Activation |
| --- | --- |
| Mainnet | Height 0, fixed in chain parameters |
| Regtest | Height 0 by default; `-testactivationheight=undo@HEIGHT` selects a nonnegative height below INT_MAX in isolated tests |
| Existing public testnet | Disabled, pending a separate coordinated activation |
| Testnet4 | Disabled |
| Signet, including custom challenges | Disabled |

The undo override is explicitly rejected on every public network; there is no
mainnet runtime override. The rule is a consensus tightening approved for
pre-mainnet launch, active from genesis. It changes no header serialization or
block hash. An older binary can still accept blocks that violate the new rule;
all mainnet validators must use the approved consensus rules. Existing public
testnet keeps its previous behavior, including the unresolved future undo limits
until a separate activation is approved. No public-chain history was rewritten.

## Incremental enforcement

`ConnectBlock` initializes the count from
`GetSizeOfCompactSize(block.vtx.size() - 1)`, the outer undo vector prefix.
Immediately before each non-coinbase transaction's `CheckTxInputs`, it calls the
small `CheckTxUndoSize` helper when the rule is active.

The helper first charges `GetSizeOfCompactSize(tx.vin.size())`. It then fetches
each coin individually and charges
`GetSerializeSize(Using<TxInUndoFormatter>(coin))`. This reuses the actual writer's
height/coinbase VARINT, historical version dummy, amount compression and script
compression. There is no second implementation of those encodings, host object
size estimate, repeated whole-object serialization, or full undo reconstruction
to enforce the rule. Serialization sizing uses `SizeComputer`; raw script spans
are counted without copying their contents.

For each charge, `undo_size > limit || bytes > limit - undo_size` fails before
the addition; therefore the counter cannot wrap or exceed the limit. Transaction
boundaries are counted once, not once per input. Successful processing sums
exactly the fields serialized by `CBlockUndo`, with linear work in the number
of inputs. A same-block spend sees the output already added by the previous
transaction, including its actual creation height and non-coinbase flag.

This check precedes `CheckTxInputs`'s eager `HaveInputs` walk, signature/precomputed
spent-output copies, and that transaction's `UpdateCoins` / undo construction.
It stops as soon as the next coin would exceed the budget. The coin being
measured can already have been loaded, so at most one additional spent script
is fetched at that boundary. The helper leaves missing inputs to the existing
input-validation failure path; an incomplete count can never permit a valid
connection with missing inputs.

The check also runs during just-check/proposal validation, reindex connection,
assumevalid script skipping, and script-cache hits. Rejection is a normal
`BLOCK_CONSENSUS` failure with reason `bad-blk-undo-size`, not a fatal storage
error. Earlier transactions can have changed the caller's temporary view, as for
other input/script failures, but `ConnectTip` flushes that view only after a
successful connection. No rejected block's partial state is committed.

## Storage and compatibility

There is no storage-format change. `CBlockUndo`, `CTxUndo`, and
`TxInUndoFormatter` serialization are unchanged; the four-byte length, message
magic, payload offsets and checksum stay identical. `WriteBlockUndo` retains
its existing uint32 defensive check. Legacy reading is unchanged, including
historical metadata and checksum verification. Undo budgets are contextual
connection rules, not retroactive restrictions on disk decoding.

MercaHash versions, DGWv3, adaptive emission, fees, PQ v2 and MercaWeave v3
authorization, and normal chain selection are unchanged. Both authorization
paths encounter the same budget check before their existing script checks.
There is no new per-script, per-transaction, mempool or monetary restriction.

## Resource limits that remain

At the final capacity, 2 GiB of serialized undo is still permissible. Scripts
may coexist in undo coins, UTXO caches, transaction precomputation and queued
script checks. Peak RAM can substantially exceed the serialized budget; it is
not a low-RAM guarantee. Existing block/input/vector allocations also remain.
The new check bounds the demonstrated block-wide spent-output amplification,
not every node allocation or the cost of all retained chainstate.

Disk must accommodate the accepted payload plus its existing 40-byte record
overhead and allocation rounding. Recent undo cannot be pruned merely to honor
a small pruning target. Keeping many maximum-budget recent blocks or competing
branches can still require substantial disk, and disconnect/restart verification
still deserialize one complete allowed undo object. Ordinary disk exhaustion
remains an operational error. No additional consensus restriction was added to
hide these costs.

## Permanent coverage and execution

`src/test/undo_limits_tests.cpp` covers exact serialization accounting, compressed
and raw scripts, amount and height VARINT boundaries, inner and outer CompactSize
boundaries, all scheduled capacities, saturation, activation configuration,
integer limits, actual connection boundaries and state isolation. A generated
coin source proves direct connection stops loading an oversized transaction at
the budget rather than loading all 24,000 inputs. The original 4,782,720,061-byte
construction is counted without materialization and stopped within its first
transaction using the actual 64 MiB future-height limit. Disk fixtures verify
legacy record length/offset accounting, payload round trips, checksums and
malformed/truncated reads.

`test/functional/mercatura_undo_limits.py` funds less than 9 MiB of actual outputs
and checks payloads exactly 8 MiB - 1, 8 MiB, and 8 MiB + 1. Two real nodes show
preactivation acceptance versus genesis enforcement, boundary activation,
atomic rejection, connection, verification/disconnection, work-based reorg,
restart, chainstate reindex and full reindex. Existing durability/pruning tests,
PQ v2/MercaWeave v3 tests and B1–B4 regressions provide related coverage.

Final execution results (all listed final commands exited zero):

| Check | Result |
| --- | --- |
| Full build and final incremental build: `test_bitcoin`, `bitcoind`, `bitcoin-cli` | Passed |
| New `undo_limits_tests` suite | 6 cases; 132,358 assertions passed |
| Targeted storage, pruning, capacity, accounting, authorization and B1–B4 unit suites, including the new suite | 108 cases; 2,037,030 assertions passed |
| Additional header-admission, PQ signing and PSBT tests | 5 cases; 318 assertions passed |
| `mercatura_undo_limits.py` | Passed: below/exact/above budget, activation, atomic rejection, disconnect/reorg, restart, both reindex modes |
| `mercatura_pq_capacity.py` | Passed: real PQ v2 consolidation, full-capacity blocks, propagation and reindex |
| `mercatura_mercaweave.py` | Passed: real v3 activation, mixed v2/v3 authorization, wallets, PSBT, mining and recovery |
| `mercatura_headers_lowwork.py --mercahash-v2-height=0 --pq` | Passed (B1) |
| `mercatura_classical_payment_safety.py` | Passed (B2) |
| `mercatura_recipient_totals.py` | Passed (B3) |
| `wallet_mercatura_pq_backup.py` | Passed (B4 consumer regression; HKDF lifetime tests also ran in `crypto_tests`) |

Thus **113 unit cases / 2,037,348 assertions** and **seven functional scripts**
passed. The 108-case run took 409.46 seconds and peaked at 609,336 KiB RSS.
The separate focused six-case run peaked at 347,128 KiB RSS. Large-position
coverage in the existing flat-file/durability suites uses sparse files with
bounded physical allocation. No multi-gigabyte undo payload was materialized.

Exact build/unit commands:

```sh
CCACHE_DISABLE=1 cmake --build /tmp/mercatura-b1-build \
  --target test_bitcoin bitcoind bitcoin-cli -j2
/tmp/mercatura-b1-build/bin/test_bitcoin \
  --run_test=undo_limits_tests,blockstorage_durability_tests,flatfile_tests,blockmanager_tests,block_capacity_tests,testnet_capacity_tests,compress_tests,coins_tests,headers_replay_tests,spend_tests,crypto_tests,mercaweave_activation_tests \
  --report_level=detailed -- \
  -testdatadir=/tmp/mercatura-b5-budget-sevSUV/unit-data
/tmp/mercatura-b1-build/bin/test_bitcoin \
  --run_test='header_admission_*:script_tests/mercatura_pq_witness_v2_end_to_end:psbt_wallet_tests/mercatura_pq_psbt_wallet_signing' \
  --report_level=detailed -- \
  -testdatadir=/tmp/mercatura-b5-budget-sevSUV/extra-unit-data
python3 /tmp/mercatura-b5-budget-sevSUV/run-functional.py
cmake -S /home/gwestcot/projects/mercatura -B /tmp/mercatura-b1-build
CCACHE_DISABLE=1 cmake --build /tmp/mercatura-b1-build \
  --target test_bitcoin bitcoind bitcoin-cli -j2
```

The functional driver invokes the source tests using the existing
`pq_lab_proxy.run_owned_test` wrapper, fresh datadirs, available loopback ports,
and `/tmp/mercatura-b1-build/test/config.ini`. It ran tests sequentially against
a node linked from the rebuilt current production libraries while the full
unit binary continued building. The eventual CMake-built node was verified
byte-identical: SHA256
`9c70bb490a3450806be2c7d858e3d1ca58ff7c6fdb8c8fea5d46919cc5b0c958`.
The final incremental build left both tested node and unit binaries unchanged;
the new functional script is also present in the generated runner tree.

Evidence is under `/tmp/mercatura-b5-budget-sevSUV/`; final functional logs and
the actual per-case commands/exits are under
`/tmp/mercatura-b5-budget-functional-rvbmog84/`. The initial functional run had
already passed the budget checks but incorrectly expected an equal-work side
branch's `submitblock` result to be `None`. The fixture was corrected to also
allow the ordinary `inconclusive` response before presenting the longer branch.
The final seven-script run passed; this required no production correction.
The initial build emitted clock-skew notices and GCC 11 string-operation
warnings in unchanged code; no warning suppression or unrelated cleanup was
performed. The final incremental build passed without those warnings.

## Files and production diff

The production diff is **52 insertions and 2 deletions, net +50 lines** across
eight files: 50 added lines, two existing lines replaced, no standalone removals.

| Files | Purpose |
| --- | --- |
| `src/consensus/consensus.h` | Capacity-derived uint64 undo budget |
| `src/consensus/params.h` | Activation height, disabled by default |
| `src/kernel/chainparams.cpp` | Mainnet/default-regtest genesis activation |
| `src/kernel/chainparams.h` | Controlled regtest activation option |
| `src/chainparams.cpp` | Parse existing test-activation mechanism; reject public-network undo overrides |
| `src/chainparamsbase.cpp` | Document `undo` in existing option help |
| `src/undo.h` | Exact incremental accounting helper using the existing formatter |
| `src/validation.cpp` | Apply the check before eager input loading and copying |
| `src/test/undo_limits_tests.cpp`, `src/test/CMakeLists.txt` | Permanent unit coverage and registration |
| `test/functional/mercatura_undo_limits.py`, `test/functional/test_runner.py` | Permanent native coverage and registration |
| This file and adjacent `README.md`, `verification.cpp`, `run-verification.py` | Approved implementation record and preserved investigation artifacts |

The organization destination is remote `project`,
`git@github.com:Mercatura-Core/Mercatura-Core.git`, branch
`hardening/phase-b-mainnet`; the fork remote `origin` is left untouched.
The parent is the completed B4 commit
`2bd3d16eba1dba2c40e41cfd0a225fb45fbcadcf`, preserving all B1–B4 commits.
No release branch or unrelated untracked file is included. Stop after B5.
