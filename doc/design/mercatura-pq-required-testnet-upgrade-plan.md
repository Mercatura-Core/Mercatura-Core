# Public-testnet compulsory-PQ upgrade plan — preparation only

## Current founder-approved policy

- **MercaHash V2 public-testnet activation: block 17,000 — FOUNDER APPROVED.**
  Height 16,999 is the last V1 block; height 17,000 and later require V2.
- **Public-testnet PQ transport: COMPULSORY FROM NODE STARTUP — FOUNDER APPROVED.**
  **PQ transport has no block-height activation.** Genesis sync, reindex, reorg,
  offline recovery and network activity toggles do not weaken that policy.
- Mainnet V2-from-genesis implementation is **DEFERRED** until the founder has
  observed real public-testnet V2 operation for several days after activation.
  Mainnet genesis, PoW settings and required-PQ policy are unchanged here.
- Release publication and live deployment are **NOT YET AUTHORIZED**. No official
  version/tag is assigned. Founder review and integration, exact-source release
  qualification, packages and operator coordination remain separate steps.

Ordinary binaries require public-testnet PQ even when the deprecated compatibility
CMake option MERCATURA_REQUIRE_PQ_TESTNET is OFF. Omitting -pqp2p or specifying
-pqp2p=required works; -pqp2p=off is rejected. No V1/BIP324 fallback follows failed
authentication. The five-message handshake and existing identity/pin safeguards
are unchanged. Mainnet remains compulsory, regtest opt-in, signet/testnet4 unchanged.

getnetworkinfo.pqp2p.required reports actual local enforcement. The legacy
testnet_enforced_build field is retained as an always-true compatibility alias for
this binary's public-testnet policy. testnet_activation is required-from-startup,
describing approved software policy, not completed live-network deployment.
Authentication and expected-identity verification remain distinct peer properties.

The independent AKE-review waiver remains an accepted experimental-testnet risk;
it is not completed independent certification. Formal two-independent-operator
bootstrap qualification remains deferred. At least one publicly reachable PQ
bootstrap and a fresh-node connection/synchronization check are still required
before cutover. The existing offline inventory renderer's 2–16-operator schema
is unchanged and does not prove public reachability or operator independence.

See the current [pre-release qualification](../validation/MERCATURA-TESTNET-17000-PQ-REQUIRED-PRE-RELEASE.md)
for exact commits, measured results and limitations. The three upstream security
fixes and subsequent portability repairs remain in the branch history.

## Two independent upgrade requirements

Compulsory PQ is a coordinated software cutover: an upgraded process uses PQ on
its first connection. Old classical-only releases cannot communicate normally
with enforcing peers. There is no classical grace period, clock gate, tip-height
gate, automatic downgrade or remote-height trust exception.

MercaHash V2 is a separate consensus transition at 17,000. Nodes/miners must be
upgraded before it; 16,999 uses V1 and 17,000 uses V2. There is no difficulty reset,
signaling vote, genesis replacement, checkpoint addition or chain reset. Capacity
still changes at 8,000 and follows its existing growth schedule. The target block
spacing remains150 seconds, not a promise of wall-clock activation time.

The founder reported height 13,450 and a 4–5 hour manual release process during this
assignment:3,550 blocks remained relative to that report. Explorer page responses
available to this environment were inconsistent, so this is explicitly a founder
observation, not an independently verified RPC tip or forecast. Recheck the actual
tip and the remaining deployment window before the later live operation. Never
automatically move the approved17,000 height.

## Qualification and operator inventory

Keep candidate source SHA, platform/build options, binary hashes, test receipts,
CI run identities and documented physical-device exclusions. A passing build does
not establish installer readiness, reachability, operator acknowledgment or cutover.

Inventory each archive/backbone, validator, explorer/RPC service, miner, seed and
wallet client: owner, binary SHA/version, OS/architecture, public destination and
independently verified pin where used, current height/hash/work, indexes, wallet
formats and rollback authority. Several addresses do not establish independence.

On disposable copies, compare genesis, height/tip, chainwork, UTXO state and
index configuration across direct load, restart, reindex and chainstate recovery.
Use exact-source measured evidence. The prior security report's original-snapshot
gap stays historical; any newly available original fixture must be identified and
qualified independently at the new candidate. Synthetic wallet tests do not prove
migration of the founder's real wallets. Original/active datadirs are never test
fixtures. Back up wallets with supported tools and protect networking identities.

## Later founder-controlled rollout

These are preparation instructions, not authority to perform live operations.

1. Review the final source and completed native/GUI/PQ qualification. Approve
   integration into v0.1 separately; prepare and verify compatible platform
   packages through the manual release process. Do not infer a new official
   version or tag from the development label.
2. Record the actual current chain tip, upgrade window, compatible operator
   cohort, bootstrap destinations/pins, recovery custody and acknowledgments.
   Allow normal PQ-connected V1 operation before the V2 boundary where possible.
3. Stop authorized services cleanly for consistent snapshots. Preserve original
   data and verified old binaries; never use a live LevelDB directory as a copy
   for negative testing. Qualify rollback only within supported offset ranges:
   old 32-bit readers cannot safely reopen metadata with wide offsets above 4 GiB.
4. Upgrade the agreed backbone and clients in the approved order. Restart
   establishes new PQ sessions. Verify required=true, pq-v1, authenticated peers,
   correctly verified pins, session changes on reconnect and no fallback.
5. Verify fresh genesis joins, offline/lagging recovery, normal most-work sync,
   transaction/block relay, native PQ mining rewards and explorer convergence.
   Check bootstrap failure recovery within an authorized controlled rehearsal.
6. Observe bounded pending<=64, queued<=32 and active<=2 crypto work, failure
   counters, memory/descriptors and disconnects. Cumulative successes are neither
   a live peer count nor proof of independent operators.
7. Confirm all producers use the shared candidate-height PoW selection. At
   17,000 verify actual V2 blocks and continued DGW/emission/capacity behavior.
   Leave mainnet V2 deferred until several days of observed public-testnet V2
   operation and a separate founder implementation decision.
8. Record measured cutover results before declaring the live upgrade complete.
   Any rollback requires the approved coordination and data-recovery plan.

No public announcement, DNS/seed edit, remote restart, release asset or live
network operation is authorized by this document.

## Consensus-aware rollback limit

After block 17,000, a V1-only release cannot follow the upgraded public-testnet
chain. A rollback must retain V2 consensus support or use an explicitly approved
coordinated recovery plan; backups do not make an obsolete consensus implementation
compatible. Before or after that boundary, transport rollback also requires an
agreed compatible peer cohort. No automatic classical fallback is available.
