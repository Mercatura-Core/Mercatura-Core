# PQ operator security guide

This is development documentation. Public-testnet PQ activation mechanism,
height/window and official release are pending founder approval. Default builds
retain pending testnet policy. Enforcing qualification builds use
`MERCATURA_REQUIRE_PQ_TESTNET=ON` and reject runtime off. An explicit isolated
`-pqp2p=required` connection is never allowed to fall back to V1 or BIP324.
Mainnet required-from-first-connection architecture is implemented; launch is
outside this assignment.

## Read assurance accurately

`pq_encrypted` means the local encrypted authentication/confirmation gate completed.
`pq_authenticated` proves possession of the presented ML-DSA networking identity.
`pq_identity_verified` additionally means an operator-configured expected pin
matched. An unpinned authenticated peer is not independently trusted. A service
bit, DNS answer, address-relay message, peer count or public fingerprint alone is
not an operator endorsement. Qt shows these properties separately.

Public fingerprints (`mcapq1:` plus 96 lowercase hex digits) and session IDs may
be inspected or copied from the peer panel. Session IDs must change on reconnect.
A fingerprint can be stable and correlatable; do not confuse it with a wallet
address, reward destination or transaction authorization key.

## Persistent networking identities and pins

POSIX persistent operation is explicit: `-pqp2pidentity=NAME` selects a simple
filename under the selected network datadir. Add `-pqp2pidentitycreate=1` only for
exclusive first creation; remove it on reload. Existing files are never silently
replaced. Directory/file ownership, restrictive permissions and no-follow rules
must pass. The file is unencrypted: trust the local user/filesystem and ancestors,
protect backups and never upload it with diagnostics. Do not move identity files
between chains. Default operation creates fresh ephemeral identities per connection.

Windows ephemeral operation is supported. Persistent Windows storage intentionally
fails closed until ACL, reparse, atomic publication and crash-recovery behavior
are securely implemented and independently testable. Do not weaken checks or use
ad hoc seed-file workarounds. Use a qualified persistent platform for stable pinned
server identities.

Configure expected pins only after independently authenticating the destination
and fingerprint. Syntax is `-pqp2ppin=HOST:PORT=mcapq1:FINGERPRINT_HEX`; bracket IPv6
hosts. Pins bind exact outbound destination strings including port, not arbitrary
aliases or inbound operator names. There is no TOFU or automatic replacement.
Hostname case, aliases, alternate IPv6 spellings and different port spellings
do not inherit another destination's pin. An unmatched destination can still
authenticate as an ordinary unpinned peer; it must report
`pq_identity_verified=false`. For every intended pinned bootstrap connection,
check `getpeerinfo` for the exact destination and `pq_identity_verified=true`;
`pq_authenticated=true` alone does not establish the configured operator identity.
A mismatch terminates the endpoint. Independently confirm planned rotations,
create a separately named identity, coordinate affected pins and retain protected
recovery custody. Never disable pinning merely to suppress an unexplained mismatch.

## Bootstrap and diagnostics

Use independently verified, current operator inventories. The offline helper
`contrib/seeds/mercatura-bootstrap.py` validates bounded syntax and renders a new
configuration exclusively; it does not probe endpoints, authenticate operators
or prove independence. It requests required mode explicitly. Its `--lab` output
is restricted to numeric loopback destinations. No official seeds were populated.

`getnetworkinfo.pqp2p` exposes selected chain/build/profile, effective policy,
public network discriminator, current encrypted endpoints, pending/queued/active
work, cumulative established gates and fixed failure counters. Snapshots are not
atomic. `connected` does not prove independent operators. `established_total`
includes disconnected endpoints; rejected operations are not unique attackers.
The discovery string distinguishes disabled/empty/unconnected/connected states.
No authenticated peers may indicate an outdated classical bootstrap cohort,
network reachability or pin/configuration problems; consult bounded public status
and local logs without exposing cookies, wallets or identity material.

The limits remain two crypto workers, 32 queued jobs, 64 pending endpoints,
four per known source and 16 anonymous endpoints. Failed addrman candidates retain
the existing ten-minute backoff; previously successful peers retain the ordinary
small-pool reconnect escape. Missing capability hints still permit a PQ attempt;
claimed hints never bypass authentication. Proxy/Tor/I2P routes receive the same
required transport selector, but real external topology qualification remains a
separate gate. Experimental service bit 24 is not a globally registered guarantee.

## Release and recovery

Verify exact source and package hashes independently. Native development bundles
and offscreen smoke tests do not certify signed installers or clean-machine OS
integration. The founder manually decides version/tag/publication/checksums and
activation/deployment. Follow the dedicated upgrade and rollback plans only after
that authorization. Extensive implementing-agent tests do not replace independent
review of the custom authenticated key exchange or establish a formal proof.
