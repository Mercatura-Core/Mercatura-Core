# Activation and manual release decisions — founder authority

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

## Manual founder checklist

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

Source SHA, candidate filenames/checksums and final test references will be filled
from the completed engineering evidence. No actual current public height or final
activation parameter is supplied without an authorized observation and founder
approval.

## Reproducible local candidate preparation

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

## Release-note draft — not published

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

## Operator-notification draft — not sent

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
