# Mercatura PQ P2P Transport v1 — candidate 1

**UNAPPROVED DESIGN. No custom handshake or production transport is implemented.**
This is the byte-exact candidate submitted for the handoff's P2 checkpoint.
The constants and public fixtures identify this candidate for review; maintainer
approval must name the document revision/commit. Any protocol change requires
new fixtures and renewed approval before implementation. Approval is not an
independent security audit or deployment authorization.

Read the [source audit](mercatura-pq-audit.md), [dependency manifest](mercatura-pq-dependencies.json),
[validation report](mercatura-pq-validation.md), and [permanent candidate fixtures](../../src/test/data/pq_p2p_candidate1.json).
This construction uses ordinary RFC 5869 HKDF and transcript signatures. It is
not standard Noise, TLS, Quantus pqXX, or an independently audited AKE.

## 1. Scope, assumptions, and assurance

The protocol encrypts an existing Mercatura P2P byte stream after fresh
ML-KEM-768 exchange, mutual ML-DSA-65 identity signatures, and two explicit
secret-confirmation messages. It does not modify any block, transaction, wallet,
PoW, validation rule, chain selection, reward, or peer consensus authority.

* **Level A:** encryption and proof of possession of a presented key. An active
  intermediary can substitute its own identities and establish two different
  valid sessions. This level does not authenticate the intended remote node.
* **Level B:** Level A plus exact match to an expected identity/fingerprint
  configured through a trusted operator binding. Each direction has its own
  expected-peer policy; one side pinning does not imply the other side pins.
  A stolen expected identity key still permits impersonation of that identity.

Both sides always verify signatures, even for unpinned public peers. No pin or
identity grants whitelist status, mining authority, service trust, validation
exemptions, chain preference, or eviction protection. Permissionless public
connections need no pin. The protocol neither prevents Sybils/eclipses nor
establishes an identity's real-world ownership.

Assumptions: fresh secure RNG, FIPS-conforming primitives, correct implementation,
authenticated operator pins when Level B is claimed, secure deletion for forward
secrecy, and uncompromised endpoints during the session. Security claims are
goals pending analysis; no formal verification or independent audit is claimed.

There is no 0-RTT, resumption, PSK, classical hybrid mode, cipher negotiation,
padding extension, handshake compression, transport error record, or identity
rotation message in v1.

## 2. Canonical notation and constants

`||` is byte concatenation. `u8`, `u16`, `u24`, `u32`, `u64` are unsigned integers
of the named bit widths (`u24` occupies exactly three bytes);
multi-byte integers here use little endian. Offsets are zero based. Strings are
ASCII without an implicit NUL. `D(s) = u16(len(s)) || ASCII(s)`; its length
counts ASCII bytes. This is the only variable-length domain-label encoding.
No Bitcoin CompactSize or C-string serialization is used for handshakes.

`H(x) = SHA3-384(x)`, always the full 48 bytes.
`MAC(k,x) = HMAC-SHA3-384(k,x)`, always the full 48 bytes.
`Extract(salt,IKM) = MAC(salt,IKM)`.
`Expand(PRK,info,L)` is RFC 5869 section 2.3 with HashLen=48, byte counters
1 through 255, empty T(0), and a maximum L of 12,240. Producing a 32-byte
ChaCha key from Expand is intentional output sizing, not transcript truncation.

| Constant | Exact value |
| --- | --- |
| Preface `P` | ASCII `Mercatura/P2P/PQ`, 16 bytes, hex `4d65726361747572612f5032502f5051` |
| `VERSION` | `u16(1)` = `0100` |
| `SUITE` | `u16(1)` = `0100` |
| `FLAGS` | `u16(0)` = `0000`; every other value fails |
| Role I | `00`, TCP connector; role is never selected by key ordering |
| Role R | `01`, TCP acceptor |
| Suite 1 | ML-KEM-768 / Pure ML-DSA-65 / SHA3-384 / RFC 5869 HMAC-SHA3-384 / FSChaCha20 and FSChaCha20Poly1305 |
| KEM sizes | encapsulation key 1,184; decapsulation key 2,400; ciphertext 1,088; secret 32; keygen coins 64; encapsulation coins 32 |
| ML-DSA sizes | public key 1,952; expanded secret key 4,032; signature 3,309; identity seed 32; signing randomness 32 |
| Signature context `CTX` | ASCII `Mercatura/P2PAuth/v1`, 20 bytes, without NUL; hex `4d65726361747572612f503250417574682f7631` |

The preface identifies the transport family. Version and suite are repeated in
every record header, authenticated by the transcript, and must be exactly 1.
No version list or fallback negotiation exists. Never negotiate a smaller suite
after failure. Constants are local to this protocol, never wallet/transaction
authorization domain strings.

Domain strings used below, all encoded with D when used in a formula:

```
Mercatura/P2P/v1
Mercatura/P2PTranscript/v1
Mercatura/P2PSignature/v1
Mercatura/P2PExtract/v1
Mercatura/P2PKeySchedule/v1
Mercatura/P2PConfirm/v1
Mercatura/P2PPacket/v1
Mercatura/P2PIdentity/v1
Mercatura/P2PIdentityFile/v1
```

`CTX` is passed as a FIPS 204 context, not D-encoded inside the signing API.

## 3. Network discriminator

`N = chain_id[1] || message_start[4] || genesis_raw[32] || challenge_hash[48]`
is exactly 85 bytes.

| chain_id | Network |
| --- | --- |
| `00` | Mercatura mainnet |
| `01` | Mercatura public testnet (`ChainType::TESTNET`) |
| `02` | signet |
| `03` | regtest |
| `04` | inherited testnet4 (`ChainType::TESTNET4`) |

`message_start` is `Params().MessageStart()` in array order. `genesis_raw`
is the raw canonical 32-byte `uint256` serialization of the selected genesis
hash, equivalent to `GetConsensus().hashGenesisBlock.begin()..end()`; it is the
reverse of the usual displayed big-endian hex. For signet,
`challenge_hash=H(raw signet challenge script bytes)` with no CompactSize length.
For every other network it is `H(empty)`. Custom signets therefore separate
even if a future configuration reuses another discriminator component.

Both M1 and M2 carry N and must match the local N exactly. No remote field can
select a different local chain. Chain IDs other than the listed values fail.
Implementations must not hardcode genesis values from this audit because the
approved mining branch changes chain configuration. Regenerate network-specific
fixtures only through an explicit reviewed protocol/test update if integration
changes a genesis. No chain parameter is changed by this protocol.

## 4. Prefaces and record layouts

I sends `P || M1`. R validates P and the header, admits bounded body storage,
checks N as it arrives, and finishes validating M1 before sending `P || M2`.
Each direction has exactly one P. An outbound PQ connection
accepts only P as the response preface. R sends no bytes before a complete valid
M1 has been admitted and its cheap checks pass.

Every message M is `header || body`. Header length is 12:

| Offset | Bytes | Meaning |
| --- | ---: | --- |
| 0 | 1 | type, exactly 1, 2, 3, 4, or 5 as expected by state |
| 1 | 1 | sender role, 0 for M1/M3/M5; 1 for M2/M4 |
| 2 | 2 | VERSION |
| 4 | 2 | SUITE |
| 6 | 2 | FLAGS |
| 8 | 4 | exact body length from the following tables |

Unknown types, roles, flags, versions, suites, nonmatching expected sizes, and
extensions fail before body allocation or expensive cryptography. There are no
optional fields, implicit lengths, alternative encodings, or trailing body bytes.

### M1 — I to R, InitiatorHello

Header hex: `0100010001000000b50c0000`.

| Body offset | Bytes | Field |
| --- | ---: | --- |
| 0 | 85 | N |
| 85 | 32 | fresh `nonce_I` |
| 117 | 1,184 | fresh ephemeral ML-KEM encapsulation key `ek_I` |
| 1301 | 1,952 | identity public key `id_I` |

Body 3,253 bytes; total 3,265. M1 presents a key but proves no possession.

### M2 — R to I, ResponderHello

Header hex: `0201010001000000f5100000`.

| Body offset | Bytes | Field |
| --- | ---: | --- |
| 0 | 85 | N, identical to M1 and local network |
| 85 | 32 | fresh `nonce_R` |
| 117 | 1,184 | fresh ephemeral encapsulation key `ek_R` |
| 1301 | 1,088 | `ct_R`, encapsulated by R to `ek_I` |
| 2389 | 1,952 | identity public key `id_R` |

Body 4,341 bytes; total 4,353. R generates `(ct_R,ss_R)` using fresh
32-byte encapsulation coins. M2 itself does not authenticate R.

### M3 — I to R, InitiatorAuthentication

Header hex: `03000100010000002d110000`.

| Body offset | Bytes | Field |
| --- | ---: | --- |
| 0 | 1,088 | `ct_I`, encapsulated by I to `ek_R` |
| 1088 | 3,309 | `sig_I`, defined in section 6 |

Body 4,397 bytes; total 4,409. I decapsulates `ct_R` with `dk_I`, obtains
`ss_R`, and generates `(ct_I,ss_I)` with new encapsulation coins. R then
decapsulates `ct_I` with `dk_R`, obtains `ss_I`, and verifies sig_I.

### M4 — R to I, ResponderAuthenticationAndConfirmation

Header hex: `04010100010000001d0d0000`.

| Body offset | Bytes | Field |
| --- | ---: | --- |
| 0 | 3,309 | `sig_R` |
| 3309 | 48 | `confirm_R` |

Body 3,357 bytes; total 3,369. R MUST verify sig_I and its expected-peer
binding before creating M4. I verifies sig_R and its binding before accepting
confirm_R. No application message is released on either side at this step.

### M5 — I to R, FinalConfirmation

Header hex: `050001000100000030000000`.

| Body offset | Bytes | Field |
| --- | ---: | --- |
| 0 | 48 | `confirm_I` |

Body 48 bytes; total 60. I creates M5 only after successfully authenticating R
and verifying confirm_R. R accepts application traffic only after verifying M5.

### Exact wire budgets

Largest complete record: 4,409 bytes. Entire successful exchange including both
16-byte prefaces: **15,488 bytes**. I sends 7,750; R sends 7,738. These are exact
protocol byte counts, excluding TCP/IP/proxy overhead. No retransmitted record,
padding record, or handshake decoy is allowed. TCP retransmission is transparent.
A peer exceeding its directional handshake budget fails immediately.

## 5. Exchange and contributory-secret rationale

```mermaid
sequenceDiagram
    participant I as Initiator
    participant R as Responder
    I->>R: P, M1(N, nonce_I, ek_I, id_I)
    Note over R: Admit; fresh ek_R,dk_R; encapsulate to ek_I
    R->>I: P, M2(N, nonce_R, ek_R, ct_R, id_R)
    Note over I: Decapsulate ct_R; encapsulate to ek_R; sign transcript
    I->>R: M3(ct_I, sig_I)
    Note over R: Verify I and pin; decapsulate ct_I; derive PRK
    R->>I: M4(sig_R, confirm_R)
    Note over I: Verify R and pin, then confirm_R
    I->>R: M5(confirm_I)
    Note over I,R: Traffic keys use final transcript; application gate opens locally
```

Both endpoints generate a fresh ML-KEM keypair and fresh encapsulation coins for
every attempt. They share no static KEM keys and reuse no ephemeral material
after success, failure, disconnect, or reconnect. Each identity key is separately
generated. The two ML-KEM secrets are combined in fixed role order, not XORed.
KEM encapsulation is not DH; no DH-style commutativity or contributory proof is
assumed. An honest contribution's secrecy and the KEM/HKDF assumptions motivate
the construction; this document does not claim a generic malicious-contribution
combiner theorem or a proof of AKE security.

An implementation first applies upstream public-key modulus checks and exact
sizes. Every nonzero library status fails closed. Correct-length bad ciphertexts
may return zero with implicit-rejection secrets. Do not add a ciphertext validity
test or expose an implicit-rejection oracle. Such sessions cannot pass the
subsequent secret confirmations with the honest peer, except with negligible
MAC-forgery probability under the assumptions. A malicious peer can of course
know both of its own legitimate session secrets.

Forward secrecy against **later identity-key compromise** requires that both
fresh decapsulation keys, encapsulation coins, shared secrets, PRK and copied
buffers have been erased. Identity signatures alone reveal none of these.
Compromise of endpoint memory, RNG, ephemeral keys or current traffic keys is
outside that claim. Rekeying is a one-way symmetric ratchet, not post-compromise
recovery. It cannot restore secrecy to an already compromised live session.

## 6. Transcripts and exact signing semantics

Let `A = D("Mercatura/P2P/v1") || P || M1 || P || M2`.
The two identical prefaces occupy different, fixed transcript positions. Define:

```
TH(x) = H(D("Mercatura/P2PTranscript/v1") || x)
X3    = A || header3 || ct_I
h3    = TH(X3)
msg_I = D("Mercatura/P2PSignature/v1") || 00 || h3
sig_I = PureMLDSA65.Sign(sk_I, msg_I, CTX, rnd_I)
T3    = A || M3
msg_R = D("Mercatura/P2PSignature/v1") || 01 || TH(T3 || header4)
sig_R = PureMLDSA65.Sign(sk_R, msg_R, CTX, rnd_R)
T4sig = T3 || header4 || sig_R
```

Headers 3 and 4 always contain their full final body lengths, even when a
prefix of their body is hashed before its own signature/MAC exists. This avoids
self-reference. sig_R binds sig_I. Both signatures bind both identities, both
ephemeral keys, both ciphertexts, both nonces, network discriminators, suite,
version, flags, full earlier record headers, and sender-role labels. No field is
omitted by semantic reserialization: hash the exact canonical accepted bytes.
An incremental SHA3 state or its copies may replace retaining the public bytes,
but must produce exactly these hashes. No chaining hash shortcuts are equivalent.

Use existing `mercatura_mldsa65_sign` and `mercatura_mldsa65_verify` with
the exact msg_I/msg_R byte strings and context bytes. The underlying pinned API
is `PQCP_MLDSA_NATIVE_MLDSA65_signature_internal(signature, message, message_len,
prefix, prefix_len, randomness, secret_key, 0)`, and the corresponding
`verify_internal(..., public_key, 0)`. Prefix is
`00 || uint8(len(CTX)) || CTX`, assembled by the existing wrapper exactly once.
The final zero argument selects ordinary message input, not external μ input.
Public-key length 1,952 and signature length 3,309 are mandatory.
The wrapper's complete FIPS prefix is hex `0014` followed by the 20 CTX bytes;
msg_I and msg_R are each exactly 76 bytes.

This is **Pure ML-DSA signing a protocol message that contains a SHA3-384
transcript digest**. There is no HashML-DSA prehash identifier/OID, no externally
computed μ, and no HashML-DSA API call. FIPS 204's internal SHAKE processing
remains unchanged. Do not describe this as HashML-DSA-SHA3-384. Production rnd_I
and rnd_R are fresh independent secure 32-byte draws; all-zero or fixed draws
are permitted only in public deterministic tests. A context length includes no
NUL, newline or u16 prefix. Signature errors abort and cleanse outputs.

## 7. RFC 5869 key schedule and confirmation

Use 32-byte ss_R from R's encapsulation to I and 32-byte ss_I from I's
encapsulation to R. A valid primitive return does not imply peer authentication.

```
IKM  = u16(32) || ss_R || u16(32) || ss_I
salt = H(D("Mercatura/P2PExtract/v1") || h3)
PRK  = Extract(salt, IKM)                         # 48 bytes
info(label, L, context) = D("Mercatura/P2PKeySchedule/v1") ||
                         D(label) || u16(L) || context
K(label, L, context) = Expand(PRK, info(label,L,context), L)
kc_R = K("confirm/r2i", 48, h3)
kc_I = K("confirm/i2r", 48, h3)
confirm_R = MAC(kc_R, D("Mercatura/P2PConfirm/v1") || 01 || TH(T4sig))
T4 = T4sig || confirm_R
confirm_I = MAC(kc_I, D("Mercatura/P2PConfirm/v1") || 00 || TH(T4 || header5))
T5 = T4 || header5 || confirm_I
h5 = TH(T5)
```

MAC comparison must be constant-time over all 48 bytes. Verify sig_R before
accepting confirm_R. Verify sig_I before sending M4. I's M5 acknowledges the
authenticated responder message; R does not enable application traffic based
only on sig_I. This is explicit secret confirmation in both directions; it
does not solve the impossibility of infinite common knowledge over a lossy
connection. A disconnect after M5 can always leave one side uncertain about
the other side's receipt.

After confirmation, derive each item independently with the same PRK:

| Label | L | Use |
| --- | ---: | --- |
| `app/i2r/length` | 32 | I send / R receive length cipher |
| `app/i2r/payload` | 32 | I send / R receive payload AEAD |
| `app/r2i/length` | 32 | R send / I receive length cipher |
| `app/r2i/payload` | 32 | R send / I receive payload AEAD |
| `session/id` | 32 | public session ID |

For each row, output is `K(label,L,h5)`. No combined multi-key output slicing is
used. There is no traffic key until h5 is fixed. Both sides derive the same
public session ID; display its 32 output bytes directly in lowercase hex,
without uint256 display-order reversal. Never expose PRK, kc, ss, identity seeds
or traffic keys through status interfaces.

Use a new general HKDF facility with independent P2P labels. Preserve the current
wallet L32 interface, salts, paths, account/branch/index serialization, and every
existing wallet result. Confirmation requires full 48-byte outputs; repeatedly
calling the wallet's 32-byte Expand32 cannot implement this schedule.

## 8. Post-handshake packet transport

Reuse the existing **FSChaCha20** length cipher and
**FSChaCha20Poly1305** payload cipher implementations, unchanged, initialized
with the four independent traffic keys. Do not construct a BIP324Cipher through
its ECDH initializer. A separate PQ packet wrapper performs the following for
each direction, starting with fresh counter state at zero:

```
wire_length = FSChaCha20.Crypt(u24le(len(contents)))
packet_aad  = D("Mercatura/P2PPacket/v1") || h5 || sender_role || wire_length
plaintext   = control_byte || contents
wire_packet = wire_length || FSChaCha20Poly1305.Encrypt(plaintext, packet_aad)
```

`control_byte` is `00` for an application packet, or `80` for a discard/decoy
packet. All other bits must be zero in v1. Contents for an ordinary packet are
existing V2 command encoding plus its original P2P payload; decoy contents are
arbitrary bytes and never delivered. Empty ordinary contents are invalid. Packet
expansion is 3+1+16=20 bytes. There is no garbage terminator or transport-version
packet after M5; the first application message can be the queued P2P version.
The handshake's header must never be mistaken for a packet header.

The fixed short-command mapping is IDs 1 through 28 in order:

```
addr block blocktxn cmpctblock feefilter filteradd filterclear filterload
getblocks getblocktxn getdata getheaders headers inv mempool merkleblock
notfound ping pong sendcmpct tx getcfilters cfilter getcfheaders cfheaders
getcfcheckpt cfcheckpt addrv2
```

ID 0 is followed by the existing 12-byte command field: nonzero command bytes
in the inclusive range 0x20..0x7f, then only zero padding to 12 bytes. Empty
commands fail message decoding. IDs 29..255 are reserved and invalid for v1.
Senders use short IDs when available; long encoding preserves the existing
command namespace, including Mercatura's block chunking. A receiver may accept
a valid long command even if it also has a short ID, as existing V2 does.
Message-codec extraction must preserve V2's own behavior separately, including
its treatment of currently unimplemented reserved IDs.

Reject a decrypted contents length over **4,000,013** before allocating for
the rest of the packet. This is 4,000,000 maximum payload plus the largest
13-byte command encoding at the audit base, independent of block capacity.
Do not raise the generic limit to carry a large block; preserve chunk transfer.
Recheck all existing local message limits as an additional cap. A malformed
length, header flag, AEAD tag, or stream order fails the connection; no plaintext
may be released before tag verification. A syntactically invalid authenticated
command is rejected as a message under the existing application policy.

Each call advances the length cipher by exactly 3 bytes and one chunk, and the
payload cipher by one packet, including decoys. Partial socket writes do not
reencrypt or advance either cipher a second time. DecryptLength is called once
per packet and its result cached. Failed AEAD means terminal disconnect, never
retry with advanced counters.

Both directions rekey every 224 packets, exactly as the existing FS classes:
AEAD uses nonce `u32(packet_in_epoch) || u64(epoch)` and derives the next
key from the first 32 bytes of ChaCha keystream at the reserved nonce
`u32(0xffffffff) || u64(epoch)`, starting at block 1. The length stream consumes
the next 32 keystream bytes after its 224 three-byte chunks as its next key,
then restarts at nonce `u32(0) || u64(epoch+1)`, block 0. Original implementations
remain authoritative for this established BIP324 machinery; frozen independent
packet vectors and boundary tests must verify that this description agrees.
Neither HKDF PRK nor KEM material is retained for rekeying. Close before sending
or accepting packet number 2^32 in either direction, well before epoch overflow.

## 9. Asynchronous state machine and application gate

The local state is explicit and includes send progress; a queued frame is not
a completely sent frame. A bounded crypto job is a substate, never a detached
unlimited task. A worker result carries connection identity plus a generation
counter; a result for a disconnected/replaced connection is discarded and its
secret buffers erased.

| Side/state | Permitted event | Result |
| --- | --- | --- |
| I INITIAL | admitted local job produces new keys/M1 | SEND_M1 (P+M1), then WAIT_M2 |
| R DETECTING | exact P, valid complete M1, admission | CHECK_M1/MAKE_M2; then SEND_M2 (P+M2), then WAIT_M3 |
| I WAIT_M2 | exact P + valid complete M2, early expected-key check | CHECK_M2/MAKE_M3; then SEND_M3, then WAIT_M4 |
| R WAIT_M3 | complete valid M3, sig_I and pin verified, KEM success | MAKE_M4; SEND_M4, then WAIT_M5 |
| I WAIT_M4 | complete valid M4, sig_R/pin/confirm_R verified | MAKE_M5; SEND_M5 |
| I SEND_M5 | all M5 bytes reported by MarkBytesSent | ESTABLISHED; application send/receive enabled |
| R WAIT_M5 | complete M5 and valid confirm_I | ESTABLISHED; application send/receive enabled |
| either any pending state | invalid input, deadline, cancellation, failed job or shutdown | FAILED; discard application state, cleanse, disconnect |
| either ESTABLISHED | invalid packet or disconnect | FAILED/CLOSED; no resumption |

Every header role and type must match the single expected step. Receiving the
next step requires completion of the previous outbound frame. On normal TCP
ordering the peer cannot have a valid response before receiving that frame.
No step may be executed twice; a duplicate or replay within a stream fails.
Do not accept a response as a new connection or swap roles.

`ReceivedMessageComplete` is false before ESTABLISHED; `GetReceivedMessage`
must not return application data; `SetMessageToSend` returns false without
mutating its message. The node's queued version message can remain owned by
the existing queue, but no version bytes are serialized into packets early.
R's first application packet may share a TCP read with a received M5 on the
opposite side: bytes following a successfully verified M5 are ordinary packet
bytes, not forbidden trailing handshake fields. Conversely extra bytes after
M1–M4 while waiting for a dependent local response cannot be legitimate next
steps and are rejected without expensive additional processing.

Fragmented reads accept an arbitrary prefix of the expected preface/header/body,
including a single byte. No per-fragment signing, decapsulation or allocation
growth is allowed. Fixed buffers have known exact capacities. `ReceivedBytes`
must either consume input, report failure, or use an explicitly reviewed caller
pause/wakeup mechanism; simply returning success with unconsumed bytes while
waiting for a worker violates `CNode::ReceiveMsgBytes`'s progress loop.

Serialize worker completion onto an owning network context, using bounded
per-node snapshots and short transport locks. Never hold `m_nodes_mutex`,
`g_msgproc_mutex`, `cs_main`, per-node send/receive locks or transport locks
across expensive KEM/keygen/signature operations. Recheck generation and stage
when applying the result. Test shutdown/disconnect during every job and partial
write. Cancellation can prevent publication of a result; it must not free
buffers still used by the worker.

## 10. Transport selection and downgrade policy

In a future mixed inbound listener, compare up to 16 initial bytes against P
before letting existing V2 infer its key from a V1-prefix mismatch. Retain those
bytes once; if P no longer matches and compatibility permits legacy transport,
hand them to the unchanged legacy detector. If all P bytes match, commit to PQ
and never reinterpret failures as V1/V2. A random BIP324 key matches P with
probability approximately 2^-128 under uniform-key assumptions. Deliberately
crafted collisions are possible and only cause that connection to fail closed;
the preface is not an authenticator or a cryptographic security boundary.
P differs from the known network-magic-plus-version prefixes at the audit base.
Any future magic change must rerun the discriminator collision checks.

Outbound policy is decided before connecting and attached to the connection,
retry request, destination and pin. Proposed modes:

* `off`: existing V1/V2 only.
* `prefer`: PQ attempted where policy selects it; any separate legacy attempt
  must be an explicitly permitted operator compatibility decision and reported
  accurately. A failed started PQ authentication never triggers automatic
  classical retry for that attempt. This candidate intentionally does not need
  an automatic post-PQ fallback path.
* `required`: every ordinary P2P attempt is PQ, including manual, feeler,
  addr-fetch, block relay, private broadcast, Tor/proxy and reconnect paths.
  No V1/V2 connection and no V1 reconnect request can satisfy it.

An expected identity implies required PQ for that destination in every mode;
reject contradictory options at startup/RPC validation. `ShouldReconnectV1`
always returns false for PQ. Remote service bits, timeouts, network errors,
identity mismatches and unknown versions never weaken required policy.

**Stage A implementation boundary after design approval:** default off on every
network; experimental PQ enabled only with an explicit regtest option. Startup
rejects enabling this candidate on mainnet or public networks. This is an
engineering containment policy, not a wire version change. Public-testnet opt-in
and later enforcement are separate, authorized development stages. Existing
public-testnet behavior must remain unchanged at Stage A.

## 11. Identity lifecycle, storage, and pinning

Default identity policy: fresh ephemeral ML-DSA seed/keypair per connection.
Persistent identity is an explicit operator choice, typically for a public
service or a pinned peer. There is no wallet dependency, unlocking requirement,
TOFU database, implicit pin, or correlation across networks. Public identity
keys and signatures are sent in cleartext during this candidate handshake.
Persistent keys are therefore correlatable by passive and active observers.
This privacy cost is deliberate and requires approval; Tor hides IP origin but
does not hide an exposed stable identity. Operators needing identity privacy
must not select persistence without understanding this limitation.

Persistent seed files are per selected N, opened only through an explicit
configuration in the network-specific datadir. A candidate file format is:

```
ASCII("MCAPQID1")[8] || u16(1) || N[85] || seed[32] || id_public[1952] || checksum[48]
checksum = H(D("Mercatura/P2PIdentityFile/v1") || preceding_file_bytes)
```

Exact file length: 2,127 bytes. This checksum detects accidental corruption, not
malicious writes by an actor who can change the file. On load check ownership,
permissions, no symlink/special-file substitution, exact size/version/network,
checksum and recomputed keypair/public-key equality before signing. A missing
explicit persistent file is a startup error; create it only through an explicit
generation operation. Never silently regenerate on corruption or key loss.
Seed storage is unencrypted at rest in this candidate and requires filesystem
protection; encryption-at-rest would need a separately reviewed unlock design.

Linux/macOS creation uses an exclusive owner-only temporary file in the same
directory, mode 0600, protected directory, full write, fsync/FileCommit, atomic
no-clobber publication, and directory durability where supported. Existing files
must not be overwritten on startup. Windows uses an owner-restricted ACL and
equivalent exclusive/atomic publication; POSIX chmod alone is insufficient there.
Rotation is an explicit offline/operator action with backup policy. New files
must never replace pinned identities automatically. Restrict expanded secret
key lifetime and wipe seed/secret temporary copies on all paths.

Fingerprint bytes are:

```
fp = H(D("Mercatura/P2PIdentity/v1") || u16(1) || N || u16(1952) || id_public)
display = "mcapq1:" || lowercase_hex(fp)
```

There is no whitespace, separator inside the hex, truncation, alternate case,
or uint256 byte reversal. Expected pins store all 48 fingerprint bytes or an
exact 1,952-byte key for the selected N. Compare computed fingerprint/key using
constant-time equality after structural checks and again require signature
verification. An early expected-key mismatch may abort before signing, but an
early match is not proof of possession. A fingerprint is a key identifier, not
a certificate. Pin lookup is by configured destination (including hostname/onion
name and port), not unauthenticated peer announcements or wallet addresses.

Pinned keys never rotate automatically. Replacing a pin requires a trusted
operator update; warn locally about the mismatch, do not accept a replacement
to preserve connectivity. No cross-sign rotation or TOFU is defined in v1.

## 12. Resource policy and failure handling

These are proposed local admission defaults for Stage A, requiring measured
qualification before wider deployment. They do not change wire bytes:

| Resource | Default hard bound |
| --- | --- |
| Preface/header storage | 16+12 bytes per inbound detector before admission |
| Handshake record buffers | One receive and one send slot, each at most 4,409 bytes; reject header length before reserve |
| Cumulative wire bytes | 15,488 total; I-to-R 7,750; R-to-I 7,738 |
| PQ pending-session storage | 64 KiB per admitted session, including transcript, owned keys and any queued job snapshot; no duplicate uncharged job buffers |
| Concurrent admitted pending sessions | 64 globally, including outbound and worker-pending sessions; also subject to existing connection limits |
| Crypto workers | At most 2 jobs executing, 32 queued, one outstanding job per session |
| Worker stack reservation | 1 MiB each initially; this is a reservation target to validate, not a measured library stack bound |
| Source pending cap | 4 for known IPv4 /32 or IPv6 /64; configurable within global cap for shared/NAT deployments |
| Anonymous pending pool | 16 within the 64 global cap when source cannot be identified (Tor/proxy inbound); never identify users by the proxy IP |
| Admission rate | Global token bucket, burst 32, refill 8 new sessions/second; known-source bucket burst 4, refill 1/second |
| Source accounting entries | At most 256, idle entries expire after 120 seconds; an untracked source must still obey global admission limits |
| Preface deadline | 5 seconds from accepted socket (including slow trickles) |
| Each expected complete record | 30 seconds from entering the wait state, not from last fragment |
| Total pending lifetime | 120 seconds from TCP connection, including sends and queued jobs |
| Queue wait / executing job deadline | 2 / 5 seconds respectively; failure cancels publication and closes, with running job cleanup on completion |
| Diagnostic rate | At most one failure record per connection; global 10 records/second, burst 20, plus bounded aggregate counters |

Use monotonic time, not peer timestamps or a sliding last-receive deadline.
Overridden testing timeouts may be larger only under explicit isolated-regtest
configuration. No additional inbound slot is admitted by evicting an established
peer solely to make room for an incomplete PQ handshake. Existing ban policy
continues to apply; resource rejections alone do not cause lasting identity/IP
bans. Shared-source caps can affect NAT users and anonymous pools can be
contested; test fairness and tune only with measured data, retaining global caps.

The 64 KiB budget covers PQ-owned session/job data, not all CNode/socket/process
allocations. Worker stack reservations add at most 2 MiB under the proposed
configuration; allocator, thread runtime, socket buffers and established traffic
need separate RSS/accounting measurements. Upstream major-allocation constants
are lower than total stack usage. Do not claim a proven peak-RAM figure until
allocation instrumentation and platform stack tests pass. If the implementation
cannot enforce these bounds, revise the design and obtain review before widening
deployment. The bounded worker design caps concurrent CPU work but cannot prevent
an admitted malicious peer from making us perform allowed expensive operations.

Admission and exact sizes precede key generation, sign or verify. Do not sign a
response to mere preface bytes. Cache a stage result and never redo crypto on
fragment, duplicate, logging, retry, or stats access. Shut down workers cleanly;
all captured secret buffers remain owned until workers finish and are then
cleansed, including rejected stale results.

On any authentication, pin or confirmation failure: release zero application
messages, clear provisional assurance/status, prohibit fallback, erase secrets,
release quota, disconnect. Send no detailed wire error. Local logs can distinguish
unsupported version, pin mismatch, timeout, resource rejection and general
authentication failure without key/signature/secret dumps. Implicit-rejection
outcomes and MAC mismatches must not become a detailed decapsulation oracle.

## 13. RPC and Qt assurance model

Retain existing `transport_protocol_type`, adding `"pq-v1"`, and `session_id`.
Extend copied transport/node stats with:

| Field | Meaning |
| --- | --- |
| `pq_encrypted` | Both required authentication steps and local confirmation gate completed; traffic cipher established |
| `pq_authenticated` | Remote presented identity-key possession verified in the completed session |
| `pq_identity_verified` | Completed session additionally matches an expected trusted binding |
| `pq_identity` | Public fingerprint only for an established session, optional privacy policy |
| `pq_kem` | `"ML-KEM-768"` for an established PQ session, empty otherwise |
| `pq_auth_algorithm` | `"ML-DSA-65"` for an established PQ session, empty otherwise |

Truthful statuses: legacy/pending/failed `(false,false,false)`; completed unpinned
PQ `(true,true,false)`; completed pinned PQ `(true,true,true)`. No completed PQ
session can report encryption without the mandatory presented-key authentication.
The bits still have different meanings and must be tested independently; the
third can never be inferred from the first two. An implementation may have
internal provisional proof/secret states, but must not expose them as completed
encryption. Labels in the existing Qt peer panel must say encryption established,
presented-key possession verified, expected identity verified. Do not label an
unpinned public peer independently verified or trusted.

## 14. Discovery and staged qualification

Proposed experimental service hint `NODE_P2P_PQ = uint64_t(1) << 24` is within
the source's temporary-experiment range and currently unused in the audited tree.
It is a hint, not an authentication or policy signal, and remains subject to
registration/collision review before public use. Advertise only when a node
actually accepts PQ, and do not alter NODE_NETWORK/validation semantics.
Stage A uses isolated regtest peer fixtures and explicit peer discovery tests.

Required-mode selection must probe eligible addrman candidates with PQ even if
the hint is absent, preserve network-group diversity and anchors, cache observed
capability with bounded expiry/backoff, and never learn addresses by silently
opening a classical connection. Hints received over any permitted legacy path
remain unauthenticated. Failure of PQ can mark availability, never change required
policy. Pinning a configured peer does not pin all gossip or make it a seed.

Fresh-node bootstrap requires a separately qualified mechanism: multiple
independently operated address sources/seed operators and community peers,
bounded diverse initial PQ probes, cached addresses after first use, and documented
operator-configured independent fallback peers. Manual peers are supported but
must not be the only practical normal bootstrap strategy. There is no such
qualified seed set at this checkpoint. Empty mainnet/public-testnet seeds in
the audited source make this a launch blocker. No claim of decentralized normal
bootstrap is made from a static regtest connection graph.

Before Stage B, test optional public-testnet behavior only with authorization,
separate datadirs and coordinated opt-in. Before Stage C, rehearse required mode
with independent peers, wrong/stale capabilities, IPv4/IPv6/NAT, Tor/proxies,
project infrastructure unavailable, fresh datadir sync, partition recovery and
chain convergence. Do not change live service configuration or infrastructure
from this proposal. Stage D needs independent AKE review, resolved high/critical
findings, combined mining candidate, cross-platform release evidence, sustained
public-testnet qualification and explicit maintainer deployment approval.

## 15. Threat assessment and rejected alternatives

| Attacker / failure | Intended protection and limitation |
| --- | --- |
| Passive recording, future analysis | ML-KEM establishes session secrets; later identity compromise alone should not recover erased ephemeral KEM material. Sizes, timing, preface and persistent identity remain visible. No immunity from future algorithm breaks is claimed. |
| Active transcript mutation | Role/version/suite/network, both identities and KEM inputs are signed; transcript-dependent MACs and traffic keys fail on altered sessions. An unpinned MITM can still terminate two valid sessions under its own identities. |
| Compromised identity | Pins do not help after that identity's private key is stolen; offline rotation and trusted pin replacement are required. Past-session protection depends on deletion and ephemeral security. |
| Replay/reflection/unknown-key-share | Fresh nonces/ephemeral keys, ordered identities, roles and both contributions bind sessions. R's signature includes I's signature, and confirmation includes R's signature. Replays do not authorize early data. Multi-session adversarial analysis remains required. |
| Cross-network replay | Exact N in both hellos and all signed/KDF transcript contexts; signet challenge included. Local chain is never remotely selected. |
| Downgrade | Required policy set before connect; PQ marker/version failures terminate; every retry retains policy and pin. Administrative decision to use compatibility mode provides a weaker guarantee and must be labeled. |
| Malicious peer / flood / fragmentation | Fixed lengths, budgets, absolute deadlines, admission limits and bounded crypto work; does not prevent link saturation or complete denial by a powerful adversary. Crypto admission is independent of existing connection caps. |
| Eclipse / network isolation / poisoned discovery | Existing diversity, eviction and blockchain validation remain; authentication is not a Sybil defense. Decentralized discovery/partition testing is a separate launch requirement. |
| Local memory/filesystem compromise | Outside remote transport guarantees. Restrictive storage and cleansing reduce exposure, without a claim to eliminate crash dumps, swap, compiler copies, microarchitectural or physical attacks. |

Remaining cryptographic dependencies are explicit: application encryption and
MACs still use symmetric primitives with finite key/tag security margins, and
the surrounding OS, software distribution, DNS/address sources, RPC administration
and Tor/proxy protocols are not converted to PQ cryptography by this P2P suite.
Permitted compatibility connections retain V1's plaintext properties or BIP324's
classical secp256k1 ECDH. This proposal changes neither transaction/consensus
cryptography nor those surrounding services and makes no whole-system quantum
security claim.

Rejected alternatives:

1. **Replace BIP324 in place:** breaks identity, compatibility, and fallback
   accounting. Preserve it as its own protocol.
2. **Put 48-byte SHA3-384 into Noise/Clatter unchanged:** violates Noise's hash
   assumptions and hardcoded output handling. No such substitution is made.
3. **Copy Quantus pqXX or its static-KEM signature binding:** different library,
   signature size, hash, wire format and proof assumptions; requires analysis
   rather than direct copying. This candidate instead signs session transcripts.
4. **Static KEM only:** later static-key compromise could expose recordings;
   use fresh bidirectional ephemeral material.
5. **One KEM contribution:** simpler but leaves all encapsulation randomness on
   one side; two fresh contributions are preferred here, without claiming that
   concatenation automatically supplies an arbitrary robust-combiner theorem.
6. **Identity signatures without confirmation:** do not prove both peers derived
   the same secret; M4/M5 confirmation is mandatory before application release.
7. **HashML-DSA label for a digest signed by Pure API:** misstates FIPS semantics.
   The Pure message construction is explicit and tested by context negatives.
8. **Encrypted identities in candidate 1:** would add pre-authentication traffic
   keys, AEAD nonces and additional transcript states. Deferred; explicit privacy
   tradeoff requires maintainer review, and a revised encrypted-identity design
   would require new analysis, fixtures and approval.
9. **TOFU or mandatory project pin list:** TOFU does not authenticate first use;
   a mandatory list would undermine permissionless peers. Neither is defined.
10. **Emergency silent classical fallback:** incompatible with a required-PQ claim.

## 16. Candidate fixtures and approval checklist

`src/test/data/pq_p2p_candidate1.json` contains public deterministic example
keys/coins, all five exact record bytes and prefaces, signature API inputs,
transcript hashes, Extract/Expand inputs/outputs, confirmation MACs, fingerprints
and final traffic keys. It is a **design fixture**, calculated offline using
primitive APIs, not a transport implementation or evidence of node interoperability.
Its secrets are intentionally public and must never be used by a node. Primitive
checks against independent NIST data are separate from the protocol candidate.

Candidate worked examples do not replace an independent complete protocol
implementation. Before production handshake coding, review and freeze the exact
candidate fixture digest and signing semantics. Before interoperability claims,
obtain a second independently implemented fixture/packet derivation and exercise
the full negative matrix against real transports. No formal model has been run.

Required reviewer decisions:

- Accept or revise the complete custom AKE, ordering of two ephemeral KEM
  contributions, Pure ML-DSA transcript signing, HKDF concatenation, M4/M5
  confirmation and final-transcript traffic keys.
- Accept or revise cleartext identity exposure and ephemeral-per-connection
  default; persistent identity and unencrypted seed-file policy are opt-in.
- Accept or revise exact preface/headers/network binding/experimental capability
  and fixed application packet AAD/limits/rekey scheme.
- Accept or revise resource defaults and the bounded-worker integration plan;
  measurements may require revisiting them before public qualification.
- Acknowledge unpinned MITM limits, advisory/errata and independent-analysis gaps,
  missing full builds and the decentralized bootstrap launch blocker.
- Explicitly approve the reviewed document/fixture revision for isolated Stage A
  implementation. This does not approve a mining merge, public-network use,
  mandatory enforcement or deployment.

**Standardized cryptographic algorithms do not automatically produce a
standardized, audited, or secure handshake protocol.** Engineering tests,
maintainer design approval, independent security review, and production
authorization are separate milestones. A tentative launch window cannot waive
any of them.
