# Activation and manual release decisions — founder authority

## Current founder-approved policy

- **MercaHash V2 public-testnet activation: block 17,000 — FOUNDER APPROVED.**
  Height 16,999 is the last V1 block; height 17,000 and later require V2.
- **Public-testnet PQ transport: COMPULSORY FROM NODE STARTUP — FOUNDER APPROVED.**
  **PQ transport has no block-height activation.** Genesis sync, reindex, reorg,
  offline recovery and network activity toggles do not weaken that policy.
- Mainnet V2-from-genesis implementation is **DEFERRED** until the founder has
  observed real public-testnet V2 operation for several days after activation.
  Mainnet genesis, PoW settings and required-PQ policy are unchanged here.
- Release publication and live deployment are **NOT YET AUTHORIZED**. No official
  version/tag is assigned. Founder review and integration, exact-source release
  qualification, packages and operator coordination remain separate steps.

Ordinary binaries require public-testnet PQ even when the deprecated compatibility
CMake option MERCATURA_REQUIRE_PQ_TESTNET is OFF. Omitting -pqp2p or specifying
-pqp2p=required works; -pqp2p=off is rejected. No V1/BIP324 fallback follows failed
authentication. The five-message handshake and existing identity/pin safeguards
are unchanged. Mainnet remains compulsory, regtest opt-in, signet/testnet4 unchanged.

getnetworkinfo.pqp2p.required reports actual local enforcement. The legacy
testnet_enforced_build field is retained as an always-true compatibility alias for
this binary's public-testnet policy. testnet_activation is required-from-startup,
describing approved software policy, not completed live-network deployment.
Authentication and expected-identity verification remain distinct peer properties.

The independent AKE-review waiver remains an accepted experimental-testnet risk;
it is not completed independent certification. Formal two-independent-operator
bootstrap qualification remains deferred. At least one publicly reachable PQ
bootstrap and a fresh-node connection/synchronization check are still required
before cutover. The existing offline inventory renderer's 2–16-operator schema
is unchanged and does not prove public reachability or operator independence.

See the current [pre-release qualification](../validation/MERCATURA-TESTNET-17000-PQ-REQUIRED-PRE-RELEASE.md)
for exact commits, measured results and limitations. The three upstream security
fixes and subsequent portability repairs remain in the branch history.

## Historical engineering study — before the final approvals

All remaining material in this appendix is the earlier source-scoped decision
study and evidence. Its pending decisions, optional profiles, version suggestions
and uncompleted history gates describe that checkpoint; they do not override the
approved policy above. Test results and their original source references are
preserved rather than represented as fresh validation of the activation branch.

This is a development decision package, not release or deployment authorization.
See [activation options](mercatura-pq-public-testnet-activation-options.md).

| Decision | Status | Authority |
|---|---|---|
| Public-testnet 4 MiB height | Fixed at 8,000; unchanged | Existing approved consensus |
| Public-testnet MercaHash V2 height | Pending; not configured in this branch | Founder |
| Public-testnet compulsory PQ activation mechanism | Pending; coordinated required release recommended for evaluation | Founder |
| Public-testnet compulsory PQ cutover height, if applicable | Pending; none selected | Founder |
| Official release version | Pending; a next testnet RC version may be proposed, never assigned by this agent | Founder |
| Official release tag | Not created | Founder |
| Official release publication | Not authorized | Founder |
| Live testnet deployment | Not authorized | Founder |
| Mainnet required-PQ policy | Designed for first connection at launch; prelaunch deployment excluded | Founder after qualification |
| Mainnet MercaHash V2/genesis/release parameters | Separate future approval; no consensus edits here | Founder |

The development label `0.1.2-p9p11-dev` identifies engineering builds only and does
not assign the next official release. A possible `v0.1.3-testnet-rc1` is a suggestion
for founder consideration after release-history review; no such tag/release exists
from this work and no version decision is finalized.

The pending testnet profile must remain the default while activation is undecided.
An enforcing qualification profile demonstrates the intended invariant without
becoming an approved release or triggering live network changes. Its artifacts
must be labelled development/qualification candidates. Green CI does not authorize
publication, signing, merging, download-link changes, operator announcements,
DNS/seed updates, service restarts or network cutover.

### Manual founder checklist

1. Review the exact source SHA, source diff, vectors and measured CI/runtime
   evidence; resolve or explicitly classify outstanding platform/history gates.
2. Obtain and evaluate independent cryptographic review. Implementing-agent tests
   cannot close that gate. Resolve deployment-relevant critical/high issues through
   the approved security process.
3. Supply actual current testnet operator observations and approve the activation
   mechanism/window, any coordination height, and the separate MercaHash V2 plan.
4. Verify independent bootstrap operators, software availability, historical-data
   compatibility, wallet/index recovery, protected backups and rollback authority.
5. Choose the official release version and final candidate configuration. Build
   and inspect native packages; verify checksums, metadata, installers, signing,
   wallet migration and clean-machine install/uninstall where applicable.
6. Manually create/sign the approved tag, publish the release/assets and official
   checksums, update approved download links and send the approved operator notice.
   No automation in this assignment may combine green CI with artifact publication.
7. Separately authorize live deployment and the coordinated cutover plan. Record
   actual outcomes after operators execute it; development CI is not deployment.

The engineering source, local candidate and successful exact-source CI references are listed below. No actual current public height or final
activation parameter is supplied without an authorized observation and founder
approval.

### Reproducible local candidate preparation

For the approved development source checkpoint, use a separate build directory
and the repository's platform build prerequisites/toolchain. Representative
native Unix configuration (add the appropriate Qt prefix where needed):

```sh
cmake -S . -B build-pq-candidate -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_GUI=ON -DBUILD_GUI_TESTS=ON -DBUILD_TESTS=ON -DENABLE_WALLET=ON \
  -DENABLE_IPC=OFF -DWITH_DBUS=OFF -DWITH_ZMQ=OFF -DWITH_QRENCODE=OFF \
  -DMERCATURA_PUBLIC_TESTNET_RELEASE=ON -DMERCATURA_REQUIRE_PQ_TESTNET=ON
cmake --build build-pq-candidate -j2 --target bitcoind bitcoin-cli bitcoin-qt
python3 contrib/devtools/package-pq-candidate.py --build-dir build-pq-candidate \
  --output-dir NEW-LOCAL-CANDIDATE-DIRECTORY --smoke
```

Use clean exact source; the helper rejects dirty/mismatched binary provenance.
Windows uses the existing native MSVC preset/vcpkg and its Qt platform plugin
path; follow the native qualification workflow's explicit commands. Retain
BUILD-PROVENANCE.json, SMOKE.json, SHA256SUMS and the exact source archive locally.
The development ZIP has binaries/native dependencies available beside the build;
Linux/macOS system-library dependencies are not made portable by ZIP packaging.
CI has no artifact upload or publication step. Native installer, clean-machine
install/uninstall, signing and final release-format checks remain separate gates.
The helper does not use project signing credentials or create Git tags/releases.

### Release-note draft — not published

Mercatura Core compulsory-PQ candidate adds chain-bound authenticated PQ P2P,
required-mode downgrade resistance, explicit identity pins, truthful RPC/Qt peer
assurance, bounded networking diagnostics, quota-aware automatic validation
scheduling and controlled PQ bootstrap/recovery tests. Wallet transaction
cryptography and consensus remain unchanged. The 4 MiB capacity activation stays
at public-testnet height 8,000; public MercaHash V2 activation is separately pending.

Default development testnet builds remain activation-pending. The proposed
coordinated enforcing release would require PQ on every ordinary connection;
older classical-only nodes would not interoperate. No production PQ height/date
or version is selected here. Windows uses ephemeral networking identities;
persistent Windows storage remains intentionally unsupported. Independently
trusted pins are distinct from authentication of a presented public key. Custom
AKE review and actual historical/platform release qualification remain required.

### Operator-notification draft — not sent

Subject: proposed coordinated Mercatura public-testnet PQ upgrade — decisions pending

The founder is evaluating a compulsory-PQ public-testnet upgrade. This draft does
not announce an activation or authorize an upgrade. The approved notice must
supply the official version/source SHA, verified package/checksum locations,
mechanism/window (and coordination height only if explicitly chosen), participating
operator inventory, independent bootstrap/pin bindings, protected backup steps,
compatibility warning, observation owners and tested coordinated rollback plan.

Classical-only clients will be unable to connect to enforcing peers. Fresh and
lagging clients must obtain the approved software and verified bootstrap before
cutover. Existing blockchain/genesis and capacity height 8,000 remain unchanged;
MercaHash V2 is a separate pending decision. Operators should confirm readiness,
platform support and recovery custody through the separately approved channel.
No date, height, download URL, fingerprint or operator is invented in this draft.

### Local development candidate — not an official release

Source: `fab2b87c5ae5ff2ff3b536974c4e768f62030d16`. Development channel:
`0.1.2-p9p11-dev`; qualification flags `MERCATURA_REQUIRE_PQ_TESTNET=ON` and
`MERCATURA_PUBLIC_TESTNET_RELEASE=ON`. Source defaults remain activation-pending.

| Local artifact | Bytes | SHA256 |
|---|---:|---|
| Mercatura-Core-p9p11-dev-fab2b87c5ae5-linux-x86_64.zip | 21,536,278 | cee6d47558fca1e34ad111dad190b7bde758d3175396069f22a5a7a7c338df9d |
| Mercatura-Core-p9p11-dev-fab2b87c5ae5-source.tar | 59,822,080 | 11bc0e9cbbc697d2023ad7848947933e34b0ab68b39ca5c80ba6ba87093a9583 |

BUILD-PROVENANCE.json records GCC 14.2, Release/O2 configuration and individual
binary hashes. SHA256/ZIP CRC checks, staged offline Qt startup, native CLI/daemon
version checks and an owned temporary CMake install/uninstall prefix passed. The
smoke checks required/enforcing policy, pending activation, unchanged genesis,
zero peers/listeners and clean process exit. This native Linux build depends on
its host libraries; it is not a portable or signed clean-machine installer.

The test-only injector bridge is absent from the binary bundle and installation.
CI packages stay local to disposable runners and are not automatically exported.
These checksums are development evidence, not published official release checksums.
The founder must choose the final version/configuration, rebuild, review/sign and
manually publish the approved artifacts after the open gates are resolved.

### Final development qualification references

[Source CI 40530559182](https://github.com/Mercatura-Core/Mercatura-Core/actions/runs/40530559182)
passed all seven required jobs plus meta; Linux x86_64, native MSVC and macOS
ARM64 each passed 186 CTests and 56 functional variants. Cross builds passed for
Windows UCRT and both macOS architectures. [Native GUI CI 40530559236](https://github.com/Mercatura-Core/Mercatura-Core/actions/runs/40530559236)
passed all four platforms, including Linux ARM64, with the selected seven CTests,
nine functional variants, staged package smoke and enforcing-profile checks.
The P11 report gives the complete matrix and failed-attempt ledger.

CI packages were built and validated on disposable runners without artifact
upload or retention. They are not downloadable deliverables. The Linux candidate
listed above is retained locally. Package checksums vary by native toolchain;
the Windows source TAR also has different exported bytes from Unix exports.
A controlled `git -c core.autocrlf=true archive` reproduction exactly matched
the Windows TAR checksum, confirming the platform line-ending export difference.
Use each export's recorded hash and Git commit/tree identity; this work does not
claim bit-identical cross-platform archives or reproducible binary builds.

Engineering is complete within the controlled scope. Independent security review,
public-deployment candidate qualification, final activation parameters, manual
release authorization and live deployment authorization remain separate and open.
The public-review archive and documentation publication SHA have an external
checksum/remote-verification receipt to avoid a self-referential Git commit/hash.
