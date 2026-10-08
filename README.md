# Mercatura Core (MCA)

Mercatura is a Bitcoin-family, pure proof-of-work cryptocurrency focused on a simple miner-driven consensus model, CPU-oriented proof of work, native post-quantum transaction ownership, and conservative full-node operation.

Mercatura Core is derived from the modern Bitcoin Core codebase and is being adapted into an independent network with its own consensus rules, monetary policy, network identity, wallet behavior, mining algorithm, and test infrastructure.

> **Project status:** Mercatura public testnet is live. Mainnet has not launched. The v0.1 software and protocol remain under active public-testnet validation, and mainnet launch-sensitive values may still change before release.

## Protocol Overview

| Area | Mercatura |
| --- | --- |
| Project | Mercatura |
| Ticker | `MCA` |
| Consensus | Pure Proof of Work |
| Proof of Work | MercaHash V1 |
| Target block time | 150 seconds |
| Difficulty adjustment | DGWv3 |
| DGW lookback | 24 blocks |
| Block reward distribution | 100% to PoW miners |
| Treasury | None |
| Governance rewards | None |
| Masternodes | None |
| Proof of Stake | None |
| Native transaction authorization | ML-DSA-65 post-quantum signatures |
| Native PQ output | Witness v2 Mercatura PQ |
| Monetary precision | 2 decimal places |
| Base unit | 1 MCA = 100 base units |
| Minimum fee policy | 0.01 MCA per started 1,000 bytes |
| Recommended dust threshold | 0.02 MCA |
| Initial effective block cap | 1 MiB |
| Scheduled block-cap growth | Doubles approximately every 5 years |
| Maximum scheduled block cap | 1024 MiB |
| Witness discount | None |
| Premine | None |
| Founder allocation | None |
| Consensus developer fee | None |

## Design Goals

Mercatura is intentionally narrow at the consensus layer.

The initial network uses ordinary proof-of-work chain selection without:

- Proof of Stake
- validator committees
- masternodes
- treasury voting
- governance superblocks
- protocol-enforced developer rewards
- inherited Bitcoin historical chain assumptions

The project aims to preserve mature Bitcoin Core networking, mempool, pruning, wallet, descriptor, PSBT, RPC, and node-security behavior where practical while replacing Bitcoin-specific consensus and network assumptions with Mercatura-specific rules.

Major Mercatura-specific components include:

- **MercaHash V1**, a memory-intensive CPU-oriented proof-of-work algorithm
- **DGWv3 difficulty adjustment**
- **native ML-DSA-65 post-quantum authorization**
- **two-decimal monetary accounting**
- **adaptive emission**
- **Mercatura-specific block-cap growth**
- **independent network and address identity**
- **Mercatura-specific genesis blocks**
- **no SegWit witness discount for block-cap accounting**

## MercaHash V1

MercaHash V1 is Mercatura's native proof-of-work function.

Current consensus parameters include:

- 128 MiB private mutable scratchpad per worker
- 64-byte scratchpad lines
- 2,097,152 scratchpad lines
- 4 KiB / 64-line binding blocks
- 32,768 binding blocks
- 1 binding pass
- 131,072 main mixing steps
- secondary scratchpad touch every 16 steps
- full-state cross-lane mixing every 64 steps
- SHA3-512 checkpoint every 1,024 steps
- 256 final scratchpad reads
- SHA3-256 finalization

MercaHash is consensus-critical. Independent implementations must reproduce Mercatura Core behavior exactly.

The standalone Mercatura CPU miner is maintained separately as:

**MercaMiner**

https://github.com/Mercatura-Core/MercaMiner

## Difficulty Adjustment

Mercatura uses **Dark Gravity Wave v3 (DGWv3)**.

Current parameters:

- target block spacing: **150 seconds**
- historical lookback: **24 blocks**
- target timespan: **3,600 seconds**
- minimum timespan clamp: **1,200 seconds**
- maximum timespan clamp: **10,800 seconds**
- retargeting occurs every block after the startup period

The launch target remains unchanged during the initial startup window before DGW begins calculating from historical chain data.

The public testnet is live and is being exercised with real MercaHash mining.

Final mainnet launch parameters will be set and validated immediately before mainnet release.

## Monetary Policy

Mercatura does not use Bitcoin's fixed halving schedule.

The current design uses a deterministic bootstrap issuance phase followed by the **SP-LT Adaptive Emission** system.

The adaptive controller is designed to respond gradually to long-term proof-of-work conditions while remaining fully deterministic across nodes.

Consensus monetary calculations use:

- deterministic integer arithmetic
- explicitly defined rounding behavior
- no floating-point consensus calculations
- overflow-safe intermediate arithmetic
- deterministic cross-platform behavior

Block rewards are paid entirely to miners.

Mercatura has no:

- treasury reward
- masternode reward
- staking reward
- founder reward
- protocol-enforced developer fee

More detailed monetary-policy documentation will be published as launch documentation is finalized.

## Post-Quantum Transaction Authorization

Mercatura integrates native **ML-DSA-65** transaction authorization.

The current post-quantum authorization model uses:

- native witness-v2 Mercatura PQ outputs
- 32-byte public-key commitments
- ML-DSA-65 signatures
- ML-DSA-65 public keys
- SHA3-384 authorization hashing
- deterministic wallet key derivation
- fresh cryptographically secure randomness for production signatures
- explicit versioning of PQ output and authorization formats

Mercatura's current PQ spend format uses a native witness-v2 output and an authorization witness containing the ML-DSA-65 signature and public key.

Normal Mercatura wallet ownership does not rely on classical ECDSA or Schnorr ownership paths.

Mercatura retains modern Bitcoin Core transaction infrastructure such as:

- multi-output transactions
- `sendmany`
- PSBT workflows
- wallet coin selection
- funded transaction creation
- transaction batching

The post-quantum integration is designed to coexist with those established transaction mechanisms rather than replace them.

Mercatura's PQ implementation should be understood as project-specific cryptographic engineering. It is not a claim that the system is immune to every possible future cryptographic or quantum attack.

## PQ Wallet Derivation

Mercatura PQ wallet keys are deliberately separate from Bitcoin's secp256k1 key hierarchy.

The wallet uses:

- a private PQ master seed
- HKDF-SHA3-384
- deterministic ML-DSA-65 child-seed derivation
- separate external/receive and internal/change branches
- network separation in derivation
- encrypted storage of authoritative PQ recovery material

Mercatura does not use fake BIP32, xpub, or xprv structures for PQ ownership.

## Block Capacity

Mercatura retains Bitcoin Core's block-weight framework where practical but removes the SegWit witness discount.

Witness and non-witness bytes therefore count equally for Mercatura block-cap accounting.

The current scheduled consensus ceiling is:

- **1 MiB at launch**
- doubles every **1,051,200 blocks**
- approximately one doubling every **5 years** at 150-second block spacing
- maximum scheduled cap of **1024 MiB**

The growth schedule is intended to follow long-term improvements in ordinary node bandwidth, storage, propagation capacity, and home-node viability.

This is a consensus ceiling, not a requirement that miners fill blocks to the maximum size.

## Fees and Monetary Units

Mercatura uses two decimal places.

```text
1 MCA = 100 base units
```

Current v0.1 minimum fee policy:

```text
0.01 MCA per started 1,000 bytes
```

Recommended dust threshold:

```text
0.02 MCA
```

Fee and monetary calculations use deterministic integer base-unit arithmetic.

## Network Identity

Mercatura uses independent network identities so Mercatura nodes cannot be confused with Bitcoin or another upstream network.

### Ports

| Network | P2P | RPC |
| --- | ---: | ---: |
| Mainnet | 27777 | 27776 |
| Testnet | 27778 | 27775 |
| Signet | 27779 | 27774 |
| Regtest | 27780 | 27773 |

### Bech32 HRPs

| Network | HRP |
| --- | --- |
| Mainnet | `mca` |
| Testnet | `tmca` |
| Regtest | `mcrt` |

Mercatura's inherited legacy Base58 P2PKH encoding uses the `M` prefix where
that encoding is exposed for compatibility. Classical P2PKH is not a normal
Mercatura ownership path.

Normal Mercatura wallet receive and change addresses use the native PQ
witness-v2 path.

### Network Message Start

Mercatura uses independent message-start bytes for each supported network.

This prevents Mercatura nodes from accidentally connecting to or interpreting traffic from Bitcoin or another Bitcoin-family network.

## Node Identity

Mercatura uses its own application names:

```text
Daemon:      mercaturad
CLI:         mercatura-cli
Qt wallet:   mercatura-qt
Config file: mercatura.conf
```

Mercatura also uses its own data directory and network-specific subdirectories.

## Public Testnet Connectivity

Mercatura's public testnet is live.

Current bootstrap peer:

```text
node1.mercaturacore.com:27778
```

A persistent `mercatura.conf` configuration can use:

```ini
testnet=1

[test]
addnode=node1.mercaturacore.com:27778
```

DNS seeds and fixed seeds remain intentionally limited during the early public-testnet period. Additional independent peers can be added with `addnode` as the network grows.

Public testnet resources:

- Website: https://mercaturacore.com
- Explorer: https://explorer.mercaturacore.com/?network=testnet
- Mercatura Core release: https://github.com/Mercatura-Core/Mercatura-Core/releases/tag/v0.1.0-testnet1
- MercaMiner release: https://github.com/Mercatura-Core/MercaMiner/releases/tag/v0.1.0-testnet2

**Testnet MCA is for testing only and has no monetary value.**

## Software Components

The Mercatura ecosystem currently consists of several related components.

### Mercatura Core

Mercatura Core provides:

- consensus validation
- full-node networking
- blockchain storage
- wallet functionality
- JSON-RPC
- command-line tools
- mining RPC support
- Qt wallet functionality

### MercaMiner

MercaMiner is the standalone Mercatura CPU miner.

It communicates with Mercatura Core using:

- `getblocktemplate`
- `submitblock`

MercaMiner includes:

- MercaHash V1
- multithreaded mining
- network selection
- RPC cookie authentication
- configuration-file support
- automatic RPC discovery
- payout-address configuration
- periodic hashrate reporting
- accepted/stale/rejected block reporting
- benchmark functionality

Repository:

https://github.com/Mercatura-Core/MercaMiner

### Mercatura Qt

Mercatura Core includes the Mercatura-branded Qt wallet while retaining the mature Bitcoin Core Qt application architecture where practical.

The wallet supports Mercatura-specific behavior including:

- Mercatura network identity
- MCA units
- two-decimal display
- native PQ receive addresses
- PQ sends
- transaction history
- Mercatura fee handling
- coin control
- wallet encryption
- wallet backup
- address-book functionality
- PSBT workflows

Linux, Windows, and macOS are treated as supported Mercatura Core GUI targets.

### Block Explorer

The official Mercatura Explorer is live at:

https://explorer.mercaturacore.com

Public testnet:

https://explorer.mercaturacore.com/?network=testnet

The Explorer uses Mercatura Core RPC together with indexed blockchain data and Mercatura-specific analytics.

Mainnet and testnet explorer data are kept separate.

## Current Development Status

Mercatura is in its **v0.1 pre-mainnet development cycle**, with the **public testnet now live**.

Core consensus, MercaHash V1, DGWv3, adaptive emission, native ML-DSA-65 transaction authorization, PQ wallet functionality, Mercatura Qt branding, standalone CPU mining, and public Explorer infrastructure have reached the public-testnet stage.

Current work focuses on:

- extended public-testnet operation and soak testing
- multi-node and mining validation
- cross-platform testing
- documentation and release hardening
- final mainnet parameter selection
- final mainnet genesis generation and validation

**Mercatura mainnet has not launched.**

**Testnet MCA is for testing only and has no monetary value.**

Development status should not be interpreted as an independent security audit or production-readiness certification. Public testing and external review remain important before mainnet launch.

## Building From Source

Mercatura Core uses CMake and requires a C++20-capable toolchain.

Example Linux build:

```bash
git clone https://github.com/Mercatura-Core/Mercatura-Core.git
cd Mercatura-Core

cmake -S . -B build
cmake --build build -j4
```

Dependency requirements vary by platform and enabled components.

The repository's `doc/` directory and inherited upstream build documentation should be consulted for platform-specific dependencies until fully Mercatura-specific platform documentation is completed.

## Running Unit Tests

After building:

```bash
ctest --test-dir build --output-on-failure
```

The main C++ unit-test executable can also be run directly:

```bash
build/bin/test_bitcoin
```

## Running Functional Tests

After a successful configured build:

```bash
python3 test/functional/test_runner.py
```

Individual functional tests may also be run separately using the generated build configuration.

## Running Mercatura Core

Start the daemon:

```bash
build/bin/mercaturad -daemon
```

Query the node:

```bash
build/bin/mercatura-cli getblockchaininfo
```

Stop the node cleanly:

```bash
build/bin/mercatura-cli stop
```

## Running Testnet

Start a public-testnet node with the current bootstrap peer:

```bash
build/bin/mercaturad -testnet -addnode=node1.mercaturacore.com:27778 -daemon
```

Query the node:

```bash
build/bin/mercatura-cli -testnet getblockchaininfo
```

For persistent configuration, add the bootstrap peer to `mercatura.conf`:

```ini
testnet=1

[test]
addnode=node1.mercaturacore.com:27778
```

The public-testnet P2P port is `27778`.

RPC should normally remain bound to localhost unless the operator has a specific secured deployment requirement.

## Mining

Mercatura Core exposes the mining RPC interfaces required by external miners.

For normal CPU mining, use MercaMiner:

https://github.com/Mercatura-Core/MercaMiner

MercaMiner obtains candidate blocks through:

```text
getblocktemplate
```

and submits solved blocks through:

```text
submitblock
```

MercaHash V1 requires approximately **128 MiB of private scratchpad memory per mining worker**.

As a result, practical miner configuration depends on both CPU characteristics and available memory bandwidth.

## Wallet

Mercatura retains modern Bitcoin Core wallet infrastructure where practical, including:

- descriptor-wallet infrastructure
- coin selection
- PSBT support
- encrypted wallets
- wallet backups
- transaction batching
- pruning compatibility
- modern wallet database behavior

Mercatura adds its own native PQ key storage, derivation, authorization, and ownership mechanisms.

Back up wallet data before testing development builds.

## Pruning and Indexes

Ordinary Mercatura nodes may operate in pruned mode.

Optional indexes include functionality inherited from modern Bitcoin Core such as:

- transaction indexing
- block filter indexing
- coin statistics indexing

Explorer and infrastructure nodes may require archival storage and additional indexes.

Normal node participation does not require every optional index.

## Consensus Development Rules

Consensus-sensitive Mercatura code is expected to follow strict implementation principles.

These include:

- deterministic integer arithmetic
- no consensus floating point
- explicit serialization behavior
- fixed rounding rules
- overflow-safe arithmetic
- cross-platform reproducibility
- permanent test vectors for critical cryptographic behavior
- focused regression tests for consensus changes

Consensus rules should not depend on compiler-specific, operating-system-specific, or architecture-specific behavior.

## Security

Mercatura is currently **pre-mainnet software**.

Do not use development builds to protect assets of significant value.

Consensus rules, wallet code, cryptographic integration, networking, mining behavior, recovery procedures, and upgrade paths require continued testing before production deployment.

Security-sensitive findings should be reported privately to project maintainers when public disclosure could put users or networks at risk.

General bugs and non-sensitive development issues may be reported through the GitHub issue tracker.

The project does not claim that Mercatura-specific consensus or cryptographic components have received an independent third-party security audit unless such an audit is explicitly published.

## Contributing

Development contributions are welcome.

For consensus-sensitive changes:

- keep changes narrowly scoped and reviewable
- preserve deterministic behavior
- avoid floating-point consensus arithmetic
- include focused regression tests
- add or update permanent vectors when necessary
- run relevant unit tests
- run relevant functional tests
- keep unrelated changes out of consensus patches
- avoid changing inherited upstream behavior without a Mercatura-specific reason

Large consensus or architectural changes should normally be discussed before implementation.

## Repository and Branch Status

Mercatura is currently in the `v0.1` pre-mainnet development cycle.

The public testnet is active, but development parameters may still change before the first mainnet release.

The repository's stable and development branches may differ while work is underway. Mainnet launch parameters should be considered final only when explicitly published as part of the mainnet launch release.

## Mainnet Status

**Mercatura mainnet has not yet launched.**

The current mainnet chain parameters include development placeholders that will be replaced or finalized during launch preparation.

Before mainnet launch, the project will perform a final launch-parameter pass covering items such as:

- final mainnet starting difficulty
- final mainnet genesis timestamp
- final genesis nonce
- final genesis block hash
- final genesis MercaHash
- dependent test vectors
- MercaMiner mainnet network identity
- seed infrastructure
- public explorer configuration
- release binaries
- final launch validation

Do not treat pre-launch development genesis values as permanent mainnet launch parameters.

## License

Mercatura Core is distributed under the MIT software license.

See:

```text
COPYING
```

for details.

Mercatura incorporates and derives from upstream open-source software, including Bitcoin Core and additional cryptographic components, which remain subject to their respective licenses and attribution requirements.

## Acknowledgements

Mercatura builds on years of open-source engineering from the Bitcoin Core community and other upstream projects used by the codebase.

Mercatura-specific work includes its:

- network identity
- MercaHash proof-of-work algorithm
- DGW configuration
- adaptive monetary policy
- post-quantum transaction authorization
- PQ wallet architecture
- block-cap policy
- fee and monetary model
- mining software
- test infrastructure

The project acknowledges and retains appropriate attribution for upstream open-source work.

---

**Mercatura public testnet is live. Mainnet has not launched.**
