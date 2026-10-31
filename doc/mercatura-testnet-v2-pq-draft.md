# Mercatura public-testnet upgrade — technical release-notes draft

Version-neutral development draft for founder review. No official release number,
tag, signing, binary publication or deployment is assigned or authorized here.

## MercaHash V2 activation

Existing public testnet requires MercaHash V2 beginning at block 17,000;16,999 is
the last V1 block. Historical V1 remains valid. Candidate-height selection follows
validated ancestry, including historical headers, templates and reorgs. No genesis
replacement, difficulty reset, signaling vote, new checkpoint or public consensus
override is introduced. DGWv3,150-second spacing, adaptive emission, chainwork,
transaction authorization and the capacity activation at 8,000/growth are unchanged.
Mainnet V2-from-genesis is deferred until several days of observed testnet V2
operation and separate founder authorization.

## Native CPU, CUDA and OpenCL mining

The integrated candidate retains native solo mining, automatic/manual CPU workers,
safe resource budgets, responsive cancellation, version-aware worker replacement
and CPU/GPU combined scheduling. V1 uses128 MiB and V2 uses16 MiB private scratchpads.
It includes the existing NVIDIA CUDA V1/V2 path and specialized AMD OpenCL V2
implementation. This activation changes scheduling through the existing selector;
it does not alter either algorithm, frozen vectors or GPU kernels. Physical GPU
performance is qualified only where actual device measurements exist.

## Compulsory quantum-resistant P2P

Ordinary public-testnet nodes require PQ from their first connection. Omitting
-pqp2p uses required mode; -pqp2p=required remains accepted; -pqp2p=off is rejected.
The deprecated MERCATURA_REQUIRE_PQ_TESTNET option cannot turn enforcement off.
There is no PQ block-height activation, classical/BIP324 fallback or downgrade
after authentication/pin failure. Fresh genesis sync, old datadirs, reindex,
offline recovery and reorgs preserve the same transport policy. Mainnet remains
required; regtest opt-in and signet/testnet4 behavior are preserved.

ML-KEM-768, ML-DSA-65, SHA3-384 transcript/derivation, encrypted packets, mutual
confirmation, exact-destination pins, resource quotas and cancellation are
unchanged. Encryption/authentication of a presented identity does not prove an
independently trusted expected identity; pinned verification is reported separately.

## Three Bitcoin Core security corrections

The security branch retains CVE-2026-117350 receive-backpressure event filtering,
CVE-2026-117349 continuous paused-send timeouts and CVE-2026-117346 wide block/undo
positions and file operations, plus their portability regression repairs. Existing
VARINT field encoding is retained. Old readers cannot safely reopen newly persisted
offsets outside their 32-bit range; a type change does not repair pre-existing undo
corruption. Record-size bounds,32-bit environments and filesystem limits retain
their separately documented constraints.

## Wallet and Qt responsiveness

Native one-click solo mining and existing responsive Start/Stop/Close behavior
remain. Reward ownership uses native ML-DSA-65 destinations, with 0% software mining
fees. Wallet derivation, ownership, persistence, encryption and witness-v2 rules
are unchanged. This work does not redesign the wallet or qualify unsupplied real
historical wallet files.

## Discovery and diagnostics

Existing PQ bootstrap/discovery, restart and identity/pin diagnostics are retained.
getnetworkinfo.pqp2p.required describes actual local policy; testnet_activation is
required-from-startup. The deprecated testnet_enforced_build boolean now indicates
unconditional testnet policy regardless of the old CMake option, not completed
network deployment. Peer encryption, authentication, verified pin, fingerprint
and session identifiers remain separate observations. Handshake work is bounded
by the existing two workers,32 queued and 64 pending limits.

## Upgrade and coordination requirements

The PQ software cutover and height 17,000 PoW transition are independent. Older
classical-only nodes cannot communicate normally with upgraded nodes. The founder
must arrange compatible binaries, explorer/backbone/miner/client readiness,
verified bootstrap addresses/pins, protected backups, tested recovery and operator
acknowledgments before a coordinated cutover. All block producers must upgrade
before 17,000. A successful build does not establish public connectivity or rollout.

## Qualification limitations

Use the [exact-source qualification report](validation/MERCATURA-TESTNET-17000-PQ-REQUIRED-PRE-RELEASE.md)
for actual unit/functional/Qt results, native CI status, historical-database scope,
reported chain tip and missing physical GPU evidence. Pending CI is not a pass;
compilation and mocked host tests do not establish native device performance.
Independent external AKE review remains waived with accepted experimental-testnet
risk; formal independent-operator qualification remains deferred. Neither is a
technical pass or a claim of external cryptographic certification. Public bootstrap
reachability, actual operator upgrades, packages and live deployment remain manual
qualification/authorization gates.

## Consensus-aware rollback limit

After block 17,000, a V1-only release cannot follow the upgraded public-testnet
chain. A rollback must retain V2 consensus support or use an explicitly approved
coordinated recovery plan; backups do not make an obsolete consensus implementation
compatible. Before or after that boundary, transport rollback also requires an
agreed compatible peer cohort. No automatic classical fallback is available.
