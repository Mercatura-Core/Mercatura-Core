# Mercatura Core (MCA)

Mercatura is a Bitcoin-family, pure proof-of-work cryptocurrency focused on a miner-driven consensus model, memory-hard proof of work, native post-quantum transaction ownership, authenticated post-quantum peer connections, and conservative full-node operation.

Mercatura Core is derived from the modern Bitcoin Core codebase and is being adapted into an independent network with its own consensus rules, monetary policy, network identity, wallet behavior, mining algorithm, and test infrastructure.

> **Project status:** Mercatura public testnet is live. **[Mercatura Core v0.1.3-testnet](https://github.com/Mercatura-Core/Mercatura-Core/releases/tag/v0.1.3-testnet)** is the current public-testnet release. Mainnet has not launched; development parameters remain subject to validation.

## Public Testnet v0.1.3 — Upgrade Required

**[Download Mercatura Core v0.1.3-testnet](https://github.com/Mercatura-Core/Mercatura-Core/releases/tag/v0.1.3-testnet)** — Linux x86-64, Windows x64 (installer or portable ZIP), and macOS ARM64. Download `SHA256SUMS` from the same release and verify each package before use.

- **Mandatory post-quantum networking:** All upgraded public-testnet nodes use authenticated, encrypted **PQ-v1** P2P transport (ML-KEM-768 key establishment and ML-DSA-65 peer authentication). Classical and BIP324 fallback are not accepted. Older nodes cannot connect until upgraded.
- **Live interoperability:** v0.1.3 Windows Qt and Linux testnet nodes have established authenticated encrypted PQ-v1 sessions. This is a limited public-testnet observation, not a claim of a comprehensive independent security audit.
- **MercaHash V2 activation:** **Public-testnet block height 17,000** is the first V2 block; height 16,999 is the final V1 block. Upgrade before the transition. The historic chain is unchanged.
- **Current testnet capacity:** 4 MiB effective block cap since height 8,000. The separate 1 MiB-to-1024 MiB scheduled capacity policy below describes the planned initial mainnet baseline.
- **Built-in mining:** Mercatura Core Qt includes optional native CPU solo mining, with experimental GPU support on compatible builds and devices. The Mining tab uses the existing wallet/full node, pays rewards to a wallet-owned post-quantum address, and has no mining software or developer fee. Physical GPU qualification is still limited.

Public-testnet software is experimental. Testnet MCA has **no monetary value**. Back up your wallet before upgrading.

## Protocol Overview

| Area | Mercatura |
| --- | --- |
| Project | Mercatura |
| Ticker | `MCA` |
| Consensus | Pure Proof of Work |
| Proof of Work | MercaHash V1 through testnet height 16,999; MercaHash V2 from height 17,000 |
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
| Initial planned mainnet effective block cap | 1 MiB |
| Public-testnet effective block cap | 4 MiB from block 8,000 |
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

- **MercaHash V1**, with an approved public-testnet transition to V2 at height 17,000
- **DGWv3 difficulty adjustment**
- **native ML-DSA-65 post-quantum authorization**
- **two-decimal monetary accounting**
- **adaptive emission**
- **Mercatura-specific block-cap growth**
- **independent network and address identity**
- **Mercatura-specific genesis blocks**
- **no SegWit witness discount for block-cap accounting**

## MercaHash Proof of Work

Mercatura's existing public-testnet history uses MercaHash V1 up to and including block 16,999. **MercaHash V2 activates from block 17,000**, with no genesis reset or difficulty reset. The updated node verifies historical blocks under their respective algorithms.

MercaHash V1 consensus parameters include:

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

MercaHash V2 uses a **16 MiB private scratchpad per worker** and version-aware native CPU/CUDA/OpenCL mining paths. The exact consensus implementation and permanent vectors are authoritative; do not substitute approximate independent implementations. See [MercaHash V2 testnet activation](doc/mining/MERCAHASH-V2-TESTNET-17000.md) and [native mining guidance](doc/native-cpu-mining.md) for details.

MercaHash is consensus-critical. Independent implementations must reproduce Mercatura Core behavior exactly.

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

The planned **initial mainnet** capacity schedule is:

- **1 MiB at launch**
- doubles every **1,051,200 blocks**
- approximately one doubling every **5 years** at 150-second block spacing
- maximum scheduled cap of **1024 MiB**

The growth schedule is intended to follow long-term improvements in ordinary node bandwidth, storage, propagation capacity, and home-node viability.

**Current public testnet:** The effective block cap increased to **4 MiB at block 8,000**. The planned initial mainnet schedule above should not be mistaken for the current testnet ceiling.

A consensus ceiling is not a requirement that miners fill blocks to the maximum size.

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

Mercatura's public testnet is live. **v0.1.3+ PQ-v1 transport is required from startup.** Older pre-upgrade peers cannot connect; no classical/BIP324 fallback is available. The peer software versions and PQ status can be inspected using `getpeerinfo`.

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
- Mercatura Core v0.1.3-testnet downloads: https://github.com/Mercatura-Core/Mercatura-Core/releases/tag/v0.1.3-testnet
- Release checksums: https://github.com/Mercatura-Core/Mercatura-Core/releases/download/v0.1.3-testnet/SHA256SUMS

**Testnet MCA is for testing only and has no monetary value.**

## Software Components

The Mercatura ecosystem includes the Core node and wallet plus its public Explorer.

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
- built-in wallet-owned CPU solo mining and optional experimental GPU mining

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
- optional native solo mining within a dedicated Mining tab, with CPU worker controls and live statistics
- automatic selection of a native PQ mining reward address owned by the wallet
- experimental NVIDIA CUDA / AMD OpenCL mining where supported by the build and hardware

Release downloads are provided for **Windows x64, Linux x86-64, and macOS ARM64**. macOS CPU mining is supported; the macOS application is not currently Apple-notarized. GPU modes have not been fully qualified on physical hardware.

### Block Explorer

The official Mercatura Explorer is live at:

https://explorer.mercaturacore.com

Public testnet:

https://explorer.mercaturacore.com/?network=testnet

The Explorer uses Mercatura Core RPC together with indexed blockchain data and Mercatura-specific analytics.

Mainnet and testnet explorer data are kept separate.

## Current Development Status

Mercatura is in its **v0.1 pre-mainnet development cycle**, with the **public testnet now live**.

Core consensus, MercaHash V1 and scheduled testnet V2, DGWv3, adaptive emission, native ML-DSA-65 transaction authorization, mandatory PQ-v1 P2P networking, integrated Qt mining, and public Explorer infrastructure have reached the public-testnet stage.

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

**Use v0.1.3 or later for PQ-v1 network compatibility**, and back up wallet data before upgrading. Public-testnet Qt release binaries are locked to testnet; mainnet has not launched.

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

## Mining With Mercatura Core Qt

**Native solo mining is integrated directly into the Mercatura Core Qt wallet.** There is no separate miner to install and no need to run a second blockchain node.

1. Install the latest [Mercatura Core public-testnet release](https://github.com/Mercatura-Core/Mercatura-Core/releases/tag/v0.1.3-testnet) and back up your wallet.
2. Launch Mercatura Core Qt on public testnet; allow the blockchain to synchronize.
3. Create or open your own native post-quantum wallet.
4. Open the **Mining** tab and choose **CPU** (recommended for initial testing), set an appropriate worker count, and select **Start Mining**.
5. Monitor the session's hashrate, worker activity, and accepted block statistics. Use **Stop Mining** to end the session cleanly.

Mining rewards are directed to a wallet-owned native PQ address that the Mining tab creates or reuses. There are **no software mining fees, developer fees, or automatic donation deductions**. Normal network transaction fees remain separate.

**CPU and memory:** MercaHash V1 uses a private **128 MiB** scratchpad per worker; MercaHash V2 uses **16 MiB** per worker. The active proof-of-work version changes automatically at public-testnet height **17,000**, and resource limits vary by version. Increasing CPU worker count does not necessarily increase performance proportionally.

**GPU (experimental):** Supported Windows/Linux builds may offer NVIDIA CUDA and AMD OpenCL options, including combined CPU + GPU mining and device selection. Availability depends on the build and compatible hardware. Physical GPU performance and compatibility have not been comprehensively qualified. macOS ARM64 is a CPU-mining release.

Nodes and miners should upgrade to v0.1.3 **before block 17,000**; clients using older consensus rules cannot mine or validate the V2 chain after activation. The node must also use mandatory PQ-v1 P2P on public testnet.

For advanced integrations, Mercatura Core retains `getblocktemplate` and `submitblock` RPC interfaces for compatible mining software.

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
- mainnet native mining and wallet network-identity verification
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
- native Qt mining integration
- test infrastructure

The project acknowledges and retains appropriate attribution for upstream open-source work.

---

**Mercatura public testnet is live. Mainnet has not launched.**
