# MercaWeave candidate wallet and Qt compatibility

Grouped ownership is optional. The default Receive and mining destination remains
Standard PQ witness v2. Existing native PQ seed encryption, descriptor records,
wallet transaction history and v2 derivation remain in place. No existing coin is
converted and no user must migrate funds to MercaWeave.

## Issuance and durable state

The normal CWallet reuses the authenticated WO05 account/purpose/group/address
derivation and its SQLite WalletBatch records. Receive, change and mining use
purposes 0, 1 and 2. Each request advances a durable address nonce; after 64 issued
addresses in a given account/purpose, the next group uses another key. The existing
4096-record wallet limit is retained and exhaustion fails without address reuse.
The format, genesis binding and derivation domains remain version 1.

After activation and unlock, use `getnewaddress "label" "mercaweave"`,
`getrawchangeaddress "mercaweave"`, or
`getnewmercaweaveaddress "label" account "receive|change|mining"`.
Ordinary address aliases and unspecified types retain their native-v2 behavior.
Unsupported purposes, networks, locked derivation and failed database commits
fail safely. `getaddressinfo` identifies current network protection separately
from authenticated wallet ownership, and supplies public key/nonce metadata for
explicit coordinator workflows. Those public values are not private signing keys.

The first grouped issuance atomically commits non-tolerable wallet feature bit
36 with the authenticated records. Supporting clients require that bit and both
record markers to agree. Older clients must refuse a wallet with an unknown
mandatory feature instead of silently ignoring its grouped records. Merely
running the new client on an old v2 wallet does not set the bit; opting into
grouped issuance does. Preserve a pre-upgrade backup for recovery planning.

## Encryption and recovery boundaries

Private group keys derive from the existing encrypted native PQ seed and are
cleansed after use. Public records require seed-derived authentication and exact
rederivation before they establish ownership. A cold encrypted reload does not
trust provisional grouped ownership before unlock. Once authenticated, relocking
can retain public ownership without retaining private key material.

After cold reload, unlock and rescan to recover incoming transactions missed
while grouped ownership was unauthenticated. Back up issued-range records after
issuing addresses; seed-only recovery does not discover arbitrary unknown issued
ranges. A backup can recover its authenticated known ranges and counters, not
future issuance absent from that backup. Authentication does not prevent complete
rollback of a valid old database; external backup/version discipline remains
necessary. No claim of hardware-wallet or descriptor-only grouped recovery is made.

## Transactions and PSBT

Normal funding and signing reuse native mixed v2/v3 coin selection, batching,
multi-output creation and full serialized-byte fee estimation. Grouped change is
explicitly requestable; default change remains native v2. Coin selection skips
grouped inputs when current activation/wallet support is unavailable. Signing
binds every input's actual spent amount/script and rechecks live coins before
publishing a staged result. Ordinary mempool/block validation is still required
after signing; no wallet API can freeze the chain indefinitely.

The coordinator reuses strict `MercaWeave` proprietary fields in PSBT v0 and the
existing native-v2 fields. It rejects conflicting roles, namespaces, unsupported
sighashes, wrong bodies, incomplete UTXOs and invalid available signatures.
Partial signers need the complete public prevout/role descriptions, including
foreign grouped keys/nonces, and only sign keys they own. Combining and finalizing
verify the supplied complete snapshots cryptographically; they do not certify
that an arbitrary coordinator's claimed coins are members of the live UTXO set.
The wallet signer authenticates its snapshot against current coins and ordinary
admission independently enforces membership and maturity.

Grouped coordinator policy bounds cloning: 16 MiB serialized PSBT, 4096 inputs
and outputs, 256 opaque fields per map, 32768 total opaque fields, 4096-byte opaque
keys and 4 MiB opaque values. These are local coordinator limits, not additional
consensus rules. They are checked after the native PSBT parser and before grouped
staged copies. No new preallocation-safe RPC parser guarantee is claimed.

## Native Qt and mining

Receive has one native QComboBox with Standard PQ witness v2 selected initially
and optional MercaWeave Grouped PQ witness v3. The latter requires active consensus
and a capable private-key wallet. Existing requests, labels, amount/message,
copy and QR dialogs remain the same native workflows. Loss of availability
disables an explicitly selected grouped request without silent fallback.

The concise Receive note explains that grouped spends can link addresses, key
compromise affects outstanding coins in that group, and issued ranges require
backups. There is no redundant experimental panel or extra grouped Send wizard;
the existing Send workflow selects and signs supported inputs.

Native CPU/CUDA/OpenCL workers still use the existing assembler and PoW dispatch.
Optional v3 reward destinations are checked against candidate-height activation
on every template. The solo miner additionally checks its selected chain before
accepting such a destination. Default v2 rewards, worker/resource controls,
statistics, hybrid mining and zero software fees remain the existing paths.
Hardware/platform qualification is reported separately from source preservation.
