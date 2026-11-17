# MCA-A-004: future undo-record and resource limits

> Historical investigation before project approval of the 2× budget. See
> [IMPLEMENTATION.md](IMPLEMENTATION.md) for the approved correction and its
> validation results. The original investigation text and fixture are retained.

Status: investigation complete; implementation requires a design decision.
No production or consensus code changed. No B5 commit or push is authorized by
the assignment's completion conditions until a complete correction is selected
and tested. These investigation artifacts are deliberately left uncommitted.

## Baseline

Inspected on 2026-11-17 in `hardening/phase-b-mainnet` at
`2bd3d16eba1dba2c40e41cfd0a225fb45fbcadcf` (the required B4 commit).
All four B1–B4 commits are ancestors. The initial tracked working tree was clean.
Existing untracked `mercatura_difficulty_export.py`, `release-artifacts/`, and
`test/config.ini` were preserved.

Live `git ls-remote` checks found:

| Remote | Repository | Hardening branch SHA |
| --- | --- | --- |
| project (configured upstream) | Mercatura-Core/Mercatura-Core | 2bd3d16eba1dba2c40e41cfd0a225fb45fbcadcf |
| origin | gwestcot/Mercatura-Core | 2b225460961d3800b8ea99fa94aa8885c2f63684 |

Origin is at B1, an ancestor of local B4. This is reconcilable by a future
ordinary fast-forward push, without losing B1–B4. No refs were changed here.

## Construction and consensus validity

One concrete script is nineteen repetitions of a 520-byte data push followed
by `OP_DROP`, ending in `OP_TRUE`. Its size is
`19 * (3 + 520 + 1) + 1 = 9,957` bytes. The push contents can be arbitrary.
It has nineteen counted operations, a maximum stack depth of one, no sigops,
and no signature or witness authorization. It succeeds with an empty scriptSig,
including under the standard script verification flags. It is neither P2SH nor
a witness program, nor unspendable under `CScript::IsUnspendable()`.
Disabling classical signatures does not disable this script.

A transaction with 24,000 distinct empty-scriptSig inputs and one zero-valued
`OP_TRUE` output is exactly `41 * 24,000 + 22 = 984,022` bytes. It passes
`CheckTransaction`, including the inherited 1,000,000-byte stripped-transaction
ceiling and duplicate-input checks. Sequence-final inputs and locktime zero
satisfy finality and relative locks. Twenty such transactions use distinct
previous outputs; their transaction hashes must also be distinct.

The previous outputs are consensus-feasible. For example, 4,800 distinct
coinbase transactions at heights 1,000,000 through 1,004,799 can each contain
100 zero-valued outputs with the large script. Each funding transaction is
996,855 bytes, below the transaction limit and the then-active 4 MiB block
capacity. Zero-value outputs and subsidy underclaiming are consensus-valid;
coinbase maturity is satisfied long before height 3,153,600. BIP34 heights make
the funding transactions distinct. These outputs need not be standard mempool
transactions, and miners need not obtain them through the mempool.

The bounded fixture checks funding transaction validity, UTXO retention,
maturity/value checks on a representative spend, script execution, transaction
size, and actual mainnet capacity parameters. Header PoW, timestamps, emission
state, merkle roots, and BIP34 are still ordinary block-construction
requirements. No end-to-end 480,000-output chain or overflow block was generated;
the complete construction is established by the component checks and size
arithmetic, not claimed as a full runtime reproduction.

For this concrete history, each undo coin contains:

| Field | Bytes |
| --- | ---: |
| VARINT(height * 2 + coinbase), heights above | 3 |
| Historical transaction-version dummy | 1 |
| Compressed zero amount | 1 |
| VARINT(script length + 6) | 2 |
| Script | 9,957 |
| Total | 9,964 |

The undo payload is `1 + 20 * (3 + 24,000 * 9,964)` =
**4,782,720,061 bytes**, plus the existing 8-byte header and 32-byte checksum.
With a 65-byte zero-output-value coinbase transaction, the spending block is
`80 + 1 + 65 + 20 * 984,022` = **19,680,586 bytes**. Other reasonable coinbase
choices change only this small overhead. Larger source heights or amounts can
increase undo size further.

## Earliest format-overflow boundary

Mainnet permits 16 MiB through height 3,153,599 and **32 MiB at 3,153,600**.
The example fits the latter but not the former.

This is also the earliest capacity at which any valid construction can overflow
the 32-bit record length, not just the first size accommodating this example:

* Each spending input requires at least 41 serialized block bytes.
* A retained script is at most 10,000 bytes. Its compression prefix needs at
  most two bytes; height metadata at most five, the dummy at most one, and a
  uint64 amount VARINT at most ten. Thus an undo coin is at most 10,018 bytes.
* There is one CompactSize input-count prefix per spending transaction and one
  outer transaction-count prefix. Both need at most five bytes under the block
  and transaction limits. Charge a whole five-byte transaction prefix to every
  input for a conservative upper bound.

For a block capacity C the payload is therefore at most
`floor(C / 41) * 10,023 + 5`. At 16 MiB this is **4,101,411,605**, below
`UINT32_MAX = 4,294,967,295`. Earlier capacities are smaller.

For 9,964-byte coins grouped in transactions of at most 24,000 inputs, the last
fitting count is 431,048 inputs (4,294,962,327 undo bytes); the next input makes
4,294,972,291 bytes. `SizeComputer` exercises these calculations without
materializing the undo data, as well as UINT32_MAX minus one, exactly, plus one.

## Serialization and lifecycle audit

* `undo.h`: `CBlockUndo` / `CTxUndo` are nested vectors of full `Coin` objects.
  `TxInUndoFormatter` retains the legacy dummy/version encoding.
* `compressor.h`: generic scripts are stored in full; there is no aggregate
  compression or deduplication. Scripts longer than 10,000 bytes are not UTXOs;
  the decompressor also substitutes an invalid script for overlong disk data.
* `WriteBlockUndo`: measures using a 64-bit `SizeComputer`, then requires a
  uint32 length, sets the storage-failure latch, and raises a fatal storage
  error on overflow. This happens after `ConnectBlock` has assembled all undo
  data. It writes magic + uint32 length, payload, and double-SHA256 checksum
  over the previous block hash and serialized payload.
* `ReadBlockUndo`: seeks directly to the indexed payload offset, skipping the
  header entirely; uses `HashVerifier` on actual bytes and verifies the checksum.
  It materializes the whole undo object before returning. It does not use the
  advertised record length as an aggregate allocation or read boundary.
* `serialize.h`: CompactSize's `MAX_SIZE` is 33,554,432 **elements** for these
  vectors, not a 32 MiB aggregate undo-byte limit. Five-million-byte reserve
  batches do not limit total retained memory. Valid per-transaction input counts
  are much smaller than this element ceiling; even maximum block transaction
  counts under the 1 GiB capacity ceiling fit it.
* `CBlockIndex::nUndoPos`, `nDataPos`, `FlatFilePos::nPos`, file-info `nSize` and
  `nUndoSize` are already uint64. Index and file-info VARINT encodings retain
  existing bytes for old values. File seek/truncate positions use checked
  signed 64-bit bounds. Allocation increments also have a size_t range check;
  32-bit address spaces cannot accommodate these RAM requirements regardless.
* Remaining 32-bit fields include the undo length, ordinary block record length,
  per-script compression length, block/transaction counts, and height encoding.
  The latter fields fit the existing 10,000-byte script, 1,000,000-byte
  transaction, 1 GiB block and signed-int height limits. Widening block lengths
  or heights is not needed for this finding. The legacy height-zero recovery
  helper `AccessByTxid` retains its inherited output-scan bound; newly written
  undo coins have actual nonzero creation heights, so that helper is not the
  reported construction's limiting path.
* Disconnect/reorg: `DisconnectBlock` reads the entire undo object before
  restoring inputs in reverse transaction/input order. A writer-only fix leaves
  this path's memory demand unchanged.
* Restart: `CVerifyDB` level 2 reads the full undo record; level 3 may disconnect
  blocks. The cache-size check for level 3 does not bound the level-2 undo read.
* Recovery: `ReplayBlocks` rolls back through `DisconnectBlock` and then rolls
  forward from raw blocks. Existing flush failure handling prevents publishing
  unsafe metadata. It cannot cure the deterministic per-block RAM/format issue.
* Reindex: scans raw `blk` files and revalidates/rebuilds undo via the same
  writer; it cannot bypass a legitimate oversized undo. Pruned reindex removes
  existing rev files and unusable block files before rebuilding.
* Pruning: blk/rev files are deleted together; accounting already uses 64-bit
  saturating sums. File selection, retained block ranges, and allocation-before-
  prune ordering remain relevant. The prune target is not a maximum undo-record
  size and cannot remove recent required data to make such a record smaller.
* Index connect/rewind in `index/base.cpp`, raw-transaction RPC verbosity paths,
  REST spent-output responses, and the kernel spent-output API also call the
  same full-object reader. REST may additionally serialize spent outputs into
  another in-memory buffer. A streaming redesign must account for these users.

## Resources and why a wider length alone is insufficient

The concrete example contains **4,779,360,000 script bytes**. `UpdateCoins` moves
them into undo coins, while `CheckInputScripts` copies them into each
transaction's `PrecomputedTransactionData::m_spent_outputs`. The `txsdata` vector
lives until `ConnectBlock` returns, so these two representations alone retain
**9,558,720,000 bytes (8.90 GiB)**. Add Coin/CTxOut allocations, block/input data,
UTXO cache/database, allocator overhead, and queued `CScriptCheck` output copies.
This demand occurs before the uint32 check. Merely streaming the final disk
write does not remove it. Reads, disconnection and restart have separate large
allocations even if connecting is fixed.

Conservative payload envelopes grow to 8,202,823,205 bytes at 32 MiB and
262,490,582,957 bytes at 1 GiB. Script storage alone can approach 243.9 GiB per
copy at the final capacity. These are upper bounds, not exact maximum-size
blocks, RSS measurements, or claims about typical traffic. Already at 4 MiB,
the input/script amplification allows roughly 0.95 GiB of script bytes per copy;
the first 32-bit overflow boundary is not the beginning of RAM risk.

The example needs about 4.45 GiB of undo disk space for one block, after prior
chain/UTXO storage of those outputs. A pre-funded sequence of 288 comparable
blocks retains about 1.25 TiB of undo before file-granularity effects. Pruning
cannot promise a small fixed target while recent undo is still needed. Such a
sequence requires enough distinct historical outputs; it is not a claim that
the same outputs can be spent repeatedly without recreation costs.

Processing must fetch and examine all spent scripts. The example executes
9,120,000 push/drop pairs, serializes/hashes approximately 4.78 GB for the undo
checksum, writes approximately 4.78 GB, and reads/hashes it again for undo
verification. Header length widening does not bound that work. No enormous
allocation or chain generation was performed for this investigation.

## Viable alternatives requiring approval

1. **Recommended: an activated consensus budget on aggregate spent-output/undo
   bytes per block.** Select a deterministic metric, numeric budget, activation
   height/mechanism and node resource target. Keep it below UINT32_MAX, and
   substantially lower if ordinary nodes must validate and reorganize within
   modest RAM. Account for metadata and count-prefix overhead exactly or use
   a proved conservative relation. Enforce incrementally while fetching inputs,
   before copying all spent outputs or accumulating undo. A check solely in
   `WriteBlockUndo` is too late. Keep the existing storage format, checksum,
   indexes, capacity schedule and all historical decoding unchanged. Restrict
   only blocks from the agreed activation onward and assess any previously
   accepted blocks against that activation plan. This is a consensus tightening
   (soft-fork rule), requiring coordinated adoption and activation. A configurable
   local reject limit cannot be substituted without consensus divergence.
2. **Preserve every currently valid block: extended records plus bounded
   validation/undo processing.** A zero uint32 length can unambiguously mark a
   following uint64 length because a valid undo payload is never empty. Keep
   ordinary records byte-identical; use 16-byte headers only for extended
   records, update payload offsets and 48-byte extended overhead, retain the
   existing payload/checksum. Indexed reading does not inherently need a new
   payload format, and old files remain readable. However, full-block undo
   accumulation, whole-block spent-output precomputation, reverse undo traversal,
   restart/recovery, index/RPC/REST consumers, and robust bounded malformed-record
   reads all need attention. Disk can still reach hundreds of gigabytes per
   block at the final scheduled capacity. Older binaries still fail when they
   reindex/connect oversized records; no blanket downgrade-compatibility promise
   is justified. This is a significant storage/validation design project, not a
   small length-field correction. Consensus rules could remain unchanged, but
   hardware/disk requirements must be explicitly accepted.
3. **Change/freeze the capacity schedule.** A permanent ceiling of 16 MiB avoids
   this particular uint32 overflow by the bound above, but still permits several
   GiB of scripts and multiple copies. It does not by itself resolve the resource
   part of the finding. A lower ceiling/resource target would need separate
   selection and analysis. Changing the promised future schedule tightens
   consensus and needs activation; it sacrifices unrelated transaction capacity.

Option 1 is the smallest complete direction because it reuses all existing
serialization and storage and directly bounds the amplified resource. No numeric
budget or activation policy has been selected or implemented independently.
There is no proof that a consensus change is the only possible solution;
option 2 is the more extensive consensus-preserving alternative.

## Verification

The adjacent `verification.cpp` is a standalone investigation fixture, not an
integrated regression test for a fix. `run-verification.py BUILD` links it into
the existing unit-test objects from a built CMake **Unix Makefiles** tree, using
the tree's compile flags and link command. It writes only a fresh temporary
directory. It does not change CMake or the ordinary test executable.

Final commands and results (all exited zero):

```sh
CCACHE_DISABLE=1 cmake --build /tmp/mercatura-b1-build \
  --target test_bitcoin bitcoind bitcoin-cli -j2
python3 doc/design/mca-a-004-undo-limits/run-verification.py /tmp/mercatura-b1-build
/tmp/mercatura-b1-build/bin/test_bitcoin \
  --run_test=blockstorage_durability_tests,flatfile_tests,blockmanager_tests,block_capacity_tests,compress_tests,coins_tests,headers_replay_tests,spend_tests,crypto_tests \
  --report_level=detailed -- \
  -testdatadir=/tmp/mercatura-b5-investigation-991XSO/regression-data
/tmp/mercatura-b1-build/bin/test_bitcoin \
  --run_test='header_admission_*:script_tests/mercatura_pq_witness_v2_end_to_end:psbt_wallet_tests/mercatura_pq_psbt_wallet_signing' \
  --report_level=detailed -- \
  -testdatadir=/tmp/mercatura-b5-investigation-991XSO/extra-unit
python3 /tmp/mercatura-b5-investigation-991XSO/run-functional.py
git diff --check
git diff --cached --check
```

| Check | Result |
| --- | --- |
| Build of current B4 node, CLI, unit binary | Passed; no warnings/errors in build log |
| B5 standalone investigation fixture | 3 cases, 61/61 assertions passed |
| Storage, positions, capacity, compression, coins, B1–B4 unit suites | 94 cases, 1,888,109/1,888,109 assertions passed |
| Additional B1 header admission and PQ signing/PSBT cases | 5 cases, 318/318 assertions passed |
| `mercatura_reindex.py` | Passed |
| `mercatura_chain_reorg.py` | Passed |
| `mercatura_headers_lowwork.py --mercahash-v2-height=0 --pq` (B1) | Passed |
| `mercatura_classical_payment_safety.py` (B2) | Passed |
| `mercatura_recipient_totals.py` (B3) | Passed |
| `wallet_mercatura_pq_backup.py` (B4 consumer regression) | Passed |
| Whitespace checks | Passed |

Functional cases ran sequentially in fresh directories under
`/tmp/mercatura-b5-functional-2x034n5o`, using the current
`/tmp/mercatura-b1-build/test/config.ini`, verified available loopback ports and
the existing `pq_lab_proxy.run_owned_test` wrapper to reap their own nodes.
Its `results.json` records every actual command and exit status. Loopback binding
needed an ordinary sandbox escalation; no public peer connections were used.
Pruning was exercised in the unit suites (including prune selection, unlinking,
usage accounting and failed-sync retention). The inherited broad Bitcoin pruning
functional suite was not run; no claim of new extended-record pruning coverage
is made, because no extended format was implemented.

An initial fixture expectation understated the small coinbase overhead by four
bytes; the assertion was corrected to the serialization result above. Initial
test invocations also needed the ordinary Boost `-- -testdatadir=...` separator
and colon-separated cross-suite selectors; the final commands above passed.

Runtime logs and the initial local build evidence are under
`/tmp/mercatura-b5-investigation-991XSO/` and
`/tmp/mercatura-b5-build-current.log`.

The test process's measured peak RSS for the three focused cases was
323,824 KiB, including normal node/PoW test setup. The large logical offsets in
the existing storage tests use sparse files with bounded physical allocation.
No multi-gigabyte payload was allocated or written.

Implementation/push status: production diff **0 lines**; no fix claimed;
no commit; no push; release branch untouched. Stop at B5 pending approval of
the design direction, resource budget and activation where applicable.
