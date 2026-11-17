# MCA-R-001: bounded modified-fee accounting (Phase B6)

## Finding and reachability

Confirmed on predecessor `abfd95aac92dbf2269d7129e3ded7639c9ea2fc1`.
The completed investigation recorded six UBSan signed-overflow failures:

| Path | Baseline location |
| --- | --- |
| Single replacement conflict total | `validation.cpp:1063` |
| Package modified-fee total | `validation.cpp:1579` |
| Package replacement conflict total | `validation.cpp:1147` |
| Ancestor modified-fee total | `txmempool.cpp:923` |
| Cumulative fee diagram | `util/feefrac.h:127` |
| Reported fee delta | `txmempool.h:288` |

`prioritisetransaction` accepts a signed 64-bit adjustment through authenticated
RPC. Persisted local mempool adjustments are also reapplied on import. Individual
adjustments already saturate, but this did not bound sums across entries, or
the difference between a negative modified fee and its positive base fee.
Size and transaction-count limits do not establish a monetary range invariant.

There is no P2P fee-delta input. These confirmed cases require operator-provided
adjustments or equivalent local state; subsequent ordinary transaction admission,
replacement or querying could encounter the unsafe state. This does not establish
an unauthenticated current-chain reproduction. Nor can safety without deltas be
proved from `MAX_MONEY` alone: it is a per-transaction bound and the inherited
package/conflict count bounds multiplied by that amount exceed `CAmount`.

## Range invariant

Let `L = INT64_MAX`, base fee `b >= 0`, and effective, individually saturated
modified fee `m`. Define each entry's exposure as

```
e = b + abs(m - b)
```

Keep the sum of exposures of **distinct committed entries plus all staged
additions** at most `L`. Pending removals remain charged until commit. A shared
entry appearing in both the main and staging graphs is charged once.

This bounds `b`, `abs(m)`, and `abs(m - b)`. Every partial sum over a subset of
entries is consequently in `[-L, L]`. The graph's set additions combine disjoint
sets and its set removals remove subsets. Reversal negations cannot encounter
`INT64_MIN`. Ancestor/descendant sums, package totals, base-fee mining totals,
and cumulative diagrams are subsets of the reserved union.
Package validation already rejects duplicate txids, including witness variants,
so a workspace total cannot count the same reserved entry twice.

For RBF, conflicting entries and additions are distinct. Their difference is
bounded by their combined exposure. Diagram-prefix subtraction also cancels
common entries with the same modified fee; all remaining coefficients are
`-1`, `0`, or `1`, so it has the same bound. Prioritization updates both graph
levels; it does not retain two fee values for a common entry.

The helper uses `CheckedAdd` before subtraction-by-negation and before adding
exposure. It excludes `INT64_MIN` before taking an absolute value. It also checks
the exact rational per-kvB rate against `[-L, L]`, using the existing wide
`FeeFrac` comparison. This preserves the precondition of per-kvB reporting even
for transactions smaller than 1000 bytes. No floating-point monetary accounting
or widened shared graph arithmetic is introduced.

## Mutation and failure behavior

* Admission reserves exposure before constructing/staging the entry or mutating
  the graph. Insufficient range becomes ordinary `TX_MEMPOOL_POLICY` rejection
  with reason `modified-fee-out-of-range`.
* Prioritization checks the proposed effective entry fee and replacement
  exposure before updating the delta map, entry, graph, or update counter.
  Unsupported requests return RPC error `-8`. Ordinary positive/negative
  adjustments and individual saturation retain their existing behavior.
* An unapplied changeset releases its additions on destruction. Applying it
  transfers the reservation to committed entries; removal releases it once.
  Mempool consistency checks independently recalculate committed exposure.
* Unknown transaction deltas keep their existing signed saturation. They reserve
  no entry exposure until admission, where the effective fee is checked.
* Import uses the same admission/prioritization boundaries. Old unsupported
  entries may be rejected; an unsupported update of an already-present entry
  follows import's existing exception handling. No invalid entry state is kept.

This is deliberately a conservative policy at numerical extremes. A replacement
whose final state would fit can still be rejected if its transient union with
the old entries does not fit. That reservation is necessary for the unchanged
RBF and graph arithmetic to be safe while evaluating the replacement.

## Remaining calculations

Exposure alone does not bound a newly calculated target fee minus a negative
modified fee, or estimates duplicated for several outputs. MiniMiner uses checked
addition for those differences and returns its existing empty/optional failure
result when a bump or the requested aggregate cannot be paid within
`MAX_MONEY`. Wallet input-cost accumulation checks both individual and total
costs before applying bumps, and handles an unavailable estimate explicitly.
Preset and automatically selected input pools are each bounded by `MAX_MONEY`;
their combined bump/input-cost sums fit in `CAmount`. The final transaction fee
addition is checked as well. These checks change no consensus monetary rule.
The estimate bounds are conservative: duplicated ancestral costs can exceed the
estimate budget even when a later deduplication would produce a payable fee.
Such extreme requests return a controlled wallet failure instead of feeding
unbounded intermediate values into coin selection.

Eviction adds the incremental relay rate with saturation, instead of an
unchecked signed addition. A modified-fee-derived rolling minimum is converted
back to an integer rate with the ceiling

```
floor(INT64_MAX / INT32_MAX) * 1000  // 4,294,967,298,000 base units/kvB
```

This ensures a fee estimate for every size accepted by the `int32_t` fee-rate
API fits in `CAmount`, including rounding. It also avoids out-of-range conversion
of the existing floating rolling-minimum state. The existing decay algorithm is
preserved; unrelated operator-supplied fee-rate configuration is unchanged.

The miner's existing `min(MAX_MONEY, fees + subsidy)` uses saturating addition
before its monetary clamp, producing the intended mathematical result even
when the intermediate sum would not fit.

## Compatibility and cost

This is mempool/mining/wallet arithmetic policy, active on every network without
a protocol activation or new runtime selector. Block validity, authorization,
historical hashes, undo serialization, emission, difficulty, capacity scheduling,
and normal chain selection are unchanged. B1–B5 remain ancestors.

No serialization format changes. Existing supported mempool data and fee deltas
still load; unsupported extreme combinations now fail safely. Mempool entries
and graph fee representations remain signed 64-bit values.

The fix adds one 64-bit counter per pool. Mutation checks are constant time;
rollback and consistency checking visit their existing sets. MiniMiner's output
map receives one bounded linear pass. No large monetary-value fixture requires
large transactions or allocations. Ordinary RBF/package/mining comparisons keep
exact arithmetic; shared `FeeFrac` operations are neither widened nor saturated.

## Permanent validation

`modified_fee_tests` covers exact exposure boundaries, signed limits, positive
and negative adjustments, individual and pending-delta saturation, ancestor and
descendant totals, diagrams, staging failure/abort/commit, removal, package
admission rollback, positive/negative RBF conflict totals, reported fee deltas,
unpayable/duplicated bump estimates, and eviction-derived fee estimates.

`mercatura_modified_fees.py` uses three mature native PQ UTXOs on one isolated
regtest node. It checks RPC atomicity, normal admission after a rejection,
ordinary positive/negative priority adjustments, real PQ replacement and package
submission, restart/persisted priorities, controlled wallet failure, and mining
removal. It uses the existing consensus and fee-policy settings.

Production diff: seven files, **100 inserted lines and 15 deleted lines**
(85 net). Files: `src/txmempool.{h,cpp}`, `src/validation.cpp`,
`src/rpc/mining.cpp`, `src/node/{miner,mini_miner}.cpp`, `src/wallet/spend.cpp`.
The remaining changes are this report, the two new regression files, and their
unit/functional runner registration. No unrelated changes are included.

The affected daemon, CLI, consensus library dependencies and unit-test target
built successfully with GCC 11. GCC emitted a vector-insertion warning in the
unmodified `blockencodings.cpp`; no warning was reported in the changed
production files.

The final focused suite passes **8 cases / 84 assertions**, both normally and
under `-fsanitize=signed-integer-overflow` with
`-fno-sanitize-recover=signed-integer-overflow`. Instrumented translation units
are validation, mempool, txgraph, miner, MiniMiner, mining RPC, wallet spending,
and the focused tests. The instrumented daemon also passes the new functional
script, including RPC rejection, admission recovery, PQ RBF/package submission,
restart and wallet error handling. No signed-overflow diagnostic occurs. This
is targeted instrumentation, not a claim of a fully sanitized node build.

The B1–B5 unit run passes **18 cases / 132,820 assertions**. Wallet
coin-selection and native spending-size regressions pass **16 cases / 9,586
assertions**. The existing mempool/RBF/package/MiniMiner/txgraph/FeeFrac suites
plus the initial B6 suite pass **39 cases / 3,510 assertions**. The final focused
8-case run adds the exact-1000-byte eviction boundary and runs admission and
replacement with mempool consistency checking enabled (84 assertions, as above).
All final listed build/test commands exit zero; `git diff --check` passes.

All nine bounded functional scripts pass (exit 0, no timeout):

| Script | Coverage |
| --- | --- |
| `mercatura_modified_fees.py` | New B6 RPC, PQ RBF/package, restart, wallet and mining checks |
| `mercatura_fee_policy.py` | Existing native PQ fee/dust and relay behavior |
| `mercatura_fee_boundary.py` | Exact full-byte PQ wallet fee boundary |
| `mercatura_mercaweave.py` | Native v3 and mixed v2/v3 authorization, fees, wallets, reorg, verification and recovery |
| `mercatura_headers_lowwork.py --mercahash-v2-height=0 --pq` | B1 |
| `mercatura_classical_payment_safety.py` | B2 |
| `mercatura_recipient_totals.py` | B3 |
| `wallet_mercatura_pq_backup.py` | B4 wallet consumer |
| `mercatura_undo_limits.py` | B5, including disconnection/reorg, restart and both reindex modes |

Normal functional evidence: `/tmp/mercatura-b6-functional-v3kydfnj/`.
Instrumented B6 functional evidence: `/tmp/mercatura-b6-functional-i8i4c75v/`.

Development runs corrected test expectations for existing individual
saturation, zero-delta map erasure, and the RPC maximum-feerate policy before
the successful final runs. These required fixture corrections, not additional
production-policy changes.

Exact commands for the normal build and unit runs:

```sh
CCACHE_DISABLE=1 cmake --build /tmp/mercatura-b1-build \
  --target bitcoind test_bitcoin bitcoin-cli -j4
/tmp/mercatura-b1-build/bin/test_bitcoin \
  --run_test=modified_fee_tests:mempool_tests:rbf_tests:txpackage_tests:miniminer_tests:txgraph_tests:feefrac_tests \
  --report_level=detailed -- -testdatadir=/tmp/mercatura-b6-investigation-2vCG9P/fixed-units
/tmp/mercatura-b1-build/bin/test_bitcoin \
  --run_test=modified_fee_tests \
  --report_level=detailed -- -testdatadir=/tmp/mercatura-b6-investigation-2vCG9P/final-focused-checks
/tmp/mercatura-b1-build/bin/test_bitcoin \
  --run_test=headers_replay_tests:header_admission_*:spend_tests/recipient_totals_respect_monetary_limit:spend_tests/disabled_classical_outputs_are_rejected_before_funding:crypto_tests/mercatura_hkdf_sha3_384*:undo_limits_tests \
  --report_level=detailed -- -testdatadir=/tmp/mercatura-b6-investigation-2vCG9P/fixed-hardening
/tmp/mercatura-b1-build/bin/test_bitcoin \
  --run_test=coinselector_tests:spend_tests/pq_spending_modes_match_native_input_scripts:spend_tests/max_signed_input_size_uses_external_outpoint \
  --report_level=detailed -- -testdatadir=/tmp/mercatura-b6-investigation-2vCG9P/fixed-wallet
```

Execution artifacts, build commands, sanitizer archive/link driver, and bounded
functional driver are under `/tmp/mercatura-b6-investigation-2vCG9P/`.
The functional driver invokes each source test with its generated build config
and the existing `run_owned_test` cleanup wrapper, using unused loopback ports
and per-test timeouts. No external peers or large resource fixtures are needed.

The entire legacy functional suite, fuzz campaigns, and other compiler/platform
builds were not run. The manual-entry MiniMiner test API still requires correct,
bounded caller-supplied state; production callers copy the bounded mempool.
This fix is limited to MCA-R-001; it does not audit unrelated arbitrary
operator fee-rate configuration or all other resource/accounting limits.
