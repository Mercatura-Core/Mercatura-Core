# Output Descriptors in Mercatura Core

Mercatura Core inherits substantial output-descriptor infrastructure from
Bitcoin Core. However, Mercatura's ownership and transaction-authorization
rules differ substantially from Bitcoin.

## Mercatura Ownership Model

Normal Mercatura ownership authorization uses the native witness-v2
post-quantum transaction path with ML-DSA-65.

Inherited classical signature authorization is not valid for normal Mercatura
ownership. This includes ordinary ownership based on:

- ECDSA
- Schnorr signatures
- P2PK
- P2PKH
- P2WPKH
- classical P2WSH multisig
- Taproot key-path authorization
- Tapscript signature authorization
- CHECKSIG, CHECKMULTISIG, and CHECKSIGADD ownership paths

Mercatura does not use BIP32, xpub/xprv, WIF, or public-child derivation for
post-quantum wallet keys.

## Descriptor Compatibility

The codebase retains inherited descriptor parsing and infrastructure where it
remains useful for compatibility, wallet internals, testing, or non-ownership
script functionality.

The presence of an inherited descriptor expression in the parser does **not**
mean that the corresponding classical Bitcoin ownership mechanism is valid for
normal Mercatura spends.

Software integrating with Mercatura should not assume Bitcoin Core descriptor
ownership behavior applies unchanged.

## Post-Quantum Wallets

Mercatura post-quantum wallets use a separate deterministic ML-DSA-65 key
derivation system based on a wallet master seed.

Post-quantum keys are intentionally separate from Bitcoin Core's classical
`CKey`, `CPubKey`, `CExtKey`, xpub/xprv, and BIP32 ownership model.

The exact public descriptor syntax for Mercatura post-quantum wallet keys is
not yet considered a stable public interface and may be documented separately
when finalized.

## Addresses

Normal Mercatura wallet receive and change addresses use the Mercatura native
post-quantum witness-v2 output format.

Applications should obtain addresses from Mercatura Core rather than attempting
to construct classical Bitcoin ownership descriptors for Mercatura funds.

## Developer Note

Some inherited tests and internal components still reference Bitcoin descriptor
terminology because Mercatura is derived from Bitcoin Core. These references
should not be interpreted as enabling classical ownership authorization on the
Mercatura network.
