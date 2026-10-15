# Witness-resource validation and qualification

Mainnet applies the R00-E rules from genesis: at most 1,003 items per input,
33,554,432 bytes per item, and `GetMaxBlockCapacityBytes(height) / 256` items
per block, including coinbase and repeated transaction occurrences. Existing
full-byte capacity remains authoritative. Public testnet, testnet4, signet and
regtest retain historical rules. There is no CLI opt-out.
Their legacy decoding retains its existing resource exposure; this compatibility
exception must not be described as witness-resource hardening of those networks.

## Parsing and consensus authority

`WitnessParseBudget` is owned by one decoding attempt. Nested `ParamsStream`
wrappers carry its pointer, not a separate counter per transaction. Counts are
checked and subtracted before witness descriptor allocation. Guards restore
consumption on failure; new attempts own fresh counters. Canonical CompactSize
encoding and generic vector/MAX_SIZE behavior are unchanged.

Core's P2P block, chunk-file and compact-block ingress resolves indexed parent
height before allocating witnesses. Unknown parents are deferred. Compact
reconstruction charges prefills, cached transactions and missing responses to
one block budget. RPC block submission/proposal, indexed disk reads and import
also select contextual budgets. Transaction/PSBT inspection uses the absolute
mainnet envelope; mempool admission independently uses current next-block height.

`ContextualCheckBlock` and `ConnectBlock` independently check resource limits
after witness authentication, outside validation caches. A parser rejection is
not an authoritative consensus rejection of a header. Unauthenticated alternate
witnesses must not poison the legitimate header. Template assembly reserves the
coinbase witness and accounts for complete selected packages.

## Generic decoding exceptions and trust boundaries

Generic transaction/block deserialization without a `WitnessParseParams` context
retains legacy behavior. This is required for historical networks and useful for
internal negative test fixtures; it is not a safe default for new mainnet ingress.
Any new untrusted entry point must explicitly select the applicable budget.

The network-neutral kernel C constructors `btck_transaction_create` and
`btck_block_create` accept caller-trusted bytes. A sufficiently large legacy
witness can cause significant descriptor allocation. These constructors have
no network/height argument and do not perform consensus validation. There are
no daemon P2P/RPC callers of these constructors in this tree. Kernel embedders
must apply bounded network-specific parsing before invoking them on untrusted
data. `btck_chainstate_manager_process_block` delegates to `ProcessNewBlock`,
which enforces witness authentication and resource consensus rules before block
acceptance/persistence; that does not protect earlier constructor allocations.
No public ABI or historical constructor semantics are changed by R00-E.
The kernel script-verification helper delegates to `VerifyScript`; it does not
perform full transaction/block consensus validation or resource admission.

The generic Cap'n Proto object wrapper is also network-neutral. Supported IPC
addresses are Unix sockets only (`src/ipc/process.cpp::ParseAddress`), or inherited
process descriptors. No TCP listener is implemented there. Operators must restrict
socket/directory access to trusted clients. Exposing this IPC through a separate
untrusted bridge requires bounded ingress in that bridge. IPC runtime qualification
is separate from the headless daemon tests below.

## Explicit qualification selection

Use an out-of-tree build with the daemon, CLI, wallet and `test_bitcoin` targets.
Run memory-intensive tests serially. With `BUILD` pointing to that build directory:

```sh
"$BUILD/bin/test_bitcoin" --run_test=witness_resource_tests,witness_resource_chain_tests,witness_resource_net_tests
"$BUILD/bin/test_bitcoin" --run_test=serialize_tests,transaction_tests,txvalidation_tests,validation_block_tests,script_tests,block_capacity_tests,blockchunk_tests,blockencodings_tests,mempool_tests,miner_tests,txindex_tests,txospenderindex_tests,psbt_wallet_tests
python3 "$BUILD/test/functional/test_runner.py" --jobs=1 mercatura_witness_resource.py mercatura_block_capacity.py mercatura_reindex.py wallet_mercatura_pq.py mercatura_pq_auth_commitments.py mercatura_pq_invalid_auth.py mercatura_classical_ownership_shutdown.py mercatura_pq_psbt.py mercatura_pq_reorg.py mercatura_pq_mempool_persistence.py
```

`mercatura_witness_resource.py` is registered in `MERCATURA_SCRIPTS`. It uses two
disposable mainnet nodes, a public-testnet inspection node, native MercaHash mining and the frozen genesis, with
automatic outbound connections disabled. It exercises malformed ordinary blocks,
compact prefills and missing responses, valid-body recovery, over-capacity chunk
metadata rejection, historical-network inspection selection and both reindex modes.
It does not enable mainnet rules on regtest or alter network parameters.

The C++ mainnet-chain regression separately tests cached, prefilled and newly
supplied compact transactions sharing the aggregate allowance, plus full-block
boundaries, template accounting, direct connection and disk reconstruction.

## Qualification limits

At launch, block capacity is 1 MiB, while chunk transfer is reserved for blocks
larger than an ordinary P2P message. A valid complete mainnet chunk transfer
therefore requires future-height history. Synthetic chunk assembly/completed-file
decoding is covered in C++; it is not proof of the whole live future-height
transfer state machine. Do not create artificial production-height history merely
to describe this as fully qualified.

Synthetic historical-network parsing tests must remain. Actual public-testnet
historical replay is an additional release qualification when chain data is
available; use a disposable copy, never the active datadir. No historical chain
data is downloaded by this regression. Maximum 1 GiB block memory behavior,
capacity-boundary reorganizations, external kernel embeddings, IPC runtime,
Windows, macOS and Qt require separate qualification.
