# P11: discovery, deployment rehearsal and release readiness

**Engineering implementation, controlled validation and feasible development packaging complete; exact-source CI green.** Source: `fab2b87c5ae5ff2ff3b536974c4e768f62030d16`.
Live public-network cutover is **not authorized** by this engineering assignment.

## Discovery and bootstrap implementation

Required transport is selected independently of unauthenticated capability hints.
Experimental service bit 24 (`P2P_PQ_EXPERIMENTAL`) is an availability hint, not a
registered global assignment, authenticated identity or consensus authority.
Missing/stale/colliding hints never allow V1/BIP324 fallback. Existing addrman
network grouping, bounded selection loops, anchors and peer-role machinery remain
in place. Required mode retains the ten-minute failed-candidate retry delay after the inherited 30-selection exception. Existing second-resolution success/attempt bookkeeping allows the ordinary escape for previously successful peers, so small healthy networks can recover promptly. This is availability bookkeeping, not identity trust; a source admitted in the same timestamp second is subject to that resolution. Manual operator retries remain explicit.

Fresh datadirs need verified operator bootstrap information: current Mercatura
main/test DNS and fixed seed sets remain empty. No unverified address, invented
operator or live DNS change was introduced. Startup and RPC diagnostics expose
empty discovery. `-seednode` performs a PQ address-fetch connection; normal addrman
peers subsequently supply addresses. `-addnode` is an operator emergency path,
always PQ-required. It does not establish decentralized bootstrap by itself.

`contrib/seeds/mercatura-bootstrap.py` validates and renders an explicit operator
inventory offline. It requires the existing testnet genesis, separate operator
identifiers, bounded endpoint counts, full source claims, UTC verification claims,
and optional independently trusted public pins. It refuses unsafe syntax,
configuration injection, extra secret fields and replacement of an existing file.
It does not independently authenticate the inventory's claims. Inherited Bitcoin
seed filters/agents/heights are not Mercatura qualification tools.

## Controlled rehearsal and network conditions

The repeatable four-process functional rehearsal uses two bootstrap/address
sources, a mining/relay node and a fresh joining validator. Synthetic routable
addresses are mapped by a bounded SOCKS5 fixture exclusively to owned loopback
sockets. This exercises actual Core selection and distinct network groups without
public probes. Proxy delay is asymmetric 10/20 ms and configured bandwidth is
2 MiB/s; these are fixture settings, not measured Internet/Tor/NAT properties.
The fixture has 24 concurrent connection slots and two 64-KiB buffers per handler,
with finite connection deadlines and explicit thread/socket cleanup.

Default regtest uses V2 from genesis, real maturity blocks, a native PQ wallet
payment, block confirmation, exact tip agreement and verifychain. Bootstrap loss,
retained addrman restart, fresh sessions, planned isolation, most-work catchup and
server restart are exercised. POSIX servers use protected persistent networking
identities; Windows clients use supported ephemeral identities. The actual
public-testnet variant is genesis-only networking/discovery, never historical
replay or public-difficulty mining. Optional IPv6 is a distinct lab run.

The separate `mercatura_pq_capacity.py --pq` uses real ML-DSA witnesses and existing
large-block chunk relay, wallet recovery and reindex assertions. Native Qt CPU
qualification separately proves real hashing, reward maturity, native-funded
payment/receipt/confirmation, reconnect while busy, Stop/restart and clean close.
No block/transaction authorization, difficulty or capacity assertion is bypassed.

The retained four-process regtest rehearsal passed in 92.101 seconds (91.674 seconds scenario time). All nodes reached height 104 and tip 820a337e5938340fbe4ad2f33747f957840f35e1cd73603e5c0c64c837c0e54f. A real native PQ wallet payment credited 1.00 MCA and confirmed. Observed transaction relay was 18.378 seconds and subsequent block relay 1.090 seconds under the configured lab proxy; these include application scheduling and are not Internet latency estimates. The incompatible candidate received one observed attempt without a rapid retry in the test window. The source backoff condition, rather than a ten-minute observation, establishes the configured delay.

Sampled surviving processes had maximum VmHWM 123,416 KiB, descriptors 26 and threads 12. These are process samples after recovery, not continuous aggregate peaks across all restarted lifetimes. All owned node children and proxy workers joined. Separate actual-public-identity genesis-only discovery and IPv6 loopback variants passed; neither proves historical replay or external reachability. The registered runner also passed the new actual PQ-transport capacity/chunk variant in 209 seconds and regtest/public discovery in 89/11 seconds.

The actual published v0.1.2 asset was SHA256-verified against its GitHub release digest, then used in a four-node partial upgrade and coordinated rollback on preserved generated V1 regtest history. Old classical clients were rejected without disturbing the healthy new PQ backbone. Wallet credit, tip, chainwork and UTXO hashes agree after rollback, second upgrade, chainstate reindex, full reindex and verifychain(4,0) on all four nodes. The complete rehearsal passed in 645.355 seconds at height 103; all children were reaped. Its public-testnet identity variant passed in 13.223 seconds at genesis only. Asset digest agreement is not an independent signature certification.

The explicit tip-independent activation regression passed fresh-genesis joining, an offline height-2 datadir catching up to height 9, preserved established PQ sessions while a miner crossed a synthetic boundary, a competing most-work reorganization through that boundary, full reindex, equal UTXO hashes and full verification. The boundary value 4 is only a regression observation, never a proposed public activation height. Public activation mechanism/height remain pending founder approval.

Final complete-runner and exact-source CI outcomes, final package names/checksums, and platform distinctions must be read from the final evidence and release decision tables.

## Existing chain history and consensus

The public-testnet parameter class is byte-identical to published
`v0.1.2-testnet` (`631886dd5159614b628ced3fe4fa1e289ce19451`). Genesis remains
`b3fc707c9a89dc45d1cf6a837284b22ad2b6fd41b99af601079db9dcbeff6920`, magic
`d7dbeea4`, P2P port 27778, and address identity is unchanged. Heights 0–7,999
retain 1 MiB; height 8,000 activates 4 MiB, with the existing later absolute
schedule. Mainnet/regtest capacity rules are untouched. Public-testnet MercaHash
V2 activation is unset, so its existing V1 schedule remains unchanged.

No authorized disposable real historical public-testnet datadir was available.
Startup/restart on actual history, chainstate reindex, full reindex, chainwork,
UTXO hash, historical wallet restore and transaction-index compatibility remain
**open release gates**. A genesis-only test and a byte-identical parameter class
are not substitutes. Changes between the old release and B3 include prior regtest
V2 support and mining integration; compatibility must be checked on real data.

## Development artifacts and release boundaries

Artifacts use the distinct `0.1.2-p9p11-dev` channel and exact source provenance.
No next numeric release version, official tag, signing identity or release
publication was assigned. The packaging helper requires a clean matching source
checkpoint, records configuration and binary identity, checks file SHA256/ZIP CRC,
and creates a tracked-source Git archive. CI validates packages locally on disposable runners and performs no artifact upload. The founder builds/exports and publishes artifacts manually.
Native GUI builds are reconfigured with the public-testnet GUI restriction before
packaging. Staged Qt smoke tests launch fresh owned offline testnet data, check
enforced-qualification profile, required policy, pending activation/genesis/no peers, then stop and verify child exit.

Native build bundles and a macOS app bundle, where actually produced, are reported
separately. A developer bundle plus runner smoke does not prove a signed installer,
clean-machine dependencies, OS integration, wallet migration, uninstallation or
hardware qualification. Those release gates remain explicit until executed.

## Required operator inventory

Before approving cutover, obtain a reviewed inventory; none is invented here.
For every backbone/archive, independent validator, miner, explorer node, RPC
service, client/wallet population and seed operator, record:

- Owner and independent organization; host/endpoint and authorized access process.
- Role, IPv4/IPv6 reachability, network group/ASN and bootstrap dependencies.
- Current binary/version/consensus compatibility and intended exact candidate SHA.
- Height, best hash, chainwork, UTXO-set hash and archival/index configuration.
- Wallet backups, stopped consistent datadir/config backup locations and restore test.
- Persistent P2P identity/public trusted binding where supported; recovery custody.
- Agreed upgrade window, participant notice, health owner and rollback authority.

Require at least two independently operated, verified address sources and an
adequate backbone/peer topology. Test fresh joins when one source is down and
verify independent peers are learned afterward. DNS capability hints are not
identity bindings. A single operator's several addresses do not meet independence.
Windows nodes needing persistent pin-target identities remain unsuitable until a
qualified ACL/reparse/atomic-storage implementation exists; ordinary ephemeral
clients are supported. POSIX identity files are unencrypted and need protected
backup storage, not public diagnostic uploads.

## Coordinated cutover procedure — preparation only

1. Close the release/history/security gates below and obtain explicit live
   approval covering the inventory, exact artifacts, operator coordination and
   activation window. Confirm the prelaunch mainnet remains excluded.
2. Verify candidate checksums and full Git provenance independently on each
   target. Record old/new executable paths, service configuration and recovery
   authority. Test the exact old and new binaries against disposable historical
   snapshots, including indexes, wallets and both reindex paths.
3. Record chain height/hash/work and UTXO hash. Back up every wallet through the
   supported wallet backup procedure. Stop the owned service cleanly before
   taking a consistent datadir snapshot; never copy a live LevelDB directory as
   though it were a valid backup. Protect identity/config backups separately.
4. Confirm every required backbone, explorer, mining and community dependency is
   ready for a compulsory transport barrier. Upgrade the agreed initial backbone
   pair and third node only as part of the coordinated inventory-wide sequence.
   Do not strand unprepared independent participants through an improvised
   two-node-only cutover. Notify operators through their separately approved
   coordination process; this assignment sent no such notifications.
5. Configure unchanged public testnet and verified bootstrap/pin bindings. Do not
   use `pqp2p=off`, a classical compatibility exception or a different genesis.
   Verify required policy, truthful peer assurance, no classical establishment,
   normal height/hash/work convergence and independently operated reachability.
6. Upgrade the remaining agreed services/clients in the approved window. Verify
   wallet receipt, transaction/block relay, mining rewards, explorer synchronization,
   fresh datadir joins, loss of one bootstrap and recovery. Measure pending/queue/
   worker bounds, rejection rates, memory, descriptors and unexpected disconnects.
7. Retain old binaries and protected consistent backups through the observation
   period. Record actual deployment outcomes and any exclusions before declaring
   the public cutover complete. This report cannot declare deployment performed.

## Rollback procedure — no automatic data deletion

A rollback is an operator-coordinated binary/config change, not a PQ downgrade
inside this candidate. Mainnet and the enforcing testnet qualification profile have no runtime off switch. The default testnet development profile remains activation-pending.
Before rollback, confirm the older binary accepts already accepted blocks and
wallet/index formats; a consensus-incompatible old binary is not a recovery plan.

Under separately authorized coordination: stop affected owned services cleanly,
record the current chain state, preserve all original data and failure evidence,
restore the previously tested binary/config or an explicitly approved protected
snapshot, and reconnect the agreed rollback cohort. Verify genesis, best hash,
chainwork, UTXO hash, wallets, indexes and peer reachability. Never wipe a datadir,
reset genesis, silently discard history or blindly open a newer database with an
older binary. Restore the prior networking identity only from a protected trusted
backup; no automatic pin replacement is allowed. Notify participants before a
rollback through their approved process and record post-recovery convergence.

## Independent review tracker and approval gates

| Gate | Required evidence | Status |
|---|---|---|
| A: controlled engineering | Code, isolated regressions and exact source CI | Complete with the explicit scope limits in this report |
| B: release qualification | Native artifacts/installers, real history, diverse bootstrap, rollback rehearsal | Open: history/infrastructure/installation qualification unavailable |
| C: cryptographic security | Independent custom-AKE review and resolution of deployment-relevant critical/high findings | Open: no independent review completed |
| D: live network approval | Maintainer approval of exact candidate and coordinated operator plan | Not authorized |

The independent review scope includes the two-contribution ML-KEM combiner,
authenticated exchange, transcript/role/network binding, mutual confirmation,
trust/pin model, session separation, forward-secrecy/erasure limits, cipher/rekey
state, required-mode downgrade resistance and implementation assumptions. Use the
B3 reviewer package plus the P9–P11 patch; implementing-agent tests do not satisfy
independence. No formal proof or independent cryptographic certification is claimed.

**Public compulsory cutover: NO-GO until the open release/security gates and
explicit deployment approval are resolved.** Mainnet remains prelaunch. Engineering
completion, candidate qualification, independent security approval and live
deployment authorization are separate statuses.

## Final exact-source CI and local attempt ledger

Qualified source: `fab2b87c5ae5ff2ff3b536974c4e768f62030d16`, tree
`8740466a844f323129e7455685524e32a673ca37`.
[Main CI 40530559182](https://github.com/Mercatura-Core/Mercatura-Core/actions/runs/40530559182)
and [native GUI CI 40530559236](https://github.com/Mercatura-Core/Mercatura-Core/actions/runs/40530559236)
both completed successfully. All 11 required jobs plus the main meta job passed.
Final repository lint, report links/whitespace, evidence consistency and retained
candidate SHA256/ZIP CRC checks also passed before documentation publication.

| Source CI job | Result | Executed scope |
|---|---|---|
| Linux native x86_64 headless | PASS | 186 CTests; all 56 functional variants |
| Windows native x86_64 MSVC | PASS | 186 CTests; all 56 functional variants; repeated six-worker turnover regression |
| macOS native ARM64 | PASS | 186 CTests; all 56 functional variants |
| Windows x86_64 UCRT cross | PASS | Cross-compilation; no native execution claim |
| macOS Intel x86_64 cross | PASS | Cross-compilation; Intel runtime unqualified |
| macOS ARM64 cross | PASS | Cross-compilation, separate from native jobs |
| Lint | PASS | Required repository lint |
| Native PQ GUI: Linux x86_64 | PASS | Seven CTests, nine functional variants, helper regressions, published-release migration, local package/Qt smoke and enforcing runtime |
| Native PQ GUI: Linux ARM64 | PASS | Seven CTests, nine functional variants, helper regressions, local package/Qt smoke and enforcing runtime |
| Native PQ GUI: Windows MSVC | PASS | Same selected native scope; ephemeral identities, persistent mode fails closed |
| Native PQ GUI: macOS ARM64 | PASS | Same selected native scope; POSIX identities; macOS app packaging |

The original local 56-variant attempt passed 53 and failed three fixtures: the
classical mainnet witness injector, an unbuilt local `bitcoin-util`, and an obsolete
strict RPC message expectation. Each correction passed its full targeted rerun;
the final source subsequently passed all 56 together on each of the three main
native platforms. Earlier failed attempts and superseded/cancelled CI runs remain
in the evidence ledger. The MSVC bridge initialization failure was fixed without
altering production code or weakening an assertion.

Final local enforcing-profile checks passed required public-testnet connections
(17.402 seconds), public-identity automatic discovery (12.192 seconds), IPv6
loopback discovery (8.110 seconds), development packaging/Qt smoke (8.473 seconds)
and owned temporary CMake install/uninstall staging (2.682 seconds). The installed
prefix was removed; the test-only bridge was neither packaged nor installed.
See the [manual release decision package](mercatura-pq-activation-and-manual-release-decisions.md)
for exact artifact names, hashes and build-host limitations.

Successful owned fixtures stop/reap their children and join proxy workers. The
new failure-path helper also retains node handles before framework cleanup clears
the node list; its four unit cases and actual intentional framework-failure probe
pass. Earlier failed development attempts left PID-1-adopted, zero-RSS zombie
entries on this container. They are not live nodes and cannot be reaped by this
process. No claim is made that every historical failed attempt exited cleanly.

## Completion statuses and milestones

| Status | Final result |
|---|---|
| Engineering complete | Yes: implemented code and controlled qualification, with explicit scope limits |
| Security review complete | No: independent custom-AKE review remains open |
| Release candidate qualified | No for public deployment: real historical replay, independent bootstrap and final installer gates remain open |
| Activation parameters finalized | No: founder decision pending |
| Release manually authorized | No |
| Public-testnet deployment authorized | No |

| Milestone | Outcome |
|---|---|
| P9.0 | Complete: B3 publication and baseline CI verified; authority and activation boundaries recorded |
| P9.1 | Complete: selected-chain discriminator and immutable required policy; public testnet default pending |
| P9.2 | Complete in audited/common factory and controlled runtime paths; actual external Tor/I2P/NAT qualification open |
| P9.3 | Complete with explicit platform limits: POSIX persistent/pins, Windows ephemeral; Windows persistence unsupported |
| P10.1 | Complete: truthful peer assurance and bounded aggregate RPC diagnostics |
| P10.2 | Complete in native CI and Linux end-to-end Qt/PQ mining; founder hardware not measured |
| P11.1 | Complete in controlled automatic discovery/diversity/proxy/bootstrap tests; independent live operators open |
| P11.2 | Complete in isolated multi-node/fresh/lagging/partition/reorg/reindex/upgrade/rollback rehearsal; real public history open |
| P11.3 | Complete feasible development preparation: local Linux bundle/source/checksums/staging and native CI packages; final installers open |
| P11.4 | Complete: exact-source green CI, reports, evidence and founder decision package; publication receipt external |

Prepared source, measured local candidate and documentation are reviewable. The
founder still controls the official version/tag/date/signing/publication, PQ
mechanism/window/height if any, separate public MercaHash V2 height, participating
operator inventory and actual deployment. Capacity height 8,000 is unchanged.
No official release/tag, live service/DNS change or mainnet launch was performed.
