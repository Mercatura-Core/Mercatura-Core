# Required-PQ public-testnet upgrade plan — preparation only

Activation mechanism, any height/window, official version, publication and live
operations await founder decisions. No public deployment was performed. The
[activation options](mercatura-pq-public-testnet-activation-options.md) recommend
an approved coordinated required release whose transport policy is independent
of local tip, reindex and reorganization. This recommendation is not approval.

## Before scheduling

Close the release gates recorded in the P11 qualification report and
[decision package](mercatura-pq-activation-and-manual-release-decisions.md): actual
historical public-testnet replay, native packages/installers, independent bootstrap
and independent cryptographic review. Record the exact source SHA and artifact
hashes. Preserve capacity height 8,000; public MercaHash V2 activation is separately
pending. Neither supplies a PQ activation trigger.

Inventory every archive/backbone, independent validator, explorer, RPC service,
miner, seed and wallet/client population. Record owner, role, current binary,
OS/architecture, consensus compatibility, public endpoints/netgroups/ASN,
height/hash/work, index configuration, wallet formats and rollback authority.
Require at least two independently operated, verified PQ address sources with
independent failure domains. Multiple addresses belonging to one operator do not
satisfy this requirement. This assignment has no approved live operator inventory.

Use actual timestamped operator observations for chain height. Do not infer it
from release dates or the 150-second target interval. If the founder chooses a
planning height, account for software availability, upgrade lead time, operator
readiness, block-time variance and reorgs; do not insert a production constant
without explicit approval.

## Disposable qualification

The default development build has testnet activation pending and does not require
PQ automatically. `-pqp2p=required` is available for isolated qualification. The
explicit `-DMERCATURA_REQUIRE_PQ_TESTNET=ON` build profile requires testnet PQ from
its first connection and rejects `-pqp2p=off`. It is a qualification capability,
not an authorized live activation mechanism. Mainnet architecture always requires
PQ, but prelaunch mainnet is excluded from this plan.

On disposable copies, record genesis, best hash, chainwork, UTXO-set hash, wallet
balances and index configuration before and after startup/restart, full reindex
and chainstate reindex. Test the actual previous binary and exact candidate on
the same controlled data. Preserve backups separately; do not use live operator
data as a negative-case test fixture. The provided `pq-upgrade-rehearsal.py`
uses four owned processes and a verified previous binary. Its regtest history
and public-testnet genesis case do not substitute for actual public history.

## Founder-controlled rollout sequence

These steps are instructions for a later explicitly authorized operation.

1. Founder approves exact version/artifacts, activation mechanism and coordinated
   window, participating inventory, security dispositions and rollback plan.
   Confirm all required platform packages are already available and verified.
2. Operators verify artifact hashes and provenance over independently trusted
   channels. Record old/new executable locations, service configuration and
   protected recovery locations. Back up wallets using supported wallet RPCs.
3. Stop each authorized service cleanly before a consistent datadir snapshot.
   Protect networking identities and configuration separately. Copying live
   LevelDB files is not a validated backup. Keep the original data intact.
4. Upgrade the agreed backbone cohort and remaining dependent services within
   the approved sequence/window. Classical releases cannot interoperate with
   enforcing peers. Restart replaces established classical sockets; no in-place
   transport reinterpretation or compatibility downgrade is supported.
5. Select unchanged public testnet and approved operator bootstrap/pins. Never
   add a new genesis or hide a classical exception in manual/seed connections.
   `getnetworkinfo.pqp2p.required` must be true for the enforcing candidate;
   `testnet_enforced_build` identifies its immutable qualification profile.
6. Verify authenticated PQ peers, independently configured pins where required,
   fresh session IDs after reconnect, normal most-work convergence, wallet
   receipt, transaction/block relay, mining rewards, explorer synchronization,
   fresh genesis joins and offline/lagging recovery. Lose one bootstrap source
   deliberately only within the authorized rehearsal scope and verify recovery.
7. Observe pending <=64, queued <=32 and active <=2 along with coarse failure
   counters, memory/descriptors and disconnect rates. A cumulative handshake
   counter is not a live peer count or proof of operator independence.
8. Retain tested old binaries and protected backups through the observation
   period. Record actual results, exclusions and operator acknowledgments before
   declaring cutover complete. Use the separate rollback playbook if necessary.

No notification, remote restart, DNS update, release tag, asset upload or live
network cutover is authorized by the existence of this document.
