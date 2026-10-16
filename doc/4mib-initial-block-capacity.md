# Experimental 4 MiB initial block capacity

This feature branch is a pre-mainnet consensus experiment. It is not mainnet
release approval and must not be merged into the development branch without
separate consensus/release review. The feature branch is
`feature/4mib-initial-block-cap`; its implementation target is 4 MiB.
Existing mainnet binaries with the old 1 MiB launch rule are incompatible with
larger launch-era blocks.

## Schedule

Capacity is the complete serialized block size, including witness bytes, with
no witness discount. The interval is exactly 1,051,200 block heights.

| Doubling | First height | Mainnet and regtest capacity (bytes) |
| --- | ---: | ---: |
| 0 | 0 | 4,194,304 |
| 1 | 1,051,200 | 8,388,608 |
| 2 | 2,102,400 | 16,777,216 |
| 3 | 3,153,600 | 33,554,432 |
| 4 | 4,204,800 | 67,108,864 |
| 5 | 5,256,000 | 134,217,728 |
| 6 | 6,307,200 | 268,435,456 |
| 7 | 7,358,400 | 536,870,912 |
| 8 | 8,409,600 | 1,073,741,824 |

The last value is the absolute maximum at all later heights. Public testnet,
testnet4 and signet retain their original 1 MiB initial capacity, ten doublings
and the same 1 GiB maximum. Genesis blocks and network identities are unchanged.

`Consensus::Params::nInitialBlockCapacityBytes` selects the network schedule.
Every capacity calculation takes consensus parameters explicitly, including
validation, miner/template policy, witness budgets and chain-aware P2P limits.
There is no command-line consensus override.

Mainnet retains witness resource enforcement from genesis: 1,003 items per input,
33,554,432 bytes per item, and floor(capacity / 256) aggregate items per block.
The launch aggregate budget is therefore 16,384. Regtest retains its existing
witness-resource activation setting; this experiment does not activate a second
consensus change there. Native PQ cryptography, sigop costs, standard transaction
weight (400,000 WU), fees and the 1 MB stripped-transaction ceiling are unchanged.
Remaining inherited weight-derived output-index and minimum-transaction bounds
are either context-free proof defaults or bounded by that unchanged individual
transaction ceiling; they do not impose a 4,000,000-WU block-validation limit.

## Transport, storage and RPC bounds

Ordinary messages retain their existing 4,000,000-byte limit; larger blocks use
the existing requested, sequential, disk-backed 1 MiB chunk protocol. No generic
message, vector allocation or HTTP request limit is raised. Disk reads and
reindex continue to use existing bounded block readers. Compact reconstruction
keeps its inherited 16-bit count bound. Blocks exceeding 65,535 transactions
fall back to full-block transport if they fit the ordinary message envelope;
blocks above that byte envelope use chunk relay. A compact-v2 request can
be fulfilled by chunks when the header-only requester does not yet know the
block size. Filtered and non-witness requests remain restricted. Announcement
and serving paths apply the same bound without changing the compact-block wire
format.

Merkle proofs retain the inherited default transaction-count bound. RPC proof
verification can enlarge it only using accepted active-chain index data; wallet
proof import uses a verified ancestor's network/height capacity. Other proof
structure and Merkle-root checks remain intact.

The inherited RPC HTTP body limit accommodates a 4 MiB block encoded as hex.
Larger future scheduled capacities may require separate RPC transport work;
this change does not claim end-to-end 1 GiB operational qualification.

## Permanent qualification

- `block_capacity_tests`: historical arithmetic/reorg behavior and every network's
  genesis, all doubling boundaries, saturation at extreme heights, witness-item
  allowance and template policy ceilings. No 1 GiB block allocation.
- `validation_block_tests`: direct ConnectBlock acceptance one byte below and at
  4 MiB, rejection one byte above, with each stripped transaction below 1 MB.
- `witness_resource_tests*`: mainnet 16,384-item decoding, enforcement, template,
  mempool, reconstruction, disk/reindex and malformed-ingress boundaries.
- `pmt_tests`: explicit authenticated proof-count expansion without changing the
  context-free anti-DoS default.
- `mercatura_block_capacity.py`: exact-size synthetic acceptance/rejection,
  proposal/submission, real chunk relay, 65,536-transaction full-block fallback
  and proof verification,
  restart and reindex. Padding is explicitly synthetic.
- `mercatura_pq_capacity.py`: independent real ML-DSA workloads with 384 and
  784 inputs, standard transactions, template construction, a block-only peer,
  chunk relay, wallet balances/UTXOs, restart and reindex. The largest workload
  must exceed 99% of the 4 MiB ceiling without padding.

The PQ test logs `REAL_PQ_CAPACITY` JSON observations and preserves a results file
in its test directory when run with `--nocleanup`. `receiver_input_and_pq_verification_ms`
is the existing node input/script verification metric, including more than the
ML-DSA primitive. Construction excludes PoW solving. Submission includes raw
RPC decoding, PoW and validation. Submit-to-peer-tip includes sender validation
and local propagation. Linux RSS values are process-lifetime high-water marks,
not per-block allocation deltas. Timings have no test thresholds.

Local regtest observations do not establish a production stale-block rate,
slow-peer resilience, WAN throughput, exchange/pool compatibility or supported
platform release readiness. The production target interval remains 150 seconds.

## Local qualification record

Linux x86-64, GCC 14.2, four available CPU cores. The daemon, CLI and unit-test
binary were rebuilt with `CMAKE_COMPILE_WARNING_AS_ERROR=ON` and
`CMAKE_CXX_FLAGS_RELEASE=-O2 -g`. All 518 build steps passed (Ninja elapsed
1,133.304 seconds). Assertions remained enabled.

The first strict build retained this workspace's old `-O1` setting and hit an
unchanged `checkqueue_tests.cpp` GCC array-bounds diagnostic. The same source
compiled successfully with optimized `-O2 -g -Werror -Wmaybe-uninitialized`;
the complete optimized build then passed without source changes or warning
suppressions. The checkqueue runtime suite passed too. An earlier capacity
integration run exposed a real compact-request/chunk-fallback disconnect; the
serving-path correction is exercised by the final two-node capacity tests.
Interrupted and superseded 8 MiB runs are not counted as final qualification.

Commands used (the existing build and lint environments were reused):

```sh
export LD_LIBRARY_PATH=/tmp/r00e-deps/usr/lib/x86_64-linux-gnu
/tmp/r00e-deps/usr/bin/cmake -S /workspace/Mercatura-Core -B /tmp/r00e-build \
  -DCMAKE_COMPILE_WARNING_AS_ERROR=ON '-DCMAKE_CXX_FLAGS_RELEASE=-O2 -g'
/tmp/r00e-deps/usr/bin/cmake --build /tmp/r00e-build \
  --target bitcoind bitcoin-cli test_bitcoin -j4
/tmp/r00e-deps/usr/bin/ctest --test-dir /tmp/r00e-build -j4 \
  --output-on-failure -R '^(checkqueue_tests|block_capacity_tests|blockencodings_tests|blockchunk_tests|witness_resource_tests|witness_resource_tests_chain|witness_resource_tests_net|blockfilter_tests|blockstorage_durability_tests|flatfile_tests|pmt_tests|merkleblock_tests|serialize_tests|net_tests|wallet_crypto_tests|psbt_wallet_tests|validation_flush_tests|dbwrapper_tests|coins_tests|miner_tests|crypto_tests)$'
/tmp/r00e-build/bin/test_bitcoin \
  --run_test='validation_block_tests/mercatura_launch_capacity*' --report_level=detailed
python3 /tmp/r00e-build/test/functional/test_runner.py --jobs=4 --nocleanup \
  --tmpdirprefix=/tmp/4mib/native mercatura_block_capacity.py \
  mercatura_witness_resource.py mercatura_coinbase_template.py mercatura_mining.py \
  mercatura_p2p_propagation.py mercatura_pq_multi_input.py \
  mercatura_pq_send_receive.py mercatura_pq_batching.py \
  mercatura_wallet_persistence.py mercatura_reindex.py \
  mercatura_network_identity.py mercatura_testnet_identity.py
python3 /tmp/r00e-build/test/functional/test_runner.py --jobs=1 --nocleanup \
  --tmpdirprefix=/tmp/4mib/pq mercatura_pq_capacity.py
```

The 21 CTest suites passed all 161 selected cases in 111.81 seconds. The three
direct ConnectBlock boundary cases passed all 32 assertions; the combined unit
commands took 113 seconds and both exited zero. All twelve adjacent native
functional scripts passed: 319 seconds runner time, 437 seconds including
initial cache preparation. The exact-size capacity script took 235 seconds.
It accepted and propagated 1,100,000, 4,000,001, 4,194,303 and 4,194,304 bytes,
rejected 4,194,305 with `bad-blk-capacity`, then accepted the same-parent 4 MiB
control. It also relayed a 65,536-transaction block, verified its proofs on
both nodes, and checked restart/reindex against the original UTXO hash and raw
accepted block bytes.

Ruff used the project's complete selector list, including the new untracked
Python test explicitly. Mypy checked 358 functional/developer Python files.
Both passed, as did the project tests/naming, includes, file permissions/names,
include guards, locale dependence and circular dependencies lint scripts.

The isolated real-PQ test passed in 131 seconds (exit zero), including restart,
proof import without duplicate credit, wallet balance/UTXO assertions, and full
reindex with the original tip and UTXO hash. Reindex took 41.792 seconds. Its
first attempt failed after 83 seconds because the new test used inappropriate
fees for Mercatura's 100 base units per coin. Funding now pays exactly 100 base
units and each 16-input spend pays 200, distributing split-output remainders
exactly. The retry used unchanged default RPC fee caps and relay/miner policy;
no fee override or production fee-rule change was needed.

| Measurement | Smaller real-PQ block | Near-capacity real-PQ block |
| --- | ---: | ---: |
| Serialized bytes | 2,040,236 | 4,165,211 |
| BIP141 weight (compatibility only) | 2,091,956 | 4,270,106 |
| PQ inputs | 384 | 784 |
| PQ spend outputs | 24 | 49 |
| PQ coinbase outputs (zero-value) | 1 | 1 |
| Transactions including coinbase | 25 | 50 |
| Signing and mempool admission (s) | 0.703354 | 1.947313 |
| Template and block construction, excluding PoW (s) | 0.105928 | 0.175558 |
| Submitblock, including PoW/decoding/validation (s) | 0.279721 | 0.379359 |
| Submit to peer tip, including polling (s) | 1.287647 | 1.38433 |
| Peer input/script/PQ verification (ms) | 30.61 | 53.36 |
| Peer ConnectBlock (ms) | 31.21 | 54.11 |
| Sender process-lifetime peak RSS (KiB) | 320,776 | 354,288 |
| Receiver process-lifetime peak RSS (KiB) | 296,748 | 310,968 |

The larger real-PQ block occupies 99.306% of capacity, with no padding. The
block-only receiving node had no mempool transactions before either block and
used `-maxsigcachesize=0 -assumevalid=0`; the larger block used chunk transport.
Construction/connection/submission observations are well below the 150-second
production interval on this local machine, but do not establish a WAN
propagation budget or stale-block rate.

A separate temporary single-thread microbenchmark, linked against this final
optimized `libbitcoin_crypto.a`, verified the same valid native ML-DSA-65
signature/digest repeatedly: 384 verifications in 0.049368 seconds and 784 in
0.101170 seconds (exit zero). This measures the portable verification primitive
with warm data; it is not a replacement for distinct-transaction block checks
or a cold-cache measurement. The block verification metric above includes
input checks and parallel script processing, so the two timings are not
directly comparable. No benchmark source/binary or generated artifact is
included in the changeset.

Final whitespace and staged whitespace checks passed. New C++ cases live in
existing registered test sources; the new Python regression is registered in
the default `MERCATURA_SCRIPTS` list. Platform builds, public-testnet history
replay, hostile/slow-peer/WAN testing and future-capacity operational
qualification remain separate release checks. GitHub Actions is deliberately
not monitored for this experimental branch.

## Changed-file inventory

- `doc/4mib-initial-block-capacity.md`
- `src/blockencodings.cpp`
- `src/blockencodings.h`
- `src/consensus/consensus.h`
- `src/consensus/params.h`
- `src/consensus/tx_check.cpp`
- `src/consensus/validation.h`
- `src/consensus/witness_limits.h`
- `src/kernel/chainparams.cpp`
- `src/merkleblock.cpp`
- `src/merkleblock.h`
- `src/net_processing.cpp`
- `src/node/miner.cpp`
- `src/node/miner.h`
- `src/rpc/mining.cpp`
- `src/rpc/net.cpp`
- `src/rpc/txoutproof.cpp`
- `src/test/block_capacity_tests.cpp`
- `src/test/blockencodings_tests.cpp`
- `src/test/pmt_tests.cpp`
- `src/test/validation_block_tests.cpp`
- `src/test/witness_resource_tests.cpp`
- `src/validation.cpp`
- `src/wallet/rpc/backup.cpp`
- `test/functional/mercatura_block_capacity.py`
- `test/functional/mercatura_pq_capacity.py`
- `test/functional/test_runner.py`
