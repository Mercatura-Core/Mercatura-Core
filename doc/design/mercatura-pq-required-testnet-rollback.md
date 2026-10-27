# Required-PQ rollback playbook — preparation only

Public activation and deployment are pending. This playbook prepares a later
founder-authorized recovery; it does not authorize live operations. The final
enforcing profile has no runtime off switch. Rollback is a coordinated change of
binary/configuration or an approved protected snapshot, never an automatic
PQ-to-classical retry or a hidden exception for manual peers.

## Preconditions and triggers

Before any rollout, test the exact previous and candidate binaries against
consistent disposable historical snapshots, wallets and indexes. Establish that
the previous binary accepts every block and database/wallet format it will see.
If this has not been established, starting an old binary on newer data is not a
recovery plan. Consensus compatibility, including any separately approved future
MercaHash change, must be checked independently of networking compatibility.
Public capacity height 8,000 remains unchanged.

Define observation owners and thresholds for loss of independent bootstrap,
sustained inability to relay/synchronize, unsafe resource behavior, wallet/index
failure or deployment-relevant security findings. Record exact symptoms,
height/hash/work, software hashes and approved rollback authority. Do not respond
to a single rejected classical peer by weakening required transport.

## Coordinated procedure

1. Obtain the separately authorized operator decision covering the affected
   cohort. Explain compatibility consequences through the approved coordination
   channel. This assignment has sent no operator notifications.
2. Stop affected authorized services cleanly. Record accepted tip/hash/work,
   UTXO hash, wallet balances and indexes; preserve original datadirs, public
   diagnostics and protected recovery evidence. Do not wipe data or reset genesis.
3. Select the previously qualified binary/config or explicitly approved
   consistent snapshot. Preserve the newer original data in a separate protected
   location. Never restore a snapshot by silently discarding accepted history.
   Resolve competing-chain implications using ordinary most-work validation.
4. Restore networking identities only from trusted protected backups. Never
   automatically replace pins, import another chain's identity or expose seed
   material in a public support archive. Wallet keys remain separate.
5. Restart the agreed recovery cohort and verify unchanged genesis, best hash,
   chainwork, UTXO hash, wallets, indexes and independent peer reachability.
   A classical cohort and an enforcing cohort will partition; coordinate the
   entire affected dependency graph, including explorers/miners/clients.
6. Document actual convergence and any exclusions. Re-enter qualification before
   retrying the upgrade. Keep the failed candidate and data for diagnosis.

## Evidence boundary

The isolated rehearsal switches actual published v0.1.2 binaries to the development
candidate, rejects old classical clients during a partial upgrade, performs a
whole-cohort rollback on preserved data, then upgrades again and exercises both
reindex paths. Its generated V1 regtest history is synthetic. Public-testnet
execution at genesis proves identity/startup compatibility only. Neither certifies
rollback of actual historical public-testnet wallets, chainstate or indexes.
Consult the evidence JSON for executed results and exact binary hashes.

POSIX identities are unencrypted and need protected backups. Windows persistent
storage remains unsupported and fails closed; ephemeral Windows operation does
not promise a stable server fingerprint across process restart.
