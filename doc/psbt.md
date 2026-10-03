# Partially Signed Transactions in Mercatura Core

Mercatura Core retains Bitcoin Core's Partially Signed Transaction (PSBT)
workflow while integrating Mercatura's native post-quantum authorization.

PSBT support allows transaction construction, funding, signing, inspection,
finalization, and broadcast to remain separated where appropriate.

## Mercatura Post-Quantum Authorization

Normal Mercatura ownership uses native witness-v2 ML-DSA-65 authorization.

Mercatura PSBT processing carries the post-quantum information required to
complete these spends rather than relying on classical ECDSA or Schnorr
ownership signatures.

Mercatura-specific proprietary PSBT fields use the identifier:

`Mercatura`

The currently implemented fields include:

- subtype `1`: the exact 1,952-byte ML-DSA-65 public key
- subtype `2`: the exact 3,309-byte ML-DSA-65 signature

These fields support Mercatura's native post-quantum wallet signing and
finalization workflow.

## RPC Workflow

Mercatura preserves the standard PSBT-oriented RPC workflow inherited from
Bitcoin Core where applicable, including transaction funding, wallet
processing, decoding, and finalization.

Typical RPCs include:

- `walletcreatefundedpsbt`
- `walletprocesspsbt`
- `decodepsbt`
- `finalizepsbt`

Use the built-in RPC help for the authoritative arguments and return values for
the version of Mercatura Core being used.

## Transaction Batching

Mercatura retains normal multi-output transaction support and Bitcoin Core's
existing transaction-batching model.

PSBT integration does not replace or restrict:

- multi-output transactions
- `sendmany`
- funded transaction creation
- normal wallet coin selection
- exchange-style withdrawal batching

## Classical Signatures

Inherited PSBT structures may contain fields originating from Bitcoin Core's
classical signing model.

Their presence in inherited serialization or parsing code does not mean that
ECDSA, Schnorr, Taproot key-path signatures, or classical multisig are valid
normal Mercatura ownership authorization.

## Security

A PSBT should be reviewed before signing, particularly the destination outputs,
amounts, fees, and inputs being authorized.

Security-sensitive PSBT or wallet vulnerabilities should be reported through
the private process described in `/SECURITY.md`.
