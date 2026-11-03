# MercaWeave v0.1 — WO06 Qt and native mining integration

Date: 2026-11-02, America/Los_Angeles. Offline research only.

## Pre-edit inventory and boundaries

Verified clean research root /workspace/Mercatura-MercaWeave-v0.1 on
research/mercaweave-v0.1 at 259ef51316245a84348a1f6cdffaff9787bbcd8e.
Original /workspace/Mercatura-Core is clean on work at
4b88b619fb1b08c7f30112a38eb471880b2a6af4, also project/v0.1.
Both origin/project refer to https://github.com/Mercatura-Core/Mercatura-Core.git;
the two existing worktrees are retained without fetch/pull/push or branch edits.
No AGENTS.md was found at repository or ancestor paths.

Read the complete WO05 report. The authoritative locked spec and November 2
implementation directive are embedded in the self-contained WO01 attachment,
/workspace/attachments/05713317-32f0-4abd-8c3e-31a77781b6ee/Pasted text.txt,
Parts II/III. This is the supplied authoritative equivalent of the named files;
no missing-spec stop condition applies. Recovery defaults are 100 address
candidates and 20 successive unused groups; issuance rotates at 64 addresses.

Actual inspected paths: ReceiveCoinsDialog -> AddressTableModel::addRow
(BECH32M compatibility bucket, native v2) -> ReceiveRequestDialog and recent
requests; WalletModel::prepareTransaction -> interfaces::Wallet::createTransaction,
WalletModel::requestUnlock, SendCoinsDialog PSBT/FinalizeAndExtractPSBT/sendCoins;
WalletModelTransaction native fee-size helper; TransactionDesc native status;
MiningPage -> MiningSession -> wallet::GetMiningDestination -> SoloWorkProvider
and MiningController. MiningSession coordinates wallet preparation off Qt and
tracks unload tokens. GetMiningDestination accepts owned native v2 only and
SoloWorkProvider places the full reward into the selected payout script.
Existing fee, reward, CPU/GPU, MercaHash, unlock, send and broadcast paths stay
unchanged. Existing Qt mining tests use real regtest chain/node state and actual
mining, so they are outside this offline work order and must not be launched.

Implementation inventory, established before source edits:

1. Shared Qt capability/presentation model and optional native-widget panels;
   production network authorization remains unconditionally closed, with mock
   injection compiled only in isolated targets. No secret fields in Qt state.
2. Narrow additive bindings in WalletModel and existing Receive/Send/Mining
   layouts. Default v2 receive and native mining controls retain their paths.
3. Isolated backend adapting actual WO05 address, funding, PSBT and validation
   functions; expensive tasks off the GUI thread, owned public results and stale
   generation rejection, guarded QObject lifetimes.
4. Deterministic mature-reward planner/scheduler: disabled default, threshold 50
   per exact key, strict v2 destination, maturity, resource/fee/dust checks,
   reservations, idempotence, cancellation/conflict/reorg/unload handling. No
   signer/unlock/broadcast timer and no durable live-wallet scheduler.
5. Focused headless Qt and planner tests in three configurations, actual affected
   application translation-unit compilation, normal/isolated source and symbol
   boundary checks, WO05 regression reruns and preserved prior evidence.

Actual environment is Debian/GCC/x86_64 cloud, not the example WSL/Ryzen host.
WO05 Release baseline was rerun: 22/22 pass, 11.07 seconds. Qt runtime 6.8.2 was
present without development files. Matching Debian Qt development packages were
downloaded through the permitted package proxy and extracted only under ignored
work/wo06-tools; no system installation, node, wallet or datadir is involved.

Results and final verdict will be recorded after implementation and testing.
