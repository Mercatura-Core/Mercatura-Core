# Mercatura PQ P2P: P1 source audit and integration plan

Status: source audit complete with build/review limitations below. P2 design is a
proposal awaiting explicit maintainer approval. No runtime networking change is
authorized by this document. See [the candidate specification](mercatura-pq-transport-v1.md)
and [validation evidence](mercatura-pq-validation.md).

## Verified checkout and scope

The audit base is `631886dd5159614b628ced3fe4fa1e289ce19451`, initially clean on
local branch `work` in `/workspace/Mercatura-Core`. A local `v0.1` branch was
created at that same commit; `work` was preserved. There is one worktree and only
an `origin` remote pointing to `https://github.com/Mercatura-Core/Mercatura-Core.git`.
No `project` remote, local mining branch, build directory, or AGENTS.md was found.
The local `origin/v0.1` tracking ref is stale at `43382aea07be42935c0040c1b6fdf12d2648d92f`.
GitHub's branch API independently confirms remote `v0.1` at the audit base,
`main` at `d0ddadfa9366a82c529b6993573f5b9b48cc1780`, and the mining branch at
`cc5db1df34d362bdfecd11003404bc307c2219b0`.

Only documentation, public deterministic fixtures, and a standalone primitive
test driver are added during this checkpoint. Consensus, wallet derivation,
transaction authorization, mining, chain parameters, networking behavior, and
public infrastructure remain outside these changes. No node was started or
connected to a public network. No merge, cherry-pick, rebase, reset, stash,
history rewrite, or push was performed.

## Transport architecture and reuse map

Line references below refer to the audit base. Inspect the function names again
when implementing; offsets will change.

| Component | Actual behavior and proposed use |
| --- | --- |
| `src/net.h:261`, `Transport` | Streaming transport abstraction: `ReceivedBytes`, `ReceivedMessageComplete`, `GetReceivedMessage`; send queue ownership through `SetMessageToSend`, `GetBytesToSend`, `MarkBytesSent`; status and reconnect hooks. Implement a separate `PqTransport` against this interface. |
| `src/net.h:374`, `V1Transport` | Original network magic/header/checksum transport. Preserve without modification to its wire protocol. |
| `src/net.h:456`, `src/net.cpp:988–1599`, `V2Transport` | Separate receive/send state machines, 64-byte EllSwift key exchange, garbage terminator, version packet, then application packets. Keep BIP324 identifiable and interoperable. |
| `src/net.cpp:1081`, `ProcessReceivedMaybeV1Bytes` | Incoming V1 discrimination against the 16-byte magic-plus-version prefix; nonmatching bytes trigger V2. PQ discrimination must sit before this decision, replaying buffered bytes exactly once for legacy selection. |
| `src/net.cpp:1119`, `ProcessReceivedKeyBytes` | Initializes BIP324 cipher using EllSwift ECDH, enables sending, queues version packet. This is too early for the PQ authentication gate. Do not use it for PQ initialization. |
| `src/net.cpp:1206`, `1324` | Decrypts 3-byte length before bounded reserve; full AEAD before message release; reserves at most 256 KiB ahead after setup. Reuse the reasoning and packet codec; use much smaller fixed limits during PQ handshaking. |
| `src/net.cpp:1414`, `1483` | V2 short message identifiers plus canonical long command encoding. Extract a shared message codec, preserving existing IDs and Mercatura's large-block commands. Avoid copying an independent mapping into PQ. |
| `src/net.cpp:1515`, `1532` | Stable send spans and partial write accounting; one serialized packet buffered at a time. Preserve the contract, including `more` semantics and no duplicate cipher advancement on partial writes. |
| `src/net.cpp:1554`, `1954` | Outgoing V2 may reconnect as V1 if nothing was received and at least a V1 header's worth was sent. PQ must return false and the reconnection queue must preserve its required policy and pin. |
| `src/net.cpp:1580`, `618`, `src/rpc/net.cpp:307` | V2/session ID reported only after version authentication. Extend `Transport::Info`, copied node stats, and existing RPC fields; do not infer identity verification from transport type. |
| `src/net.cpp:4040`, `CNode` constructor | Central `MakeTransport` factory currently takes a Boolean V2 selector. Replace the selector with explicit transport/policy parameters and inbound dispatch; never reinterpret PQ as V2. |
| `src/net.cpp:376`, `1738`, `1875`, `3038` | Outbound connections, accepted sockets, manual connection requests, `OpenNetworkConnection`. Carry PQ policy through every entry point, reconnection, feeler, private-broadcast, and address-fetch path. |
| `src/net.cpp:664`, `CNode::ReceiveMsgBytes` | Holds per-node `cs_vRecv`, repeatedly calls the transport while bytes remain, and queues completed messages for processing. A transport that consumes zero bytes while awaiting a worker can cause a busy loop. Bounded worker integration must explicitly handle this contract. |
| `src/net.cpp:1601`, socket loop | Uses `cs_vSend` while serializing and sending; callbacks must not perform expensive signing or KEM work under that lock. |
| `src/net_processing.cpp`, `InitializeNode`, version/verack handling | Existing P2P version is a later application handshake; outgoing version may already be queued. Queue ownership can remain, but PQ must refuse message serialization and report no received application until the transport authentication/confirmation gate is complete. |

## Cipher and memory audit

`src/bip324.cpp:39` uses salt `bitcoin_v2_shared_secret || MessageStart`,
secp256k1 ECDH, and `CHKDF_HMAC_SHA256_L32`. It derives four independent packet
keys, garbage terminators, and a session ID, then explicitly cleanses ECDH and
HKDF temporaries. `BIP324Cipher` owns an ECDH key and EllSwift public key. Its
initializer is not a generic KDF input API. Do not patch it to accept an arbitrary
PQ shared secret, or claim its ECDH authentication properties apply to PQ.

The independently reusable pieces are `FSChaCha20` and
`FSChaCha20Poly1305` in `src/crypto/chacha20*`. They accept 32-byte keys and use
the BIP324 rekey interval of 224 packets. `AEADChaCha20Poly1305::Decrypt`
checks the tag before producing plaintext. AEAD and length ciphers have different
counter/rekey rules; preserve both, including decoys and encrypted lengths.
Underlying ChaCha key state is cleansed on destruction, and rekey temporaries
are cleansed. New secret-owning session objects must also erase the initial
traffic-key arrays, KEM decapsulation keys, encapsulation coins, signing
randomness, shared secrets, PRK, and pending-worker copies.

`src/crypto/sha3.*` has SHA3-256/384/512. `SHA3_384` produces 48 bytes with
104-byte sponge rate; `CHMAC_SHA3_384` uses that HMAC block size. The HMAC object
retains inner/outer state; its automatic temporary arrays and the wallet HKDF's
PRK currently have no dedicated cleansing destructor. This is a reuse concern
for new P2P secrets, not authorization to change wallet behavior during P1.
Use scoped, explicitly cleansed P2P KDF storage after approval, including HMAC
state, and verify generated code where practical.

`CHKDF_HMAC_SHA3_384_L32` in `src/crypto/hkdf_sha3_384.*` implements Extract
and the first 32 bytes of `HMAC(PRK, info || 01)`. This is the correct one-block
RFC 5869 case. It is used by `DeriveMercaturaPQChildSeed` and must stay
byte-for-byte compatible. Add a separately named general Extract/Expand facility
after approval, with the RFC 5869 bound of 12,240 output bytes; retain the L32
interface and golden vectors. Do not implement Noise's special two/three-output
HKDF as a substitute for RFC 5869.

`src/random.*` provides `GetStrongRandBytes`/`GetRandBytes`; OS entropy failures
are fatal rather than recoverable success with zero bytes. Wrappers must preserve
this fail-closed contract. Test-only injected failure hooks must never select
deterministic randomness in production.

## Existing ML-DSA integration

`src/crypto/mldsa_native/MERCATURA_PIN.txt` names v2.0.0, commit
`834a90d5e846ffa1e1611bd24e160bb2e9b86d35`. The upstream tag resolves to that
same commit. `src/crypto/CMakeLists.txt` builds ML-DSA-65 portable C with
`MLD_CONFIG_CORE_API_ONLY`. `mercatura_mldsa.c` checks exact sizes, formats
`00 || uint8(context_length) || context`, and calls
`PQCP_MLDSA_NATIVE_MLDSA65_signature_internal`/`verify_internal` with the
external-μ flag zero. This is Pure ML-DSA over the caller's message. It is not
HashML-DSA merely because a caller's message is a digest.

Reuse this narrow interface with a new P2P context and independently generated
identity seed. Do not reuse `mercatura_pqkey`, spending keys, wallet seeds,
addresses, or the `Mercatura/PQAuth/v1` authorization context. Upstream signing
requires a valid secret key: persistent identity loading should regenerate a
validated keypair from its seed and check its recorded public key before use.

Size constants are 1,952-byte public key, 4,032-byte expanded secret key,
3,309-byte signature, and 32-byte seed/randomness. Upstream
`MLD_TOTAL_ALLOC_65_SIGN=69312` describes major allocations, not a total stack
bound. The 39,872-byte verify and 44,320-byte keygen allocation constants likewise
exclude other stack usage. A 16 KiB worker-stack assumption would be unsafe.
Portable Linux sanitizer checks of this existing implementation are recorded
separately; no Windows/macOS conclusion follows from those checks.

## Connection policy, discovery, and DoS

`NODE_P2P_V2` is service bit 11. `-v2transport` controls its local advertisement
in `src/init.cpp:981`; automatic outbound selection intersects local and addrman
service flags at `src/net.cpp:2912`. These flags are unauthenticated hints and
are not available as a trusted pre-handshake negotiation channel. Manual RPC
paths separately accept `v2transport`. A PQ-required node must attempt PQ even
with an empty or stale capability cache; it must not depend on learning a bit
over an initially classical connection.

`src/kernel/chainparams.cpp` explicitly clears both DNS and fixed seed lists for
mainnet and Mercatura public testnet. Both currently document manual bootstrap.
Testnet4 still contains inherited Bitcoin testnet4 seeds and parameters; it is
not evidence of Mercatura PQ peer availability. Decentralized fresh-datadir
bootstrap is therefore an unresolved launch blocker. No seed or chain parameter
was changed here. The candidate's discovery policy needs a separate qualification
test with project endpoints unavailable and independent peers available.

`CreateNodeFromAcceptedSocket` checks network activity, selectable socket,
permissions/bans, existing inbound cap, and eviction. `AttemptToEvictConnection`
uses multiple protected characteristics to resist eclipse attacks. PQ identity
proof or pin status must not add an eviction exemption or validation privilege.
Admission must avoid evicting established peers merely to admit a flood of
incomplete PQ handshakes.

`CConnman::InactivityCheck` uses a 60-second default initial connection timeout
and 20-minute send/receive intervals. It is not a complete PQ per-stage deadline
or CPU admission controller. Existing receive flood limits are application
queue limits, not a limit on KEM/signature work. Add fixed pre-authentication
buffers, absolute monotonic deadlines, global pending caps, bounded worker
admission, and source/anonymous fairness as specified in P2. Fragment trickles
must not extend deadlines indefinitely.

IPv4/IPv6, Tor, I2P, CJDNS, SOCKS proxies, network groups, reachability, and ban
policy already live in `CConnman`/`netbase`. Keep those paths. Accepted onion
connections deliberately do not use the proxy address as a remote identity.
Apply a shared anonymous-resource budget rather than mistaking the Tor proxy's
loopback IP for each user's source. Identity pinning can bind a configured onion
destination; it must not expose its hostname or private endpoint through RPC.

## Proposed files after design approval

| Files | Boundary |
| --- | --- |
| New `src/net_pq.h/.cpp` | Separate `PqTransport`, handshake/cipher state, bounded parsing, no consensus logic. |
| New narrow ML-KEM wrapper and `src/crypto/mlkem_native/` | Pinned library, portable backend, exact sizes/RNG/error/cleansing API. |
| New P2P identity module | Network-specific persistent or ephemeral key material and expected-key policy; no wallet dependency. |
| `src/net.h/.cpp`, `src/node/connection_types.h/.cpp` | Factory, policy propagation, inbound discriminator, worker lifecycle, stats, explicit PQ type. |
| `src/init.cpp`, node context/shutdown integration | Opt-in isolated-regtest policy, identity initialization, bounded worker startup/teardown. |
| `src/protocol.h/.cpp`, addrman selection, `src/rpc/net.cpp` | Experimental capability hint, discovery, pin-aware connection RPCs, accurate assurance fields. |
| `src/crypto/hkdf_sha3_384.*`, crypto/node CMake | New general HKDF interface with old wallet behavior retained; build only intended library configurations. |
| `src/qt/rpcconsole.cpp`, peer stats/model and existing peer panel | Display encryption, key possession, and expected identity separately. |
| `src/test`, `test/functional`, fuzz, bench and their CMake/runner lists | Vectors, negative matrix, lifecycle, end-to-end regression, actual transport benchmarks. |

No implementation of those runtime changes is included at this checkpoint.

## Mining integration audit

The remote comparison of the audit base with
`cc5db1df34d362bdfecd11003404bc307c2219b0` reports 28 commits ahead, zero behind,
and the audit base as merge base. The checkpoint is real and is not already in
this checkout. Its patch lists changes to `src/crypto/CMakeLists.txt`,
`src/net_processing.cpp`, `src/test/CMakeLists.txt`, `test/functional/test_runner.py`,
and several mining/Qt test files. Crypto source registration and test registration
are likely textual conflicts; network-processing changes must preserve the
mining branch's header/PoW handling. Its Qt work changes mining/wallet/client
models while PQ is expected to touch the existing peer console, so direct UI
overlap appears lower, with shared build and responsiveness risks remaining.
The inspected comparison does not list top-level CMake or `src/net.cpp` changes.
This is a path/patch audit, not an attempted merge or proof of compatibility.

Do not merge or modify the mining branch without explicit authorization. An
eventual combined candidate must rerun native PQ transactions, header relay,
large-block chunking, realistic mining/network concurrency, reorg/partition
convergence, wallet/Qt responsiveness, and platform release checks. Preserve
the optional status of extreme rapid-wallet stress tests.

## Upstream review and dependency choice

* Quantus QIP-4 at improvement-proposals commit
  `b19530aff9cd82d4d49f9cc45f3463db1e2c4da7` is a draft. Chain source reviewed at
  `9e6d01f8bbefd126d87be6325e8cfc1f09825ae9`, especially
  `client/litep2p/src/crypto/{dilithium.rs,noise/mod.rs,noise/protocol.rs}`.
  It uses Clatter 2.2.0's `PqHandshake<MlKem768,MlKem768,ChaChaPoly,Sha256>`.
  Identity payload signs `noise-libp2p-static-key: || KEM public key` using
  ML-DSA-87; the KEM pattern is four messages. This is useful architectural
  evidence, not a portable proof for a new transcript/signature construction.
* BIP324 source reviewed at bitcoin/bips commit
  `100ba872c808946155b1eeb8c4568058f2eea0a7`, `bip-0324.mediawiki`, alongside
  Mercatura's actual cipher and transport tests. The user-supplied bitcoin.org
  mirror is not reachable through the shell proxy; the GitHub source supplies
  the specification. Preserve BIP324 interoperability in its existing class.
* Noise's hash-functions section requires `HASHLEN` 32 or 64. A SHA3-384
  transcript is 48 bytes. The proposal deliberately does not instantiate or
  modify Noise's chaining-key algorithm. Standard RFC 5869 with HMAC-SHA3-384
  has an ordinary 48-byte PRK and does not have this Noise restriction.
* `mlkem-native` latest release reported by GitHub is v2.0.0 (2026-08-07),
  tag commit `d1b2fe782888bdb761a50336012923180be7f502`. Evaluate portable C,
  parameter set 768, no native arithmetic/FIPS202 backend flags. Only standalone
  evaluation compiles it now. The 43 evaluation files are pinned by Git blob
  hashes in `mercatura-pq-dependencies.json`. They include the portable source,
  license, security policy, and upstream KAT header. They are not vendored into
  the node. Full release provenance and packaging remain P3 work.
* `mldsa-native` latest release is also v2.0.0 (2026-08-07), matching the existing
  pin. Both offer Apache-2.0 OR ISC OR MIT licensing. Preserve original notices
  and selected license when vendoring. Do not upgrade transaction crypto as a
  side effect of P2P work.
* ML-KEM APIs return distinct invalid-public-key and invalid-secret-key errors.
  A malformed ciphertext of the correct size normally uses implicit rejection
  and returns a pseudorandom secret with success. The protocol must detect the
  mismatch through transcript authentication/key confirmation, not expect a
  decapsulation validity oracle.
* GitHub connector access to security-advisories endpoints was rejected as an
  unsupported endpoint. The release notes and security policy were read, but
  this does not establish that there are no advisories. Recheck advisories and
  NIST FIPS 203/204 errata/SP 800-227 before approval and dependency integration.
  Direct access to standards publishers was unavailable under current policy;
  standards requirements were cross-checked against pinned source/API docs
  and NIST's published ACVP data, not a newly downloaded standards PDF.

## Test baseline and retained tests

No active build existed. CMake and Boost/Libevent/SQLite development headers
are missing. Shell HTTPS to GitHub fails to connect to the configured proxy;
GitHub connector reads work. Full configure, node unit/functional baseline,
Qt build, fuzzing, and end-to-end benchmarks therefore remain unrun. The actual
host is Debian 13 x86-64, GCC 14.2, Intel Xeon Platinum 8573C, three available
CPUs and approximately 9.7 GiB RAM, not the requested Ryzen/WSL2 machine.

The added standalone compiler/test path uses existing crypto translation units
and external pinned ML-KEM only. It verifies the old wallet HKDF golden output
and the existing transaction signature golden fingerprint, public NIST vectors,
and signing-domain negatives. See validation evidence for exact commands,
counts, and sanitizer limitations. Passing it is not a full-node regression pass.

Preserve `crypto_tests`, `script_tests` PQ authorization cases, `net_tests`'s
`v2transport_test`, `bip324_tests`, `src/test/fuzz/{bip324,p2p_transport_serialization}`,
`p2p_v2_transport.py`, `p2p_v2_encrypted.py`, `p2p_v2_misbehaving.py`,
`feature_proxy.py`, seed/addr/addrv2 tests, native PQ wallet/send/relay/auth/reorg
tests, header replay, and Mercatura block-capacity/chunking tests. Add the new
transport as additional coverage; do not change legacy expected outputs to make
PQ tests pass.

## Findings requiring explicit follow-up

1. Custom SHA3-384 AKE needs maintainer design approval and independent protocol
   review. Algorithm standardization is not handshake standardization.
2. Empty mainnet/public-testnet seed configuration cannot satisfy decentralized
   normal bootstrap without additional work and evidence.
3. Required-policy propagation must close all V1 reconnect/manual/proxy paths.
4. Expensive crypto cannot execute on the global socket/message locks; asynchronous
   completion must satisfy the streaming API's progress and lifetime contracts.
5. Existing wallet-only HKDF is insufficient for 48-byte confirmation keys;
   extending it must preserve every wallet derivation output.
6. Buffer limits do not bound CPU, stacks, source churn, or worker cancellation;
   these need separate admission limits and measured enforcement.
7. Persistent identity privacy and deliberate pin rotation must be explicit.
8. Full builds, official signature vectors, advisory/errata recheck, cross-platform
   validation, integrated mining testing, and end-to-end benchmarks are pending.
9. No configured `project` remote or working shell network exists for the
   requested validated push. Never substitute `origin` or force-push.
