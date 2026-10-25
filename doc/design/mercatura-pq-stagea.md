# Experimental PQ P2P: isolated Stage A

The October 25, 2026 handoff authorizes isolated regtest engineering of Candidate
1 after its mandatory corrections. The corrected specification is
[mercatura-pq-transport-v1.md](mercatura-pq-transport-v1.md), frozen at `5a90b9c`.
The five messages, signature inputs, HKDF schedule and 15,488-byte successful
exchange are unchanged. This custom authenticated key exchange has no formal
security proof or independent production-security approval.

## Configuration and containment

`-pqp2p=off` is the default on every network. It preserves existing V1/BIP324
selection. `-pqp2p=required` selects PQ for every inbound and outbound P2P
connection on the node, including manual/RPC and reconnect attempts. It rejects
startup on mainnet, testnet, testnet4 and signet. Stage A does not implement the
future `prefer` mode or a mixed PQ/classical listener. There is no automatic
classical retry after a PQ failure. Existing RPC `v2transport` preferences cannot
override required PQ policy. A required listener stops advertising V2 acceptance.
Explicit RPC requests for classical V2 are rejected when that acceptance is
disabled; an ordinary manual connection uses PQ under required mode.

Use new temporary datadirs, separate ports and loopback-only binds. Do not reuse
an existing node/Qt datadir or connect experimental binaries to public peers.
For example, with binaries in `build-pq/bin`:

```sh
PQ_TEST_A=$(mktemp -d)
PQ_TEST_B=$(mktemp -d)
chmod 700 "$PQ_TEST_A" "$PQ_TEST_B"
build-pq/bin/mercaturad -regtest -datadir="$PQ_TEST_A" -daemonwait \
  -pqp2p=required -connect=0 -dnsseed=0 -fixedseeds=0 -discover=0 -natpmp=0 \
  -bind=127.0.0.1:28444 -port=28444 -rpcport=28443
build-pq/bin/mercaturad -regtest -datadir="$PQ_TEST_B" -daemonwait \
  -pqp2p=required -connect=0 -dnsseed=0 -fixedseeds=0 -discover=0 -natpmp=0 \
  -bind=127.0.0.1:28544 -port=28544 -rpcport=28543
build-pq/bin/mercatura-cli -regtest -datadir="$PQ_TEST_B" -rpcport=28543 \
  addnode 127.0.0.1:28444 onetry
build-pq/bin/mercatura-cli -regtest -datadir="$PQ_TEST_A" -rpcport=28443 getpeerinfo
build-pq/bin/mercatura-cli -regtest -datadir="$PQ_TEST_B" -rpcport=28543 getpeerinfo
build-pq/bin/mercatura-cli -regtest -datadir="$PQ_TEST_B" -rpcport=28543 stop
build-pq/bin/mercatura-cli -regtest -datadir="$PQ_TEST_A" -rpcport=28443 stop
```

## Networking identities and expected peers

Each connection gets a fresh ML-DSA-65 identity by default, independently of its
fresh ephemeral ML-KEM material. No wallet keys or transaction signing contexts
are used. Public identities and signatures are sent in cleartext. Unpinned
authentication proves possession of the presented key; it does not identify an
expected remote node and does not prevent an intermediary using its own keys.

For explicitly persistent identities, use `-pqp2pidentity=pq_identity.dat`.
Only a filename in the selected network datadir is accepted. Create it once
through `-pqp2pidentitycreate=1`; subsequent starts omit the create flag.
Existing files are never overwritten. Missing, corrupt, wrong-network, unsafe
permission, symlink or public-key/seed mismatch cases fail startup. The POSIX
implementation uses an opened owner-only directory, exclusive mode-0600
temporary, file sync, atomic no-clobber link publication and directory sync.
The parent directory must be owner-only (normally 0700). Windows persistent
storage fails closed until the required ACL/publication implementation is
qualified; ephemeral networking identities do not use this storage path.

The 2,127-byte seed file is **unencrypted**. Filesystem permissions do not supply
encryption at rest. Persistent public identities allow passive/active session
correlation; Tor does not hide an exposed stable key. This is provisional
experimental storage/privacy policy, not production identity-security approval.

An expected outbound identity is configured as:

```text
-pqp2ppin=127.0.0.1:28444=mcapq1:<96 lowercase hex digits>
```

Repeat the option for different exact destination strings, including ports.
The fingerprint must come from a trusted operator channel. A persistent node
logs its own public fingerprint at startup. Do not treat a fingerprint learned
from an unpinned connection as an independently trusted binding. Pins neither
rotate nor update automatically. Duplicate bindings and noncanonical fingerprints
are rejected. The Stage A command-line pin map applies to outbound destinations;
the transport API also supports an independently supplied responder-side pin.
No mandatory trusted-node list or validation exemption is introduced.

`getpeerinfo` exposes:

| Session | pq_encrypted | pq_authenticated | pq_identity_verified |
| --- | --- | --- | --- |
| Legacy, pending or failed | false | false | false |
| Established unpinned PQ | true | true | false |
| Established, expected pin matched | true | true | true |

An established PQ peer reports `transport_protocol_type="pq-v1"`, a public
session ID, public fingerprint, `pq_kem="ML-KEM-768"` and
`pq_auth_algorithm="ML-DSA-65"`. Pending PQ sessions identify their selected
transport but expose no established session ID/identity or completed assurance.
Secrets and traffic keys are never status fields. Qt-specific presentation and
cross-platform GUI qualification are outside this Stage A implementation batch.

## Architecture and bounds

`net_pq_protocol` implements the frozen five-record exchange and independent
directional packet states. It hashes exact accepted bytes incrementally rather
than retaining transcript copies. The responder verifies M3's signature and
expected binding before decapsulation. The initiator application gate opens only
after every M5 byte has been reported sent; the responder gate opens after a
valid M5. M5 processing performs only MAC/HKDF and cipher initialization, so it
can consume a coalesced first packet without running KEM/signature work under
network locks. Invalid input releases no unauthenticated application message.

`PQTransport` implements existing `Transport` methods and adds short polling of
worker results/deadlines from the socket loop. Each job exclusively owns its
handshake state; no worker has a node/transport pointer. Result ownership and a
generation check prevent stale publication. Cancellation retains buffers and
admission accounting until the executing owner is finished. Accounting leases
own separate quota state, so a worker cannot destroy its own pool. Shutdown joins
the bounded workers. Native C cryptographic calls cannot be forcibly interrupted;
timeouts cancel publication and running calls finish before cleanup/join.

| Resource | Stage A behavior |
| --- | --- |
| Pending sessions | 64 globally, including outbound/queued/executing work |
| Known source | 4 pending per IPv4 /32 or IPv6 /64 |
| Anonymous source | Shared pool of 16 within the global cap |
| Crypto pool | 2 executing, 32 queued, one outstanding job per transport |
| POSIX worker stack | Explicit 1 MiB reservation; measured during Linux tests |
| Session-owned pending storage | Fixed record bounds plus compile-time conservative 64 KiB allocation budget |
| Receive/send handshake records | At most 4,409 bytes each; canonical headers checked before body reservation |
| Admission | Global burst 32/refill 8 per second; source burst 4/refill 1 per second |
| Source accounting | 256 entries; idle entries expire after 120 seconds; unknown sources fail closed when full |
| Deadlines | Preface 5 s, record 30 s, total 120 s, queue 2 s, executing job 5 s |
| Failure diagnostics | One per failed connection, global burst 20/refill 10 per second |

At full inbound capacity, required mode rejects the new unauthenticated attempt
instead of evicting an existing peer to make room for an incomplete handshake.
This policy does not depend on pins or grant particular identities an exemption.

The local storage calculation is conservative allocation accounting, not a
measured proof of whole-process RSS or library stack high-water usage. Worker
stack reservations are separate from the per-session heap bound. Windows worker
stack reservations have not been qualified. All resource values require further
measurement under realistic multi-node load before widening Stage A.

Packet encryption uses the existing FSChaCha20 and FSChaCha20Poly1305 classes
unchanged. A new directional wrapper binds the final transcript, sender role and
encrypted length as AAD. Both classes rekey every 224 packets. Partial writes
reuse an existing ciphertext; a receive length is decrypted once. AEAD failure
is terminal. Completed send/decode buffers are released rather than permanently
holding backpressure allocations. No BIP324 ECDH initializer is repurposed.

## Reproducing checks

The exact evidence and limitations are recorded in the
[Stage A validation report](mercatura-pq-stagea-validation.md).
Useful commands from the repository root are:

```sh
cmake -S . -B build-pq -DBUILD_GUI=OFF -DBUILD_TESTS=ON -DENABLE_IPC=OFF
cmake --build build-pq --target bitcoind bitcoin-cli pq_stagea_tests pq_transport_runner pq_fuzz_smoke -j4
build-pq/bin/pq_stagea_tests --log_level=test_suite
build-pq/bin/pq_transport_runner --log_level=test_suite
build-pq/bin/pq_transport_runner --run_test=pq_transport_tests/benchmark --log_level=test_suite
build-pq/bin/pq_fuzz_smoke
python3 contrib/devtools/pq-primitive-check.py --work-dir /tmp/mercatura-pq-primitives --mlkem-source src/crypto/mlkem_native
python3 contrib/devtools/pq-packet-vectors.py
python3 test/functional/feature_pq_transport.py --configfile=build-pq/test/config.ini
python3 test/functional/feature_pq_transport_negative.py --configfile=build-pq/test/config.ini
```

Use fewer build jobs when memory/CPU limits require it. The packet-vector script
checks the frozen file by default. `--write` is an explicit fixture update, never
a way to accommodate a crypto mismatch. The independent reference uses the
existing Python FS cipher implementations and checks both directions through
three rekey boundaries each. It does not call the C++ PQ wrapper.

`pq_fuzz_smoke` executes the permanent `pq_packet` and `pq_handshake` fuzz targets
on 1,000 fixed-seed inputs each. It is a bounded smoke test, **not** a
coverage-guided campaign. These targets are also registered in the normal fuzz
executable for later libFuzzer qualification. Full parser/state/adversarial
campaigns, sustained churn, endpoint interoperability using an independent
implementation, cross-platform builds and independent AKE analysis remain gates.

The isolated functional test uses new datadirs and loopback ports, checks actual
node connections, native PQ transaction/block relay, ephemeral reconnects,
persistent identities and pins, wrong-pin failures, malformed traffic, classical
rejection, public-network startup rejection and shutdown. Its 101-block funding
fixture tests coinbase maturity; it is not a realistic block-cadence benchmark.
No consensus, wallet derivation, transaction authorization, chain parameter or
mining-branch merge is part of this work.

The separate negative wire test transparently relays real nodes without session
keys, captures a successful public handshake for replay tests, and verifies that
tampering/disconnects fail without unauthenticated version delivery. It also
checks established-peer preservation at full inbound capacity and absolute
preface/record deadlines despite later fragments. Unit tests cover absolute
queue deadlines and discarding an otherwise successful late crypto result.

The optional Linux diagnostic is separate from normal regression execution:

```sh
python3 contrib/devtools/pq-node-benchmark.py --configfile=build-pq/test/config.ini --nocleanup
```

It records small BIP324/PQ samples, per-process `/proc` memory/CPU counters,
reconnects and RPC response times during four RPC-generated regtest blocks.
These are local diagnostics, not statistical capacity, realistic cadence,
endurance or MercaHash V2 native CPU/GPU qualification.

Empty public seed lists remain a separate bootstrap/launch blocker. Nothing in
Stage A authorizes public-testnet deployment, mainnet enforcement, a mining merge
or a production-security claim.
