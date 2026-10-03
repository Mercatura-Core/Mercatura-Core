# Mercatura Core Documentation

Mercatura Core is the reference full-node and wallet implementation for
Mercatura (MCA).

Mercatura is a proof-of-work cryptocurrency derived from Bitcoin Core and
adapted around Mercatura-specific consensus, mining, monetary, scaling, and
post-quantum authorization rules.

Mercatura is currently in pre-release development. Official release binaries
and finalized public-network launch parameters will be published after the
required validation and release process is complete.

## Running

The primary Mercatura Core executables are:

- `mercatura-qt` — graphical wallet and node interface
- `mercaturad` — headless full node
- `mercatura-cli` — command-line RPC client

Use the built-in help provided by these executables for authoritative runtime
options and RPC arguments.

## Building

The current primary reviewed build documentation is:

- [Unix / Linux Build Notes](build-unix.md)
- [Windows Build Notes](build-windows.md)
- [Windows MSVC Build Notes](build-windows-msvc.md)
- [macOS Build Notes](build-osx.md)
- [Dependencies](dependencies.md)

Additional inherited platform build documents remain in the repository for
upstream reference but have not all received the same Mercatura-specific
documentation review.

## Configuration and Runtime

- [`mercatura.conf` Configuration](mercatura-conf.md)
- [Files and Data Directories](files.md)
- [JSON-RPC Interface](JSON-RPC-interface.md)

## Wallet and Transactions

- [Mercatura Wallet Management](managing-wallets.md)
- [Output Descriptors in Mercatura Core](descriptors.md)
- [Partially Signed Transactions in Mercatura Core](psbt.md)

Normal Mercatura ownership uses native witness-v2 ML-DSA-65 authorization.

Classical Bitcoin ECDSA/Schnorr ownership documentation should not be assumed
to apply to Mercatura.

## Development

The repository [README](/README.md) contains the primary project overview,
development status, and consensus summary.

The source tree also contains inherited Bitcoin Core developer, design, test,
networking, and historical documentation. These materials remain useful for
upstream architecture and provenance, but Mercatura-specific behavior and
documentation take precedence wherever the projects differ.

## Release Status

See [Mercatura Core Pre-Release Status](release-notes.md).

Historical upstream release notes under `doc/release-notes/` are retained for
implementation history and are not Mercatura release announcements.

## Security

Do not disclose unresolved security vulnerabilities through public issues or
community channels.

See the repository [Security Policy](/SECURITY.md) for private vulnerability
reporting instructions.

## Bug Reports

Reproducible technical bugs may be reported through the Mercatura GitHub issue
tracker.

## License

Mercatura Core retains applicable upstream open-source licensing and attribution.
See the repository license files for details.
