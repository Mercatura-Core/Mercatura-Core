# Mercatura Wallet Management

Mercatura Core uses native witness-v2 ML-DSA-65 authorization for normal wallet
ownership.

Mercatura post-quantum wallet keys are separate from Bitcoin Core's classical
ECDSA, Schnorr, BIP32, xpub/xprv, and WIF ownership model.

## Creating a Wallet

Create a wallet with:

    mercatura-cli createwallet "wallet-01"

For authoritative arguments:

    mercatura-cli help createwallet

## Receiving Funds

Generate a normal Mercatura receive address with:

    mercatura-cli -rpcwallet="wallet-01" getnewaddress

Normal Mercatura receive and change addresses use the native post-quantum
witness-v2 output format.

Applications should obtain addresses from Mercatura Core instead of constructing
classical Bitcoin ownership addresses or descriptors.

## Wallet Balances

Inspect balances with:

    mercatura-cli -rpcwallet="wallet-01" getbalances

## Sending Funds

Create a normal wallet transaction with:

    mercatura-cli -rpcwallet="wallet-01" sendtoaddress "<mercatura-address>" <amount>

Mercatura Core handles normal coin selection, fees, post-quantum authorization,
and change creation.

Multi-output transactions and transaction batching remain supported.

## Wallet Encryption

Use built-in help for the wallet-encryption interfaces supported by the
Mercatura Core version being used:

    mercatura-cli help encryptwallet
    mercatura-cli help walletpassphrase
    mercatura-cli help walletpassphrasechange

Wallet encryption protects wallet secrets at rest. It does not make a
compromised or untrusted computer safe for signing.

## Backups and Recovery

Mercatura post-quantum wallet recovery state includes the authoritative PQ
wallet master seed together with the derivation and version metadata required
to reproduce wallet keys.

Treat wallet backups as security-sensitive secrets and keep protected copies.

Use built-in help for backup and restore operations:

    mercatura-cli help backupwallet
    mercatura-cli help restorewallet

Classical Bitcoin xpub/xprv or BIP32 data is not a substitute for backing up
Mercatura post-quantum wallet state.

## Default Wallet Locations

| Operating System | Wallet Directory |
| --- | --- |
| Linux | `$HOME/.mercatura/wallets/` |
| macOS | `$HOME/Library/Application Support/Mercatura/wallets/` |
| Windows | `%LOCALAPPDATA%\Mercatura\wallets\` |

The data directory may be changed with supported runtime options.

## PSBT

Mercatura retains PSBT workflows and integrates native ML-DSA-65 authorization
into wallet processing.

See [Partially Signed Transactions in Mercatura Core](psbt.md).

## Descriptor Compatibility

Mercatura retains inherited descriptor infrastructure, but classical Bitcoin
descriptor ownership paths are not normal Mercatura ownership authorization.

See [Output Descriptors in Mercatura Core](descriptors.md).

## Security

Do not disclose wallet vulnerabilities through public issues.

See the repository [Security Policy](/SECURITY.md) for private vulnerability
reporting instructions.
