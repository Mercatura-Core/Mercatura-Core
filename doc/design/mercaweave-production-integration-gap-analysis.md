# MercaWeave upgrade candidate: repository audit and implementation gaps

Audit date: 2026-11-06, America/Los_Angeles. This records the inspection before
substantial implementation. The new founder work order authorizes development
on a separate integration branch; it does not authorize deployment or modify
the existing research freeze on its published branch.

## Exact source identities

| Identity | Verified value |
|---|---|
| Canonical repository | https://github.com/Mercatura-Core/Mercatura-Core |
| Current production/default branch | `v0.1` |
| Production branch commit | `4b88b619fb1b08c7f30112a38eb471880b2a6af4` |
| Official v0.1.3 tag | `v0.1.3-testnet` |
| Annotated tag object | `f231a27b2c19eff58e20718b0036147f83df1eee` |
| Tagged source commit | `3aa7bf649ed15314acd84e082632ec5d888d5baa` |
| Published research checkpoint | `664d491dd702a581d9914c664edbb3c53e0e977c` |
| Integration branch | `integration/mercaweave-v0.1-upgrade` |
| Integration worktree | `/workspace/Mercatura-MercaWeave-upgrade` |

Live remote refs and local ancestry were checked. Production is two README-only
commits beyond the official tag; its runtime source is the v0.1.3 tagged source.
The research checkpoint descends from production without a merge conflict or a
missing production consensus/networking commit. The release preparation branch
`release/testnet-v2-17000-pq-required` is an ancestor of the tagged source. The
separate macOS artifact branch adds packaging workflow changes, not a newer
consensus implementation; it is not adopted as the integration base.

Original `/workspace/Mercatura-Core` remains clean on `work` at the production
SHA. Research remains clean on its published SHA. No node/miner process was
found in this worker. No deployed executable, data directory or running public
node was inspected or changed. No live-chain height or soak completion is inferred.

## Existing implementation and gaps

WO01 source/validation, WO02 security, WO03 readiness, WO04 isolated consensus
and WO05 wallet reports were read with their acceptance limits. Later actual
Chainstate/CWallet and WO08 qualification were checked against the sources.
Historical reports describe their own stages; they are not rewritten to imply
that later production features were already present.

| Area | Existing reusable work | Production gap / implementation target |
|---|---|---|
| Authorization | Shared `script/pqgroup.cpp`: locked ML-DSA-65 domains, exact key membership, canonical anchors/followers, commitments, manifest, `4G+N`, limits and vectors. | Preserve these rules and bytes; do not introduce another signature scheme or digest. |
| Consensus orchestration | Immutable `GroupContext`, actual CScriptCheck queue and ConnectBlock preflight; private fixtures exercise real coins/undo and poisoned-cache rejection. | Select grouped validation from explicit consensus activation, cover normal mempool/block/direct Script entrypoints, require context after activation and bind active flags into all cache paths. |
| Activation | All production networks have no MercaWeave schedule; ordinary unknown-v3 consensus may succeed without authorization. | Add an optional network-specific activation schedule, leave every public height unset, and allow immediate/delayed activation only in disposable regtest tests. Reject public CLI overrides. |
| Preactivation safety | Native wallet cannot issue fixture group records; Qt reports unavailable. Generic raw witness tools still exist. | Keep historical unknown-witness consensus distinct from protected custody. Refuse normal wallet generation/funding and mining payouts to unactivated/unsupported v3, expose unavailable state clearly, and test the permissive raw consensus control. |
| Wallet custody | Actual PQ seed/encryption, authenticated issued ranges, SQLite/WalletBatch records, ownership, raw signing, exact transaction estimates and partial PSBT signing exist behind the private capability. | Make authenticated records load normally without granting activation; gate issuance/signing on authoritative active chain state, integrate ordinary receive/funding/transaction tracking and preserve v2/descriptor data. |
| Persistence/recovery | Real encrypted reload/backup/restore and known-range recovery are tested in components. | Qualify normal process restart, locked/unlocked behavior, rescan/reindex and reorg handling. Do not promise recovery of unknown issued ranges or protection against complete database rollback. |
| PSBT | Strict grouped namespace/roles, genuine partial signatures, atomic publication and explicit-capability finalizers reuse native PSBT containers. | Integrate ordinary RPC/coordinator finalization and combining, reject malformed/conflicting metadata, retain exact authenticated prevouts and full-byte fees, and qualify mixed v2/v3 end to end. |
| Qt | A research Panel, native-v2 QR/copy tests and preview model exist. Normal network capability is permanently closed. | Use one native Receive address selector, default v2; enable v3 only with actual wallet/consensus support. Preserve labels, requests, copy and QR. Replace redundant experimental receive UI rather than stacking another panel. |
| Mining | Native CPU/CUDA/OpenCL PoW dispatch and resource/worker controls already implement v0.1.3. | Preserve algorithms and 0% software fees. Keep v2 reward defaults; validate optional v3 reward destinations through the same activation rules. |
| Qualification | Research CI 390 and archival CI 391 passed all eight jobs; each native platform ran 187 CTest targets and 45 native functionals. | Those results qualify the old research source, not this future upgrade. Run new native consensus/wallet/Qt/mining/PQ gates and report exact source, commands, results, skips and platform limitations. |

## Preserved production invariants

Public-testnet MercaHash V2 height remains **17,000**, capacity transition remains
8,000, and compulsory PQ transport remains enforced. Genesis, DGW, monetary
controller/emission/rewards, fees/dust, V1 historical validation, V2 mining
algorithms and mining worker/resource logic are not redesign targets.

The existing locked witness format is native version 3 with a 32-byte program;
the anchor/follower grammar, ML-DSA context and wallet derivation domains remain
the approved research rules. Default ownership and reward addresses remain v2.

## Planned acceptance gates and decision boundaries

1. Before coding: record original/research identities and hashes of historical
   reports/evidence; create only the dedicated integration branch/worktree.
2. Implement a network-derived activation gate, with public heights unset and
   regtest-only test configuration. Test H-1/H/H+1, immediate activation, missing
   authorization context, malformed versions/lengths, cache separation and reorgs.
3. Integrate the existing wallet/PSBT path into ordinary interfaces, with
   activation checks for receiving, funding, signing and reward destinations;
   qualify encrypted reload, backup/restore, rescan and transaction tracking.
4. Complete native Qt selection and compatibility tests. Keep v2 defaults and
   clearly unavailable preactivation v3 state; no implied protection from merely
   decoding an unknown-witness address.
5. Run Mercatura-native unit and functional gates, Release/Debug/sanitizers where
   practical, protocol vector comparisons, network/mining noninterference,
   repository lint, and source/evidence preservation checks. The worker has a
   two-CPU cgroup quota and 8 GiB memory; two build/functional workers are justified.
6. Produce reproducible build, migration, qualification, release/approval and
   consensus-aware rollout/rollback documents. Do not call prior component-only
   evidence an activated-node qualification. Independent review, physical GPU
   validation and live-network qualification remain separately stated gates.

No public activation height, release version/tag, protected-branch merge,
installer, public deployment or live-node restart is selected. Any unresolved
security-critical consensus decision stops its dependent implementation; all
independent safe work continues. Founder approval remains necessary after the
v0.1.3 soak period; this worker cannot attest that the soak period is complete.
