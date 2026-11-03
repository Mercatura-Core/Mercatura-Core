# MercaWeave v0.1 — WO05 wallet and PSBT integration

Date: 2026-11-02, America/Los_Angeles. Research only. No network activation.

## Architecture recorded before implementation

Starting clean research HEAD: 93b356af34bd1ea60fd7c58a99533f3d0e93bc31.
Branch research/mercaweave-v0.1, /workspace/Mercatura-MercaWeave-v0.1.
Original /workspace/Mercatura-Core remains clean on work at
4b88b619fb1b08c7f30112a38eb471880b2a6af4. WO04 baseline: 17/17 pass.

Use a research-only wallet facade with one protected authoritative 32-byte PQ
seed, shared by native v2 and locked v3 derivation. Compile the actual existing
wallet.cpp seed-encryption functions through a narrowly guarded minimal build;
their bodies and normal-wallet compilation stay unchanged. Reuse CCrypter,
CMasterKey/MercaturaPQCryptedSeed, real coin-selection algorithms, actual PSBT
containers/parser/merge, and WO04 validation. No CWallet database, GUI or RPC is
instantiated. New metadata belongs to a separate versioned encrypted research
snapshot, never an ordinary wallet schema. Expanded secret keys are ephemeral.

Generic PSBT storage supports MercaWeave proprietary input records without
changing production PSBT parsing. Research signing/finalization reconstructs
the canonical manifest and verifies against the authoritative synthetic view.
It does not call the normal global-Params PSBT finalizer for experimental groups.
This limitation and untested CWallet/mempool lifecycle paths must stay explicit.

All research APIs require compile-time test symbols and have no normal node,
wallet, Qt or RPC call path. Unactivated v3 outputs are not protected custody.
No Secure Vault, mining integration, activation or deployment work is included.
