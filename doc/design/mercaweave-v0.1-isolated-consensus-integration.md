# MercaWeave v0.1 — Work Order 04 isolated integration

Date: 2026-11-02, America/Los_Angeles. Research only; no network activation.

## Architecture recorded before implementation

Verified starting HEAD `2ff60be0b6c5f58f36110ecba5cb72ca2663126d` on
`research/mercaweave-v0.1` in `/workspace/Mercatura-MercaWeave-v0.1`, clean.
Original `/workspace/Mercatura-Core` remains clean on `work` at
`4b88b619fb1b08c7f30112a38eb471880b2a6af4`. WO03 Release baseline: 11/11 pass.

Intended call path: isolated in-memory authoritative coin view -> genuine
CheckTransaction and Consensus::CheckTxInputs -> vin-order owned snapshot ->
WO03 complete uncached Prepare -> private immutable script jobs -> real
VerifyScript/VerifyWitnessProgram -> research-only checker consumes bound group
evidence. Non-v3 inputs retain genuine native interpreter/PQ verification.

The only production hooks will be guarded by
`MERCATURA_MERCAWEAVE_RESEARCH_TEST_ONLY` in interpreter.h/.cpp. Only explicitly
named standalone research targets define that symbol. The ordinary interpreter
build will contain neither the new checker virtual method nor v3 dispatch; no
script flag, runtime switch, network setting or activation mechanism is added.
Separate normal and isolated binaries prevent differently compiled checker ABIs
from sharing a process. A control executable and build/symbol checks will verify
the inactive path. Normal production CMake will not reference the research target.

This is an uncached dispatch wrapper exercising actual Script and consensus
transaction/coin-view components. It does not claim execution of the node's
CheckInputScripts/CScriptCheck cache/lock/queue orchestration or ConnectBlock.
Those paths depend on node-wide state and will remain a reported limitation.
Private jobs will call the genuine VerifyScript directly with the bound checker.

The synthetic UTXO owner will serialize mutations, assign a checked view epoch,
and capture/validate coins while locked. Proof publication and job execution
must reject changed view epochs. Candidate blocks will use real layered coin
caches and commit only after the entire fixture succeeds. No daemon, chain
parameters, wallet, PSBT, mining or existing data directory will be touched.

An unactivated witness-v3 output must never be presented as protected MercaWeave
custody. Existing unknown-witness permissiveness is not group authorization;
policy discouragement alone does not provide custody protection.

Further implementation, measured results, limitations and final verdict follow
after validation. This architecture section records intent, not a test result.
