# P10: Qt, RPC and native mining integration

**Engineering implementation and controlled validation complete; exact-source CI green.** Source: `fab2b87c5ae5ff2ff3b536974c4e768f62030d16`.

## Public assurance semantics

The existing peer fields retain their meaning: `transport_protocol_type` identifies
`pq-v1`; `session_id` is public and changes per session; `pq_encrypted` means the
local authentication/confirmation gate completed; `pq_authenticated` means proof
of possession of the presented ML-DSA identity; `pq_identity_verified` additionally
means an independently configured expected pin matched. Unpinned successful peers
are encrypted and authenticated, with `pq_identity_verified=false`.
`pq_identity`, `pq_kem` and `pq_auth_algorithm` retain established-session semantics.
A fingerprint is a public identifier, not an operator endorsement.

New peer booleans `pq_failed` and `pq_pin_mismatch` describe a terminal endpoint
while it remains visible before normal removal. They never grant assurance.
Disconnect/failure clears identity, session and encryption/authentication status.
CNode statistics explicitly clear a reused session string when unavailable.

## Bounded local diagnostics

`getnetworkinfo.pqp2p` is an object containing:

| Field | Type / meaning |
|---|---|
| `chain`, `build`, `transport` | Strings: selected chain, full build ID, pq-v1 or classical |
| `network_discriminator` | Hex string: actual public 85-byte N; empty when PQ is off |
| `testnet_enforced_build`, `testnet_activation` | Build-profile boolean and pending founder-decision status |
| `required` | Boolean: all P2P connections require PQ |
| `listening` | Boolean: required PQ, P2P listening enabled, network active |
| `connected` | Integer: current endpoints whose local PQ encryption gate completed |
| `pending`, `queued`, `active` | Integers: admitted incomplete endpoints, queued crypto jobs, executing crypto jobs |
| `established_total` | Integer: cumulative completed local gates, including disconnected endpoints |
| `failures` | Object of integer counters: framing, authentication_or_crypto, pin, deadline, resource, packet |
| `known_addresses` | Integer: addrman entries, not independently verified operators |
| `discovery` | String: not_required, disabled, connected, no_authenticated_peers or no_addresses |

Snapshots span independently locked components and are not an atomic accounting
transaction. Failure counters count rejected operations, including socket/admission
rejections, not unique people or independently attributed attacks. Cancellation and
ordinary disconnect are not authentication failures. Coarse crypto-job failures
include errors that cannot safely be distinguished at this boundary. Log token
limits remain in force; counters do not create high-cardinality logs. No secret
keys, seeds, HKDF state, randomness or private identity material is exposed.

## Qt presentation and clearing

Previously Qt hid PQ session IDs behind a V2-only condition and lacked distinct
assurance labels. The existing peer details now show `PQ v1`, encryption state,
presented identity authentication, expected identity verification, public
fingerprint and public session ID. Long public values use read-only scrolling fields and full-value tooltips; copy preserves the exact identifier. Polling preserves an existing selection.

Successful unpinned peers say Established / Yes / Not configured. Pinned peers
say Verified only after completed authentication. Pending peers show no old public
values. Terminal failures say Unavailable; an observed failed pin says Mismatch.
Failed sockets may disappear before a UI polling interval; process-lifetime RPC
counters remain available after removal. There is no single green Secure label.
The standard RPC console remains available for aggregate operational diagnostics.

Tests use actual RPCConsole labels with injected public statistics and a real
in-memory PQ exchange exposed through the real PeerTableModel. They cover legacy,
pending, unpinned, pinned, failed pin, session refresh, disconnect, row removal,
shutdown and network-model clearing. A screenshot, when present, is a controlled
lab peer panel, not a public operator session.

## Scheduling investigation and mining

The baseline automatic script-verification budget used raw host hardware
concurrency. The miner already uses affinity/container/job-aware CPU detection.
The current host exposes three CPUs with a two-CPU quota. Raw hardware
concurrency is not a reliable quota budget; larger hosts are covered by a
separate synthetic 192-CPU/two-quota regression.
Automatic validation now uses the same existing effective CPU detector: a
simulated 192-host/two-quota fixture yields one background script worker. Explicit
positive `-par` values retain their requested behavior, subject to the existing
maximum. Leave-free and extreme integer requests are tested without allocating
threads or scratchpads. Consensus validation rules are unchanged.

This addresses budget selection; GUI responsiveness remains an observed runtime
property. B2's historical automatic-mode heartbeat failure and its passing `-par=1`
run remain evidence. Current old/new automatic-mode and explicit-budget measurements
must be reported separately. The existing one-second heartbeat/Stop/Close assertions
were not relaxed. Cloud results do not predict Ryzen 5 4600G performance.

On the same isolated Linux x86_64 host (three visible CPUs, cgroup quota two), verified B3 automatic mode failed the unchanged one-second heartbeat limit with a 1,925 ms maximum gap. The quota-aware automatic build passed the complete V2 native Qt/PQ qualification: maximum heartbeat 257 ms, Stop completion 26 ms, Close completion 108 ms, session destroyed, mature rewards and an actual wallet payment independently received and confirmed over PQ, reconnect while busy, restart, tip agreement and full verification. This is an observed pair of runs, not a statistical performance guarantee.

The passing automatic run reached peer height 459. Transaction: d6b944ad8b9292e75f53b0df4bb43988740ed4299edd739a35409a2055722fef; confirmation block: a35004c36b4ea2fe67a9d58c01ee5fce67aba113ebcc1820bef97902af025f66. Aggregate sampled peak RSS was 257,696 KiB across the two owned processes. The explicit -par=1 height-20 regtest transition separately passed with maximum heartbeat 301 ms and peer height 33; its ten-second run does not claim a mature funded payment.

Peer-panel tests pass for actual in-memory PQ peers and injected failure/verification states, including exact-value clipboard copy and selection persistence during polling. The complete Qt mining unit suite passed alone in 100.945 seconds. A preceding run overlapped block generation and failed two publication checks; that failure is retained and no assertion was weakened. Native Linux/macOS/Windows scope is reported per actual CI job, distinct from this Linux end-to-end measurement.

Native CPU tests use the integrated Qt miner and required PQ peers. Regtest V2
from genesis and the existing regtest height-20 transition are separate schedules.
Public testnet has no V2 activation configured at this checkpoint; no public
activation is simulated by changing its consensus parameters. No standalone miner,
Stratum expansion, GPU hardware result or mainnet activation is claimed.

## Final native CI scope

[Native GUI CI 40530559236](https://github.com/Mercatura-Core/Mercatura-Core/actions/runs/40530559236)
passed on the exact source above for Linux x86_64, Linux ARM64, Windows MSVC and
macOS ARM64. Each executed `test_peersecurity-qt` and `test_mining-qt` within the
seven selected CTests, the nine selected controlled functional variants, staged
offline GUI startup and required-profile runtime rejection of off. This is actual
native execution. The separate long Linux Qt/PQ payment and responsiveness run
above is the end-to-end measurement; it was not repeated as that same measurement
on every operating system. macOS Intel and Windows UCRT were cross-compiled only.
