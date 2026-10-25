# Candidate 1 isolated Stage A implementation and validation

Date: October 25, 2026. Branch: `v0.1`. Starting checkpoint:
`f268f94c209c44f6ffc6a544a7f0866bb441120e`. Tested runtime implementation:
`2682b2987a968f2cbd3b22ce4d7961277ec3d61f`.

The authorized P3/P4 Stage A engineering target is implemented: real Mercatura
regtest nodes perform Candidate 1's five-message authenticated exchange, send
encrypted application messages, relay native PQ transactions and blocks, enforce
explicit pins, reconnect and shut down. The final positive functional run passed
in 64.947 seconds. PQ remains disabled by default and enablement is rejected on
mainnet, testnet, testnet4 and signet. All functional runs used new temporary
datadirs and loopback ports. No public experimental deployment occurred.

Operation and reproduction instructions are in
[mercatura-pq-stagea.md](mercatura-pq-stagea.md). Public node observations are in
[mercatura-pq-stagea-evidence.json](mercatura-pq-stagea-evidence.json); measured
performance is in
[mercatura-pq-stagea-benchmarks.json](mercatura-pq-stagea-benchmarks.json).
These records contain public test identifiers and measurements, not wallet
secrets, networking seeds, traffic keys, RPC credentials or copied datadirs.

## 1. Overall progress and completed phases

The conservative overall program milestone count is **approximately 27%**:
P1-P3 are complete out of the original eleven phases. This counts completed
whole phases, not implementation effort or confidence in security. The
authorized Stage A runtime portion of P4 is also implemented and validated;
the original P4 additionally requires broader discovery/bootstrap and network
qualification, so that whole phase is not counted as closed.

| Original phase | Current status |
| --- | --- |
| P1 audit | Complete at the supplied checkpoint; preserved |
| P2 specification | Complete with the three required corrections frozen before implementation; conditional Stage A approval only |
| P3 primitives | Complete for the pinned portable implementation, integration and acceptance checks described below |
| P4 transport | Stage A runtime complete; decentralized bootstrap, discovery and IPv6/NAT/Tor qualification remain |
| P5 identities | Ephemeral default, POSIX persistent opt-in and outbound pins implemented; Windows persistence and production policy remain |
| P6 DoS protection | Initial limits, worker ownership, deadlines and recovery tests implemented; sustained stress qualification remains |
| P7 testing | Deterministic, regression, real-node, adversarial and sanitizer checks pass; guided fuzzing, endurance and independent analysis remain |
| P8 performance | Small same-host BIP324/PQ measurements complete; representative hosts and sustained operation remain |
| P9 platforms | Linux x86_64 tested; Windows, macOS, WSL2 and ARM64 unqualified |
| P10 observability | CLI/configuration and RPC status implemented; Qt presentation deferred |
| P11 deployment | Isolated Stage A demonstrated; Stages B/C/D remain unauthorized |

## 2. Corrected and frozen specification

The authoritative specification is
[mercatura-pq-transport-v1.md](mercatura-pq-transport-v1.md), frozen in
`5a90b9cb2c9d8c11c12d390f97be500b249d28bc` before custom-handshake code.

The required corrections establish responder M3 signature/pin verification
before decapsulation; require permanent packet/rekey vectors before validated
application traffic; and document cleartext identities, fresh per-connection
ephemeral defaults and explicitly selected unencrypted persistent storage.
Packet vectors were completed alongside implementation as authorized.

The five record lengths remain M1=3,265, M2=4,353, M3=4,409, M4=3,369 and
M5=60 bytes. Including both 16-byte prefaces gives exactly **15,488 bytes**:
7,750 initiator-to-responder and 7,738 responder-to-initiator. Signing inputs,
network/role domains, transcript construction and key schedule are unchanged.
No Candidate 2 or new cryptographic semantics were introduced.

| Preserved artifact | SHA-256 |
| --- | --- |
| Corrected specification, unchanged since freeze | `5960a8a1f032280f3a4bacb9aad07331eb1cae975e566e657e58d8d84ddc2e42` |
| Original `src/test/data/pq_p2p_candidate1.json`, byte-for-byte unchanged | `6222abf5163ee34a987fe383a8e10b07e50abf1a3ab93f2a0e031a50dd9415f5` |

The original fixture's historical approval-status text is retained to preserve
its bytes; the corrected specification records the current conditional decision.

## 3. Cryptographic integration

| Dependency | Pinned revision and use |
| --- | --- |
| mlkem-native v2.0.0 | `d1b2fe782888bdb761a50336012923180be7f502`; portable C ML-KEM-768 only |
| Existing mldsa-native v2.0.0 | `834a90d5e846ffa1e1611bd24e160bb2e9b86d35`; existing Mercatura wrapper reused |
| NIST ACVP reference vectors | `975de31eb83d87039ec88934fdc47d8c312b892d` |

All 43 copied upstream ML-KEM source/support files were checked against the
dependency manifest's SHA-256 values and Git blob IDs. The additional local
`README.mercatura` explains integration. The mathematical implementation was
not edited. ML-KEM public/private/ciphertext/shared-secret sizes are
1,184/2,400/1,088/32 bytes. Random coins come from Mercatura's strong RNG in
at-most-32-byte draws, with errors clearing outputs. Implicit rejection is
preserved; it is not reinterpreted as an explicit ciphertext-validity oracle.

The narrow `mercatura_p2p_crypto` API owns noncopyable, cleansing secret buffers
and supports RFC 5869 Extract/Expand and exact Pure ML-DSA signing context
`Mercatura/P2PAuth/v1`. Production signatures use fresh secure randomness;
deterministic coins are confined to public vectors/tests. Networking identities
are separate from wallet keys. The existing wallet HKDF implementation and
derivation serialization are unchanged. Shared HMAC changes only cleanse
temporary pads, hash states and intermediate values; golden outputs and wallet
backup/recovery checks pass.

## 4. Handshake, packet codec and integration status

`net_pq_protocol` implements exact incremental transcript hashing, canonical
headers, both ephemeral KEM contributions, mutual signatures, all frozen HKDF
labels, M4/M5 confirmations and independent traffic secrets. Invalid M3
signatures are tested to cause zero responder decapsulations. The initiator's
application gate opens after all M5 bytes are sent; the responder opens after a
valid M5. Fragmentation and M5 coalesced with the first encrypted packet pass.

`PQTransport` uses the existing `Transport` abstraction. KEM and signature work
runs in a bounded two-worker pool outside networking locks. Jobs own handshake
state, with cancellation/generation checks before publication. Shutdown joins
workers. Pending sessions, queues, source groups, admission rates, record sizes
and absolute deadlines are bounded as documented in the operation guide. At
full inbound capacity, a new unauthenticated attempt does not evict an existing
peer merely to make room for its handshake. No identity receives a validation
or resource-accounting exemption.

Packet encryption reuses the existing FSChaCha20 and FSChaCha20Poly1305 classes
unchanged. The wrapper implements frozen transcript/role/encrypted-length AAD,
u24 length encryption, zero-based directional counters, controls 0x00/0x80,
224-packet rekeys and the packet limit. Partial writes retain ciphertext without
reencryption. Invalid lengths, tags, controls, sizes, AAD, directions and counter
states fail closed; plaintext is not delivered before successful authentication.
Independent Python reference vectors cover 674 packets per direction (1,348
total), including three rekey boundaries in each direction, and match C++.

`-pqp2p=required` is explicit regtest-only policy for inbound/outbound/manual and
reconnect paths. There is no PQ-failure fallback to BIP324 or V1. Existing V1 and
BIP324 behavior remains selected by default with `-pqp2p=off`. An explicit RPC
request for classical V2 is rejected when required mode disables V2 acceptance.

Default identities are fresh per connection. POSIX persistent files require
explicit creation, owner-only directory/file permissions and validated exact
contents. Symlinks, hard links, special files, corrupt/wrong-network files and
silent replacement are rejected. Atomic publication does not overwrite an
existing identity. The seed file is unencrypted and identities/signatures are
cleartext on the wire; persistent identities enable session correlation.

Outbound expected identities use exact destination-to-fingerprint bindings.
The API also accepts responder-side pins, but Stage A has no inbound CLI pin
map. Unpinned authenticated sessions prove possession of the presented key;
they do not verify an expected node or protect against an intermediary using
its own identities. RPC exposes this distinction with separate encryption,
authentication and identity-verification flags. Pending/failed sessions expose
no completed assurance or established session identifier.

## 5. Actual node-to-node evidence and relay

The final run used the committed `2682b29` runtime and
`test/functional/feature_pq_transport.py`. Both endpoints reported `pq-v1`,
ML-KEM-768, ML-DSA-65, encryption and authentication. Their matching public
session ID was:

```text
4132848b334b11ef3e9d8ecaea8d33b9fe460f736c9142d25169a2ebdd49dc87
```

At the pinned observation, the outbound node reported
`pq_identity_verified=true`; the inbound endpoint had no configured expected
identity and correctly reported false. Subtracting per-message wire counts from
received-byte totals yields 7,750 and 7,738 handshake bytes. A separate transparent
wire relay captured all five records and the same exact 15,488-byte total.

The two full nodes relayed a native PQ transaction, included it in a relayed
block and agreed on height 102:

```text
txid:  61817cd1757412e33a64926190c41da3a93406861b2a20d695be53ee097fa456
block: fafa59db9b6ad3ae14272a8e26f1b8749b03622fb7833b0759a7ef87af647931
```

The 101-block funding fixture is for coinbase maturity, not a cadence benchmark.
Initial connection including TCP/RPC/version/pong took 260.844 ms and reconnect
262.378 ms in this final run. Reconnect changed both default ephemeral
identities and session ID. Persistent identities survived restart. Wrong pins
disconnected without fallback; correcting the explicit pin reconnected.
Classical peers and malformed input were rejected. Public-network enablement
failed startup. All isolated nodes stopped successfully.

## 6. Tests and adversarial coverage

| Check | Result and scope |
| --- | --- |
| Primitive/vector checker | PASS 287 normal and 287 ASan/UBSan cases: 101 symmetric, 105 existing NIST ACVP, 45 new official Pure ML-DSA signature cases, 12 domain/signature golden, 5 KEM negative, 3 upstream KAT, 4 Candidate primitive and 12 independent OpenSSL HKDF comparisons |
| Deterministic protocol/packet unit tests | PASS 4 cases, normal and ASan/UBSan; exact five-message fixture and packet/reference negatives |
| Independent packet reference | PASS 1,348 packets, three rekey boundaries per direction |
| Focused transport/resource/storage tests | PASS 9 cases; benchmark deliberately disabled in ordinary run and enabled separately |
| Selected broad regression | PASS 168 selected cases, 1,711,030 assertions across 16 suites; 810 outside selection skipped, not claimed as passed |
| Networking after inbound-capacity fix | PASS 17 selected cases; 962 outside selection skipped |
| Final positive real-node functional test | PASS in 64.947 s on committed runtime |
| Real-node negative wire test | PASS 24 adversarial scenarios, two absolute deadlines and preservation of an established peer at full inbound capacity |
| Existing legacy/default-off transaction and block relay | PASS `mercatura_p2p_propagation.py` |
| Existing wallet backup/restore and restored-wallet spend | PASS `mercatura_wallet_backup_restore.py`, restored balance 23,782.34 MCA and confirmed 1.00 MCA spend |
| Fuzz-target smoke | PASS 1,000 fixed-seed inputs for each of `pq_handshake` and `pq_packet`, normal and sanitized |

Selected regression suites were `crypto_tests`, `bip324_tests`, `net_tests`,
`pq_p2p_tests`, `pq_transport_tests`, `wallet_crypto_tests`,
`script_segwit_tests`, `mercatura_controller_tests`, `mercatura_emission_tests`,
`mercatura_fixedpoint_tests`, `pow_tests`, `blockchunk_tests`,
`block_capacity_tests`, `witness_resource_tests`,
`witness_resource_tests_chain` and `witness_resource_tests_net`.

Permanent unit tests exercise header/type/role/version/suite/flags/length and
network failures, wrong keys/context/transcripts/signatures/confirmations,
pins, illegal ordering/trailing data/early application traffic, packet AAD/tag/
counter/control/size failures, fragmentation, buffer release, cancelled/stale
results, pending/source/queue limits, queue/execution deadlines and recovery.
Replay, reflection and identity substitution are regression scenarios, not a
proof against all unknown-key-share or active attack constructions.

The real-node relay mutates preface, role, version, suite, flags, length, both
network bindings, both KEM ciphertexts, both signatures, both confirmations,
recorded M3 replay, role reflection, duplicate M1 at M3, identity substitution
and application ciphertext; it also disconnects after each of M1-M5. It checks
that the intended mutation was reached, no unauthenticated version was delivered
and no classical retry occurred. Seven-byte relay fragments are exercised.
The absolute preface/record deadlines expired after 5.059/30.082 seconds despite
later fragments, preventing indefinite timer extension.

Sanitizers cover primitive code, protocol/packet units and fuzz-target smoke.
The entire node, asynchronous adapter and storage implementation were not run
under sanitizers. `ASAN_OPTIONS=detect_leaks=0` was necessary in this environment;
LeakSanitizer and ThreadSanitizer qualification remain open. The fixed-seed smoke
runs are not coverage-guided fuzz campaigns. No full unit-suite or full functional
suite pass is claimed.

## 7. Performance and resource observations

Host: Debian 13, Linux 6.18.55 x86_64, Intel Xeon Platinum 8573C, GCC 14.2.0,
Release `-O2`, cgroup limit of two CPU cores and 8 GiB memory. This is not the
operator's Ryzen/WSL2 system. Both modes used the same host and binaries.

| In-memory diagnostic | BIP324 | Candidate 1 PQ |
| --- | ---: | ---: |
| Complete handshake median, 12 samples | 0.272 ms | 2.931 ms |
| Complete handshake maximum | 0.482 ms | 5.807 ms |
| Process CPU for 12 handshakes | 3.616 ms | 35.443 ms |
| Observed handshake bytes | 1,053-7,550 | 15,488 every sample |
| Transport codec throughput, 1,024 x 16 KiB messages | 169.12 MiB/s | 164.13 MiB/s |

The separate PQ packet encrypt/decrypt loop measured 181.99 MiB/s. Four concurrent
PQ sessions completed in 4.366 ms wall time / 9.013 ms process CPU, observing two
active workers, queue peak three and eight pending endpoint sessions. Both worker
stack reservations measured 1,048,576 bytes. The benchmark observed a maximum
crypto job of 3,335 microseconds. `sizeof(Handshake)=6,416` and
`sizeof(Identity)=5,984` are object sizes, not complete allocation or stack peaks.

Four real-node connection samples per mode had medians of 133.50 ms BIP324 and
212.70 ms PQ including RPC/TCP/version/pong. Combined CPU readings were 0.06 s
per mode at `/proc` clock-tick granularity. After connections, both modes used
about 180 MiB RSS per process. After the four-block RPC diagnostic, maximum
whole-process high-water RSS was about 311 MiB in each mode. PQ virtual memory
after connections was about 973 MiB per node; BIP324 was about 843/907 MiB.
These reservations include allocator arenas and thread runtime, not merely
the explicit worker stacks or committed resident memory.

Idle `getpeerinfo` median latency was 0.826 ms BIP324 and 0.999 ms PQ. During
four RPC-generated regtest blocks, medians/maxima were 1.340/455.615 ms and
0.909/156.788 ms respectively. The small sample and differing scheduling do not
establish comparative responsiveness. This diagnostic does not qualify native
MercaHash V2 CPU/GPU mining, realistic block cadence, sustained moderate churn,
endurance, lower-power hosts or TCP/WAN throughput. The conservative 64 KiB
pending-session allocation budget is not a whole-process RSS or library stack
high-water proof. Full saturation and long-duration memory/CPU profiling remain.

## 8. Platform and source-control status

Linux x86_64 daemon/CLI/full unit binaries build and the above tests pass.
GUI was disabled. Windows and macOS builds, ARM64 and WSL2 have not been run.
POSIX persistent storage is implemented; Windows persistence deliberately fails
closed pending an ACL and atomic-publication implementation. Windows worker-stack
reservations and Qt behavior are unqualified.

Validated local implementation commits, in order:

| Commit | Change |
| --- | --- |
| `5a90b9cb2c9d8c11c12d390f97be500b249d28bc` | Freeze corrected Stage A Candidate 1 specification |
| `ca783db7f2532bdd23d08f73f77ed7c7fcb27aeb` | Pinned portable ML-KEM and isolated P2P crypto API |
| `ff70b3e79e26bd6b662471c92c13de6d4248a9f0` | Official Pure ML-DSA-65 signature vectors |
| `f59c7d5a1f78da06d949de2eda75b74f08f1db61` | HMAC/HKDF secret-state cleanup |
| `3679f34dac56e82a9414be0a2f95f7aebaeeaea4` | Five-message exchange and packet codec/vectors |
| `2682b2987a968f2cbd3b22ce4d7961277ec3d61f` | Real-node regtest integration, resources, storage, RPC, adversarial tests and benchmarks |

This report and public evidence are committed separately after the runtime.
`project` is absent. Only `origin` is configured and it was not pushed to.
All changes remain local; no history was rewritten and no mining branch was
merged or modified. The preserved `work` branch remains at
`631886dd5159614b628ced3fe4fa1e289ce19451`.

A diff against the starting checkpoint confirms no changes to consensus,
validation, chain parameters, proof-of-work, script, wallet, mining, MercaHash,
the transaction ML-DSA wrapper/vendor source, wallet PQ derivation or wallet HKDF
source. The networking integration and shared HMAC memory cleanup are covered
by the regression checks above. No existing blockchain/datadir was reused.

## 9. Unresolved concerns, approval gates and next work

The custom authenticated exchange still needs independent analysis of its
authentication, key-confirmation and two-contribution combiner assumptions.
Internal consistency, standard primitive vectors and working nodes do not prove
the composition secure. An independently implemented peer has not verified
interoperability; the Python packet reference covers symmetric framing only.
Cleartext identities, unpinned intermediary exposure and unencrypted persistent
seeds remain explicit experimental policy limitations.

Native cryptographic calls are not preemptible: timeouts cancel publication,
then cleanup/shutdown waits for the running call to finish. Conservative memory
accounting, cancellation and bounded workers need sustained hostile-load,
allocator/stack profiling and race/leak qualification. IPv6 source grouping is
implemented but full IPv6/NAT/Tor/network-partition operation is not qualified.
Public seed lists remain empty, and decentralized community bootstrap is an
open launch blocker. Pin rotation/recovery and production identity storage
policy require further design and operator review.

Next concrete development work, within any subsequently authorized scope:

1. Run coverage-guided `pq_handshake`/`pq_packet` fuzzing with retained corpora,
   sanitizer coverage of the full adapter/node, TSAN and working leak detection.
2. Commission independent AKE/composition review and an independent endpoint
   interoperability implementation before broadening trust or deployment claims.
3. Exercise sustained churn, full admission/queue saturation, partitions,
   shutdown races and RSS/stack high-water profiling on modest and lower-power
   systems; assess real native-mining responsiveness on an approved combined
   release candidate without modifying the separate mining branch here.
4. Qualify Windows/macOS/ARM64/WSL2, implement Windows identity ACL/publication,
   and finish Qt presentation and operator recovery/rotation policy.
5. Design and test decentralized fresh-datadir bootstrap, IPv4/IPv6, shared-IP,
   NAT and Tor/proxy operation with independent community peers available and
   project-operated bootstrap endpoints unavailable.
6. Complete platform/regression/security review gates and obtain explicit
   authorization before Stage B optional public-testnet, Stage C PQ-required
   public-testnet or Stage D mainnet work. No production-security approval,
   independent-audit approval or deployment authorization is asserted here.

## 10. Exact changed-file inventory

The following paths are the complete change set relative to
`f268f94c209c44f6ffc6a544a7f0866bb441120e`, including this report and its public
evidence. Unchanged original fixtures and prior audit documents are not listed.

82 files:

```text
contrib/devtools/pq-fuzz-smoke.cpp
contrib/devtools/pq-node-benchmark.py
contrib/devtools/pq-packet-vectors.py
contrib/devtools/pq-primitive-check.py
contrib/devtools/pq-stagea-test-main.cpp
doc/README.md
doc/design/mercatura-pq-stagea-benchmarks.json
doc/design/mercatura-pq-stagea-evidence.json
doc/design/mercatura-pq-stagea-validation.md
doc/design/mercatura-pq-stagea.md
doc/design/mercatura-pq-transport-v1.md
src/CMakeLists.txt
src/crypto/CMakeLists.txt
src/crypto/hmac_sha3_384.cpp
src/crypto/hmac_sha3_384.h
src/crypto/mercatura_p2p_crypto.cpp
src/crypto/mercatura_p2p_crypto.h
src/crypto/mlkem_native/LICENSE
src/crypto/mlkem_native/META.yml
src/crypto/mlkem_native/README.mercatura
src/crypto/mlkem_native/SECURITY.md
src/crypto/mlkem_native/mlkem/README.md
src/crypto/mlkem_native/mlkem/mlkem_native.c
src/crypto/mlkem_native/mlkem/mlkem_native.h
src/crypto/mlkem_native/mlkem/mlkem_native_config.h
src/crypto/mlkem_native/mlkem/src/cbmc.h
src/crypto/mlkem_native/mlkem/src/common.h
src/crypto/mlkem_native/mlkem/src/compress.c
src/crypto/mlkem_native/mlkem/src/compress.h
src/crypto/mlkem_native/mlkem/src/context.h
src/crypto/mlkem_native/mlkem/src/debug.c
src/crypto/mlkem_native/mlkem/src/debug.h
src/crypto/mlkem_native/mlkem/src/fips202/fips202.c
src/crypto/mlkem_native/mlkem/src/fips202/fips202.h
src/crypto/mlkem_native/mlkem/src/fips202/fips202x4.c
src/crypto/mlkem_native/mlkem/src/fips202/fips202x4.h
src/crypto/mlkem_native/mlkem/src/fips202/keccakf1600.c
src/crypto/mlkem_native/mlkem/src/fips202/keccakf1600.h
src/crypto/mlkem_native/mlkem/src/fips202/native/api.h
src/crypto/mlkem_native/mlkem/src/fips202/native/auto.h
src/crypto/mlkem_native/mlkem/src/indcpa.c
src/crypto/mlkem_native/mlkem/src/indcpa.h
src/crypto/mlkem_native/mlkem/src/kem.c
src/crypto/mlkem_native/mlkem/src/kem.h
src/crypto/mlkem_native/mlkem/src/native/api.h
src/crypto/mlkem_native/mlkem/src/native/meta.h
src/crypto/mlkem_native/mlkem/src/params.h
src/crypto/mlkem_native/mlkem/src/poly.c
src/crypto/mlkem_native/mlkem/src/poly.h
src/crypto/mlkem_native/mlkem/src/poly_k.c
src/crypto/mlkem_native/mlkem/src/poly_k.h
src/crypto/mlkem_native/mlkem/src/randombytes.h
src/crypto/mlkem_native/mlkem/src/sampling.c
src/crypto/mlkem_native/mlkem/src/sampling.h
src/crypto/mlkem_native/mlkem/src/symmetric.h
src/crypto/mlkem_native/mlkem/src/sys.h
src/crypto/mlkem_native/mlkem/src/verify.c
src/crypto/mlkem_native/mlkem/src/verify.h
src/crypto/mlkem_native/mlkem/src/zetas.inc
src/crypto/mlkem_native/test/src/test_mlkem.c
src/crypto/mlkem_native/test/test_vectors/expected_test_vectors.h
src/init.cpp
src/net.cpp
src/net.h
src/net_pq.cpp
src/net_pq.h
src/net_pq_identity.cpp
src/net_pq_protocol.cpp
src/net_pq_protocol.h
src/node/connection_types.cpp
src/node/connection_types.h
src/rpc/net.cpp
src/test/CMakeLists.txt
src/test/data/pq_p2p_mldsa_acvp.json
src/test/data/pq_p2p_packets.json
src/test/fuzz/CMakeLists.txt
src/test/fuzz/pq_transport.cpp
src/test/pq_p2p_tests.cpp
src/test/pq_transport_tests.cpp
test/functional/feature_pq_transport.py
test/functional/feature_pq_transport_negative.py
test/functional/test_runner.py
```
