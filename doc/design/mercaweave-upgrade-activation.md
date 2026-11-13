# MercaWeave activation and deployment

Public Mercatura testnet (`ChainType::TESTNET`) is scheduled to activate
MercaWeave at block **23,000**. This is the approved source-level activation
schedule; publishing releases and upgrading deployed nodes are separate steps.

## Network rules

`Consensus::Params::nMercaWeaveHeight` is an optional network consensus parameter.
It is **23,000 on public testnet** and **unset on mainnet, testnet4, signet and
default regtest**. Public testnet block 22,999 remains preactivation; block 23,000
and later blocks enforce MercaWeave. Unset means there is no activation at any
height. Negative heights are inactive.
The candidate block height, rather than its parent height, determines the rule.

Only isolated regtest accepts `-mercaweaveheight`. The values `0` and a positive
height enable immediate and delayed tests; `-1` leaves activation unset. Other
negative values and values at or above `INT_MAX` fail startup. Supplying the
argument on a public network fails startup, including a supplied value of `-1`.
There is no public configuration override or transaction-provided activation bit.

Before activation, historical unknown-witness consensus remains permissive for
v3. This cannot be made safe by changing address encoding or a wallet label.
The native wallet refuses issuance, funding and signing of v3 outputs, ordinary
relay rejects v3 inputs and outputs, and mining refuses an unprotected v3 reward.
Raw block construction can still demonstrate the old permissiveness. Such a
control is evidence of the hazard, not successful grouped authorization.

At activation, witness version 3 requires the approved 32-byte native program,
the exact grouped ML-DSA-65 authorization and authenticated spent outputs. Missing
transaction-wide context fails closed. The new Script flag requires witness
verification; callers cannot obtain permissive success by omitting that flag.
P2SH wrapping, scriptSig data, bad roles, invalid commitments, duplicate exact
keys, forward/self follower references and invalid signatures remain rejected.
The locked `4G+N` work accounting and 4096-input, 256-group, 1024-member limits
are reused. Native v2 keeps its independent signature domain and verification.

## Validation, caches and reorgs

Normal mempool checks use the next block height. Thus at tip H-1, correctly
authorized transactions can enter for the first enforced block H. Wallet address
issuance and grouped signing are conservatively enabled only once the active tip
has reached H. Every mining template rechecks its candidate height.

Activated grouped validation builds an immutable context from actual current
coins before consulting any legacy full-script cache. It verifies each group
once, then gives asynchronous CScriptCheck jobs owned transaction/prevout data.
Followers may execute before anchors. Native inputs still execute ordinary
Script. Grouped authorization neither reads nor populates the old full-script
cache, and assumevalid cannot skip activated grouped Script verification.

If a reorg makes the next block inactive, mempool entries with v3 inputs or
outputs and their descendants are removed. Wallet issuance/signing and optional
rewards close again. Known authenticated wallet records remain available for
tracking and recovery; possession of those records is not an activation signal.
The Receive selector disables a selected grouped request without silently
replacing it with a v2 address.

The research decoder's 32 MiB aggregate buffer ceiling remains an offline input
policy. It is not added to consensus. Production serialization uses the native
serializer and existing chain byte/witness/work limits. Protocol domains,
commitments, digests, signatures and derivation bytes are unchanged.

## Safest subsequent deployment process

1. Finish qualification on the exact proposed source and obtain independent
   review of the activation, consensus, wallet and PSBT changes.
2. Let the founder finish the current v0.1.3 MercaHash V2 soak and assess existing
   mining, PQ networking, storage and wallet health. This document does not attest
   that the soak is complete or that deployed nodes have been inspected.
3. Coordinate operator upgrades and validating/mining adoption ahead of the
   approved public-testnet height 23,000. Any revision to that schedule or a
   separate mainnet activation requires its own coordinated decision.
4. Qualify binaries built from the agreed network-parameter commit on isolated
   matching chains and all supported platforms. Reproduce V1 historical replay,
   the V2 transition, compulsory PQ transport, reorgs and wallet recovery.
5. Obtain separate release, merge and deployment approvals. Publish the exact
   source/binary identities and activation policy; do not advertise v3 custody
   before protection is active. Existing v2 users need no fund migration.
6. Monitor the activation block and subsequent finality, validation rejects,
   competing tips, template construction, mempool work/bytes, PQ connectivity,
   CPU/GPU results and encrypted-wallet recovery. Agree response thresholds and
   responsible operators before rollout, using measurements rather than an
   invented universal threshold.

## Consensus-aware incident response

Before activation, withdraw the proposed release or revise an unactivated
schedule through an explicitly coordinated decision. Preserve backups and logs.
Do not reopen a grouped-key wallet with a client that does not support its format.

After activation, an old consensus client can accept invalid grouped spends as
unknown witnesses. Downgrading such a validating node is unsafe even if its wallet
uses only v2. A parameter toggle or binary rollback does not undo consensus.
Prefer a reviewed forward fix preserving accepted rules; any rule change requires
an explicit network coordination decision. Pausing optional grouped issuance or
mining is an operational containment action, not permission to weaken validation.
Preserve chain data, issued-range backups and evidence throughout recovery.

MercaHash V2, its public-testnet height 17000, native v2 ownership, mandatory PQ
networking, monetary policy, DGW, capacity schedule and permanent zero mining
software fees are preserved. No deployed v0.1.3 process or datadir is part of
this isolated development procedure.
