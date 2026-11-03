# MercaWeave v0.1 — WO07C production-path integration

Started 2026-11-03, America/Los_Angeles. Implementation and execution in progress.

## Initial repository and baseline

Research root `/workspace/Mercatura-MercaWeave-v0.1`, branch
`research/mercaweave-v0.1`, clean starting HEAD
`0de90cccbb33c033edd5c3578a02d53967791682`.
Original `/workspace/Mercatura-Core` is clean on `work` at
`4b88b619fb1b08c7f30112a38eb471880b2a6af4`, also `project/v0.1`.
No additional commits need reconciliation. No AGENTS.md applies.

Read the complete supplied WO07C, the embedded locked WO01 specification,
WO01 source audit/validation, and WO02–WO07B reports. Existing Release,
checked-STL Debug and ASan/UBSan standalone builds reproduce **34/34 each**
before critical edits (61.70 / 211.20 / 164.01 seconds under concurrent load).
Leak detection remains unavailable in this ptrace environment, not passed.
The existing native functional runner is
`work/wo07-native-release/test/functional/test_runner.py`.

## Initial production-path map and gap register

| Path | Starting classification | Existing implementation / exact missing work |
|---|---|---|
| Wire, transaction/witness parsing | IMPLEMENTED / RESEARCH ONLY | Native transaction decoder plus bounded grouped research parser. Shared grouped grammar still lives under test/. |
| Group ownership, manifest, digest, ML-DSA | RESEARCH ONLY | `mercaweave::{Reconstruct,CheckOwnership,TransactionHashes,Verify}`; exact frozen vectors. Move the same implementation into a reusable production component, without duplicating cryptography. |
| Native Script dispatch | PARTIAL | Guarded WO04 checker hook; ordinary unknown-v3 remains permissive under mandatory flags. Add dormant typed group-context consumption, preserving that existing inactive behavior. |
| CheckInputScripts / CScriptCheck / caches | PARTIAL | WO07 selected function bodies use research View/Engine. Need authentic complete vin-order coins, immutable owned jobs, exact binding and uncached grouped route in production implementation. |
| Full Chainstate::ConnectBlock / DisconnectBlock | MISSING | Generic production connect/undo exists but has no grouped caller. Need isolated direct real routines, fee/resource checks, persistent undo, rollback, coins/reload and competing-history tests. |
| Mempool / block P2P acceptance | MISSING FOR V3 | Remains deliberately inactive. No activation, policy change or positive v3 node-process scenario is permitted. Native v2 tests are separate. |
| CWallet seed, encryption, native v2 signing | IMPLEMENTED | `InitializeMercaturaPQWallet`, `WithMercaturaPQMasterSeed`, EncryptWallet/Unlock/Lock, WalletBatch and SQLite. Reuse this authoritative seed and lifecycle. |
| CWallet v3 ownership / addresses / coins | MISSING | Only the WO05 facade owns grouped records. Need real wallet metadata loading, nonce/key derivation and IsMine/coin enumeration under isolated capability. No ordinary v3 receive flow. |
| CWallet grouped signing / construction | MISSING | Real SignTransaction currently accepts v2-only PQ input composition. Need complete group coordinator, atomic witness publication and mixed native signing. |
| Genuine wallet PSBT | PARTIAL | Native containers/parser/Merge; WO05 research orchestration. Need actual FillPSBT and strict grouped combine/finalize/extract with complete trustworthy prevouts. |
| Wallet persistence / recovery | RESEARCH ONLY FOR V3 | WO07 Store uses genuine SQLite in a separate namespace. Need actual CWallet/WalletBatch metadata, native encrypted seed, backup/reload and recovery, with no second ownership seed. |
| Ordinary Qt/RPC receiving/broadcast | IMPLEMENTED CLOSED | Existing gates and independent-key v2 defaults must remain closed for grouped custody. No new public enable switch or raw-script consensus prohibition. |

GAP 1 (full grouped chainstate) and GAP 2 (actual grouped CWallet) begin **OPEN**.
Neither will be closed on the strength of a renamed fixture or adapter alone.

## Intended implementation boundary

Promote the existing locked engine into a shared `script/` component, keeping
research includes as compatibility wrappers and preserving every vector. Use a
private, nonserializable in-process fixture capability; its construction exists
only in a guarded test header. No runtime option or deployment can mint it.
Add an immutable group-only authorization snapshot before independent real Script
jobs; native inputs retain genuine checks. Grouped operations bypass both whole
script-cache lookup and insertion. No new cross-transaction signature cache.

Exercise actual Chainstate methods using disposable native C++ test utilities,
real coins/undo/database components and caller-owned staging. Extend actual
CWallet methods and WalletBatch records, using the existing native protected PQ
seed and encryption. All positive grouped issuance/signing stays capability-bound
to isolated fixture wallet objects. Ordinary node/wallet/Qt behavior remains
inactive; unknown-witness permissiveness is never described as protection.

Builds use the existing GCC/SDK, three CPUs, about 9.7 GiB RAM and 3.2 GiB free
disk. Bound concurrency, preserve earlier evidence and failed diagnostics, and
do not repeat WO07B solely to inflate transaction counts. No chainparams,
activation, deployed datadir, original-worktree change, push or WO08 work.
