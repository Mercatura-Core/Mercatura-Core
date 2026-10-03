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

Mercatura Core provides the primary node, GUI wallet, and command-line
interfaces.

On supported platforms, the main executables are:

- `mercatura-qt` — Mercatura Core graphical wallet and node interface
- `mercaturad` — Mercatura Core headless node
- `mercatura-cli` — command-line RPC client

Runtime options and available RPC commands can be inspected directly from the
executables using their built-in help commands.

## Building

The following documents contain platform-specific build information and
dependency notes:

- [Dependencies](dependencies.md)
- [macOS Build Notes](build-osx.md)
- [Unix Build Notes](build-unix.md)
- [Windows Build Notes](build-windows-msvc.md)
- [FreeBSD Build Notes](build-freebsd.md)
- [OpenBSD Build Notes](build-openbsd.md)
- [NetBSD Build Notes](build-netbsd.md)

## Development

The repository [README](/README.md) contains the primary Mercatura project
overview, development status, and contribution guidance.

Additional technical documentation includes:

- [Developer Notes](developer-notes.md)
- [Productivity Notes](productivity.md)
- [Release Process](release-process.md)
- [Translation Process](translation_process.md)
- [Translation Strings Policy](translation_strings_policy.md)
- [JSON-RPC Interface](JSON-RPC-interface.md)
- [Unauthenticated REST Interface](REST-interface.md)
- [Benchmarking](benchmarking.md)
- [Internal Design Documentation](design/)
- [Files and Data Directories](files.md)
- [Fuzz Testing](fuzzing.md)
- [I2P Support](i2p.md)
- [Tor Support](tor.md)
- [ZMQ](zmq.md)

Some inherited Bitcoin Core documentation remains in the repository where it is
technically relevant or provides useful upstream implementation history.
Mercatura-specific behavior takes precedence where the projects differ.

## Security

Do not disclose unresolved security vulnerabilities through public issues or
community channels.

See the repository [Security Policy](/SECURITY.md) for private vulnerability
reporting instructions.

## Bug Reports

Reproducible technical bugs may be reported through the Mercatura GitHub issue
tracker.

Before submitting a report, search existing issues and include relevant version,
platform, reproduction, configuration, and log information where appropriate.

## License

Distributed under the [MIT software license](/COPYING).