# P9: compulsory PQ network and platform qualification

**Engineering implementation and controlled validation complete; exact-source CI green.** This report covers
engineering work, not independent cryptographic approval or deployment.

## Baseline and unchanged boundaries

Branch: `integration/pq-p2p-p9-p11-required-testnet`.
B3 publication: `f46bfa2527feac3302c6c3a02010f5670d880fd3`; qualified B3 source:
`db84cc3ac3bcd71b7f6cf913c7eef695e407873d`. B3 publication CI `40459461581`
was freshly checked: completed success, all seven required jobs plus meta green.
Qualified P9–P11 source: `fab2b87c5ae5ff2ff3b536974c4e768f62030d16`. Documentation publication: see external receipt.

The Candidate 1 wire format, five-message exchange, signatures, key schedule,
confirmation lengths, packet encoding and rekeys are unchanged. The P9 handoff
expands the earlier isolated Stage A execution policy; it does not constitute an
independent AKE review. Consensus, genesis, magic, ports, prefixes, DGW, emission,
wallet derivation, PQ witness authorization and capacity schedules are unchanged.

## Effective network policy

| Selected chain | Default | Explicit `off` | Explicit `required` |
|---|---|---|---|
| Mercatura mainnet | Required PQ | Startup error | Required PQ |
| Public testnet, default pending build | Off | Classical development behavior | Isolated required qualification |
| Public testnet, enforcing qualification build | Required PQ | Startup error | Required PQ |
| Regtest | Off | Classical test behavior | Required PQ |
| Signet | Existing classical behavior | Existing behavior | Startup error |
| Inherited testnet4 | Existing CLI restrictions preserved | Existing restrictions | Rejected; no activation |

`MERCATURA_REQUIRE_PQ_TESTNET` defaults OFF. ON is a separate immutable qualification build profile, not an approved activation mechanism or release. The final enforcing profile has no runtime off switch. Public activation mechanism, height, date and release remain pending founder approval. No height or clock trigger is introduced.

`RequiredMode` is shared by startup selection and tested independently. Explicit
`-v2transport=1` contradicts required mode and fails; disabling BIP324 does not
disable PQ. RPC requests explicitly asking for BIP324 fail under required mode.
A false legacy preference is overridden by the required transport at the common
CNode factory. `setnetworkactive` toggles activity, never chain policy.

`CConnman::Start` rejects missing compulsory or wrong-network contexts before
binding a socket. Mainnet policy is tested in memory, through pre-bind rejection and through the existing isolated mainnet-parameter witness regression. Its Python message injector uses a test-only loopback bridge built around the actual authenticated PQ adapter; all malformed-witness, compact-block, retry and reindex assertions remain intact. The helper accepts no public destination, is never installed or packaged, and creates no production transport exception. No public mainnet launch or deployment was performed.
Development builds identify themselves as `0.1.2-p9p11-dev` plus Git suffix.
The public-testnet GUI package selects testnet and rejects other networks. Daemon
operators must explicitly select `-testnet`; mainnet remains prelaunch.

## Network discriminator

`NetworkForChain` takes selected local chain parameters. N remains exactly:
`chain_id[1] || magic[4] || genesis_raw[32] || SHA3-384(challenge)[48]`.
Only signet hashes its raw challenge; all other chains use SHA3-384(empty).
No peer field selects a local chain. Both hellos retain the existing exact N gate.
Regtest and public-testnet full vectors and hashes are in the evidence JSON.
Tests cover exact frozen regtest preservation, actual testnet identity, all five
chain IDs, magic/genesis separation, same-magic custom signet challenge separation,
and all 25 chain-pair exchanges. Only five same-chain pairs authenticate.

## Connection coverage and downgrade resistance

Every production CNode is created through ConnectNode or accepted-socket creation.
Both pass the same immutable per-Connman PQ context into the common factory.
The factory chooses PQ before consulting the inherited V2 boolean. Capability
hints, source address, proxy type and connection priority cannot weaken it.
`PQTransport::ShouldReconnectV1()` remains false; Connman also refuses to enqueue
a V1 retry while its PQ context exists. No classical parser receives PQ sockets.

| Origin | Shared implementation / verification boundary |
|---|---|
| Automatic full relay and restored addrman | Addrman selection -> OpenNetworkConnection -> ConnectNode |
| Block relay, extra block relay and anchors | Same path and factory; anchor service hints do not select transport |
| `-connect`, `-addnode`, RPC addnode/onetry | Specified/added queues -> common ConnectNode; exact destination pins |
| Feeler and address fetch / seednode | Same PQ factory; completed short-lived gates counted separately from live peers |
| Extra network-specific outbound | Existing diversity selection -> same ConnectNode |
| Private broadcast | Proxy override -> same ConnectNode; no classical exemption |
| Inbound IPv4/IPv6, Tor and I2P adapters | Accepted-socket construction -> same context and PQ responder |
| IBD, missing blocks and established relay | Existing peer roles use these same sockets; no new transport path |
| Reconnect after failure | Required context retained; no V1 retry admission |

The permanent selector unit exercises all seven ConnectionType values, both
legacy preference booleans and four combinations of missing/V2/PQ service hints.
Actual loopback tests cover manual/configured connections, full relay, block
relay, address fetch, feeler, seed discovery, reconnect and isolation. The P11
SOCKS lab exercises real proxy connection construction without a Tor service.
Actual Tor, I2P, external NAT and independent operators remain separate gates;
a shared factory test is not native runtime certification of those environments.

## Admission, identity and storage

Two executing crypto workers, 32 queued jobs, 64 pending endpoints, four pending
per known source, 16 anonymous endpoints, the 256-entry source table and all
B1/B2 deadlines/token rules remain unchanged. Public status adds fixed-size
counters and public N only. No unbounded per-peer diagnostics were added.
Established sessions are not counted as pending handshake allocations.

On POSIX, explicit persistent networking identities remain in the selected
network datadir, with owner-only directory/file checks, no-follow opens, exclusive
creation, no-clobber publication, validated reload and explicit separate-name
rotation. Files are unencrypted and assume a trusted local user/filesystem and
ancestors. Per-network N binds storage and fingerprint interpretation. These
identities do not share wallet, transaction, mining reward or RPC keys.

Windows persistence remains intentionally unsupported and fails closed. This
assignment does not claim qualification of Windows owner ACLs, reparse handling,
atomic publication or crash recovery. Ephemeral Windows operation remains the
supported default. Stable pinned servers require a qualified persistent platform.

## Results, defects and limitations

Local checks passed: policy/transport/identity/assurance unit suites; 61 normal B1 boundary cases plus two separately selected capacity cases; ASan/UBSan/LSan and TSan versions of both groups; independent primitive and packet references. The disabled benchmark is not counted as an executed test. Tests cover both pending and enforcing policy values, all 25 chain pairs and 56 connection type/preference/hint combinations.

The required-mode public testnet fixture verified the default pending profile and isolated opt-in. The enforcing-profile default/off rejection passed on the final local candidate and all four native GUI CI platforms. Wrong pins, corrected pins, persistent POSIX restart, incompatible classical endpoints and fresh reconnect sessions pass in owned fixtures. Windows persistence remains explicitly unsupported; Windows native results must be read from the final CI matrix.

The original full local runner encountered a classical mainnet witness injector, an omitted offline utility target, and an obsolete expected RPC diagnostic. Targeted reruns pass after the test bridge, building bitcoin-util, and updating the strict -8 diagnostic expectation. No consensus assertion, cryptographic vector, runtime required policy or deadline was relaxed. Final exact-source CI and package results are recorded in the evidence JSON and P11 report.

The initial signet expected-value test accidentally selected Bitcoin's generic
Hash overload; it was corrected to an independent SHA3-384 constant. A new test
fixture initially used an obsolete NetGroupManager constructor and was corrected
to NoAsmap. These are development test errors, not protocol failures. Initial
builds overlapped source development; qualification uses a stable dependent-object
rebuild and an exact clean source CI checkpoint. All failed attempts are retained.

No custom AKE security certification is claimed. Windows persistence, actual
historical public-testnet replay and external network/operator qualification are
explicit open boundaries. See P11 gates and the B3 independent-review package.

## Final source and platform qualification

All seven required jobs plus the meta job passed in [source CI 40530559182](https://github.com/Mercatura-Core/Mercatura-Core/actions/runs/40530559182).
Linux x86_64, native Windows MSVC and native macOS ARM64 each passed 186 CTest
entries and all 56 registered Mercatura functional variants on the source above.
Windows UCRT, macOS Intel and macOS ARM64 cross jobs passed compilation; those
cross jobs do not establish native runtime coverage.

All four additional jobs passed in [native GUI CI 40530559236](https://github.com/Mercatura-Core/Mercatura-Core/actions/runs/40530559236):
Linux x86_64/ARM64, Windows MSVC and macOS ARM64. Each executed the seven selected
PQ/identity/policy/Qt CTests, nine controlled functional variants, offline helper
regressions, staged native GUI smoke and immutable testnet-enforcing runtime
checks. These seven selected CTests are not a claim that the entire GUI-enabled
CTest catalog ran. Linux x86_64 also repeated the published-release migration.

The first MSVC build of the new test bridge rejected a platform-specific
`in_addr` aggregate initialization. The final one-line portability fix uses the
already initialized `sockaddr_in.sin_addr`; both final native Windows jobs pass.
No production transport or consensus exception was introduced.
